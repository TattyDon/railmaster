#include "railmaster/net/session.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace railmaster::net {

namespace {

// Bump when the messages change: games of different versions do not mix.
constexpr std::uint32_t kProtocol = 2;

// A checksum every game month (30 days).
constexpr std::uint64_t kCheckTicks = 30ULL * sim::World::kTicksPerDay;

enum Type : std::uint8_t {
    kHello = 1,   // guest: protocol, name
    kWelcome = 2, // host: the seat, the game as a save
    kRefuse = 3,  // host: why not
    kCommand = 4, // guest: a number for it, the command
    kStep = 5,    // host: run to this tick, these commands on the way
    kDone = 6,    // host: the guest's command numbered so, played (where in the log) or refused (why)
    kCheck = 7,   // host: the game's checksum at a tick
    kNote = 8,    // host: news for everyone's screen
    kPlayers = 9, // host: everyone's seat and name
};

class Out {
public:
    Out& u(std::uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) s_.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
        return *this;
    }
    Out& text(std::string_view t) {
        u(t.size(), 4);
        s_.append(t);
        return *this;
    }
    Out& rest(std::string_view t) {
        s_.append(t);
        return *this;
    }
    const std::string& str() const { return s_; }

private:
    std::string s_;
};

class In {
public:
    explicit In(std::string_view s) : s_(s) {}
    std::uint64_t u(int bytes) {
        if (s_.size() - at_ < static_cast<std::size_t>(bytes)) throw std::runtime_error("network: a message ends early");
        std::uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v |= std::uint64_t{static_cast<unsigned char>(s_[at_++])} << (8 * i);
        return v;
    }
    std::string text() {
        const auto n = static_cast<std::size_t>(u(4));
        if (s_.size() - at_ < n) throw std::runtime_error("network: a message ends early");
        std::string t(s_.substr(at_, n));
        at_ += n;
        return t;
    }
    std::string_view rest() {
        const std::string_view r = s_.substr(at_);
        at_ = s_.size();
        return r;
    }

private:
    std::string_view s_;
    std::size_t at_ = 0;
};

std::string player_name(sim::PlayerId seat) { return "player " + std::to_string(seat + 1); }

} // namespace

std::pair<std::string, std::uint16_t> split_address(const std::string& address) {
    const auto colon = address.rfind(':');
    // One colon: a port after it. More: an IPv6 address, without a port.
    if (colon == std::string::npos || address.find(':') != colon) return {address, kDefaultPort};
    const std::string port = address.substr(colon + 1);
    if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return {address, kDefaultPort};
    const long n = std::stol(port);
    if (n <= 0 || n > 65535) return {address, kDefaultPort};
    return {address.substr(0, colon), static_cast<std::uint16_t>(n)};
}

// ---- Host ----

Host::Host(sim::World& world, std::uint16_t port) : world_(world) {
    std::string error;
    listener_ = Listener::listen(port, error);
    if (!listener_.open()) throw std::runtime_error(error);
    sent_ = world_.command_log().size();
    sent_tick_ = world_.total_ticks();
}

void Host::say(const std::string& text) {
    news_.push_back(text);
    for (Seat& s : seats_)
        if (s.joined) s.conn.send(kNote, text);
}

void Host::send_step() {
    const auto& log = world_.command_log();
    if (log.size() == sent_ && world_.total_ticks() == sent_tick_ && pace_ == sent_pace_) return;
    const std::vector<sim::World::LoggedCommand> fresh(log.begin() + static_cast<std::ptrdiff_t>(sent_), log.end());
    Out m;
    m.u(world_.total_ticks(), 8).u(pace_, 1).rest(sim::World::encode(fresh));
    for (Seat& s : seats_)
        if (s.joined) s.conn.send(kStep, m.str());
    sent_ = log.size();
    sent_tick_ = world_.total_ticks();
    sent_pace_ = pace_;
}

void Host::send_players() {
    Out m;
    const auto list = players();
    m.u(list.size(), 2);
    for (const auto& [seat, name] : list) m.u(seat, 2).text(name);
    for (Seat& s : seats_)
        if (s.joined) s.conn.send(kPlayers, m.str());
}

void Host::handle(Seat& s, const Message& m) {
    In in(m.body);
    switch (m.type) {
    case kHello: {
        if (s.joined) return;
        const auto protocol = static_cast<std::uint32_t>(in.u(4));
        std::string name = in.text().substr(0, 24);
        if (protocol != kProtocol) {
            s.conn.send(kRefuse, "the host's game is a different version: both players need the same one");
            s.conn.flush(200);
            s.conn.close();
            return;
        }
        // The first free seat (seat 0 is the host's).
        const auto humans = static_cast<sim::PlayerId>(std::max(1, world_.config().humans));
        std::optional<sim::PlayerId> free;
        for (sim::PlayerId p = 1; p < humans && !free; ++p) {
            if (std::none_of(seats_.begin(), seats_.end(), [&](const Seat& o) { return o.joined && o.seat == p; })) free = p;
        }
        if (!free) {
            s.conn.send(kRefuse, humans <= 1 ? "this game has no seats for other players"
                                             : "the game is full (" + std::to_string(humans) + " players)");
            s.conn.flush(200);
            s.conn.close();
            return;
        }
        // Everyone else is told everything so far, then the newcomer gets
        // the game as it stands: from here all are told the same.
        send_step();
        if (name.empty()) name = player_name(*free);
        s.seat = *free;
        s.name = name;
        s.joined = true;
        Out w;
        w.u(s.seat, 2).rest(world_.save());
        s.conn.send(kWelcome, w.str());
        sent_pace_ = 0xFF; // the newcomer hears the pace with the next step
        send_players();
        say(name + " joined as " + player_name(s.seat));
        return;
    }
    case kCommand: {
        if (!s.joined) return;
        const auto seq = static_cast<std::uint32_t>(in.u(4));
        std::vector<sim::World::LoggedCommand> cmds = sim::World::decode(in.rest());
        if (cmds.size() != 1) throw std::runtime_error("network: a command message without one command");
        const sim::CommandResult r = world_.execute(cmds.front().cmd, s.seat);
        send_step(); // the command (if played) reaches everyone before the answer
        Out d;
        d.u(seq, 4).u(r.ok ? 1 : 0, 1).u(r.ok ? world_.command_log().size() - 1 : 0, 8).text(r.error);
        s.conn.send(kDone, d.str());
        return;
    }
    default: return; // nothing else comes from a guest
    }
}

void Host::poll() {
    while (auto c = listener_.accept()) seats_.push_back(Seat{std::move(*c), 0, {}, false});
    for (Seat& s : seats_) {
        try {
            while (auto m = s.conn.receive(0)) handle(s, *m);
        } catch (const std::exception&) {
            s.conn.close(); // garbled: let them go
        }
    }
    send_step();
    for (Seat& s : seats_) s.conn.flush(0);
    // Players gone: their seat is free again (their company stays).
    for (auto it = seats_.begin(); it != seats_.end();) {
        if (it->conn.open()) {
            ++it;
            continue;
        }
        const bool joined = it->joined;
        const std::string name = it->name;
        it = seats_.erase(it);
        if (joined) {
            send_players();
            say(name + " left the game");
        }
    }
}

void Host::after_tick() {
    if (world_.total_ticks() % kCheckTicks != 0) return;
    if (std::none_of(seats_.begin(), seats_.end(), [](const Seat& s) { return s.joined; })) return;
    // Sent before the step that lets the guests run to this tick (poll
    // sends that), so each has it when it gets there.
    Out m;
    m.u(world_.total_ticks(), 8).u(world_.checksum(), 8);
    for (Seat& s : seats_)
        if (s.joined) s.conn.send(kCheck, m.str());
}

std::vector<std::pair<sim::PlayerId, std::string>> Host::players() const {
    std::vector<std::pair<sim::PlayerId, std::string>> out{{0, name_}};
    for (const Seat& s : seats_)
        if (s.joined) out.emplace_back(s.seat, s.name);
    return out;
}

std::vector<std::string> Host::take_news() { return std::exchange(news_, {}); }

// ---- Guest ----

Guest::Guest(sim::World& world, const std::string& address, const std::string& name, sim::GameData data, int timeout_ms)
    : world_(world) {
    const auto [host, port] = split_address(address);
    std::string error;
    conn_ = Connection::connect(host, port, std::min(timeout_ms, 5000), error);
    if (!conn_.open()) throw std::runtime_error(error);
    Out hello;
    hello.u(kProtocol, 4).text(name);
    conn_.send(kHello, hello.str());
    conn_.flush(1000);
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (waited >= timeout_ms) throw std::runtime_error("the host did not answer");
        const auto m = conn_.receive(static_cast<int>(timeout_ms - waited));
        if (!m) {
            if (!conn_.open()) throw std::runtime_error("the host closed the connection");
            continue;
        }
        if (m->type == kRefuse) throw std::runtime_error(m->body);
        if (m->type != kWelcome) continue;
        In in(m->body);
        const auto seat = static_cast<sim::PlayerId>(in.u(2));
        world_ = sim::World::load(in.rest(), std::move(data));
        world_.set_local_player(seat);
        granted_ = world_.total_ticks();
        world_.set_remote([this](const sim::Command& cmd) { return send_command(cmd); });
        return;
    }
}

Guest::~Guest() {
    world_.set_remote(nullptr); // this copy plays on alone
}

void Guest::lost() {
    if (!world_.has_remote()) return;
    world_.set_remote(nullptr);
    queue_.clear();
    news_.push_back("The host has left: the game carries on here, on your own");
}

void Guest::handle(const Message& m) {
    In in(m.body);
    switch (m.type) {
    case kStep: {
        granted_ = std::max(granted_, in.u(8));
        pace_ = static_cast<std::uint8_t>(in.u(1));
        for (auto& c : sim::World::decode(in.rest())) queue_.push_back(std::move(c));
        return;
    }
    case kDone: {
        Done d;
        d.seq = static_cast<std::uint32_t>(in.u(4));
        d.ok = in.u(1) != 0;
        d.index = in.u(8);
        d.error = in.text();
        done_ = d;
        return;
    }
    case kCheck: {
        const std::uint64_t tick = in.u(8);
        checks_[tick] = in.u(8);
        verify();
        return;
    }
    case kNote: news_.push_back(m.body); return;
    case kPlayers: {
        players_.assign(static_cast<std::size_t>(in.u(2)), {});
        for (auto& [seat, name] : players_) {
            seat = static_cast<sim::PlayerId>(in.u(2));
            name = in.text();
        }
        return;
    }
    default: return;
    }
}

void Guest::poll() {
    if (!world_.has_remote()) return;
    try {
        while (auto m = conn_.receive(0)) handle(*m);
    } catch (const std::exception& e) {
        drift_ = std::string("a garbled message from the host: ") + e.what();
        conn_.close();
    }
    conn_.flush(0);
    if (!conn_.open()) lost();
}

void Guest::advance(int max_ticks) {
    for (int n = 0;;) {
        // Commands given at this tick, played before it runs on (as the host did).
        while (!queue_.empty() && queue_.front().tick <= world_.total_ticks()) {
            const sim::World::LoggedCommand c = queue_.front();
            queue_.pop_front();
            if (c.tick < world_.total_ticks() && !drift_) drift_ = "a command came for a tick already past";
            const sim::CommandResult r = world_.play_logged(c);
            if (!r.ok && !drift_) drift_ = "a command the host played was refused here: " + r.error;
            if (r.ok && c.who == world_.local_player()) own_results_[world_.command_log().size() - 1] = r;
        }
        if (world_.total_ticks() >= granted_ || n >= max_ticks) break;
        world_.tick();
        ++n;
        verify();
    }
}

void Guest::verify() {
    // The host's checksum for the tick this copy is at, if it sent one (it
    // sends them ahead, so none should be left behind).
    for (auto it = checks_.begin(); it != checks_.end() && it->first <= world_.total_ticks();) {
        if (it->first < world_.total_ticks()) {
            if (!drift_) drift_ = "a checksum came for a tick already past";
        } else if (it->second != world_.checksum()) {
            if (!drift_) {
                drift_ = "this copy of the game no longer matches the host's (day " +
                         std::to_string(world_.total_ticks() / sim::World::kTicksPerDay) + ")";
            }
        } else {
            ++verified_;
        }
        it = checks_.erase(it);
    }
}

std::uint64_t Guest::behind() const { return granted_ > world_.total_ticks() ? granted_ - world_.total_ticks() : 0; }

std::vector<std::string> Guest::take_news() { return std::exchange(news_, {}); }

sim::CommandResult Guest::send_command(const sim::Command& cmd) {
    const std::uint32_t seq = ++seq_;
    Out m;
    m.u(seq, 4).rest(sim::World::encode({sim::World::LoggedCommand{0, world_.local_player(), cmd}}));
    conn_.send(kCommand, m.str());
    conn_.flush(1000);
    // Wait for the host to play it (and this copy too, on the way).
    const auto start = std::chrono::steady_clock::now();
    while (!(done_ && done_->seq == seq)) {
        if (!conn_.open()) {
            lost();
            sim::CommandResult r;
            r.error = "the connection to the host was lost";
            return r;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            sim::CommandResult r;
            r.error = "no answer from the host";
            return r;
        }
        try {
            if (auto msg = conn_.receive(20)) handle(*msg);
        } catch (const std::exception& e) {
            drift_ = std::string("a garbled message from the host: ") + e.what();
            conn_.close();
        }
        advance(1 << 30);
    }
    const Done d = *done_;
    done_.reset();
    sim::CommandResult r;
    if (!d.ok) {
        r.error = d.error;
        return r;
    }
    advance(1 << 30); // through to the command, if not there yet
    const auto it = own_results_.find(static_cast<std::size_t>(d.index));
    if (it == own_results_.end()) {
        r.ok = true; // played, but its details were lost (drift)
        return r;
    }
    r = it->second;
    own_results_.erase(own_results_.begin(), std::next(it));
    return r;
}

} // namespace railmaster::net
