#include "railmaster/net/session.hpp"
#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace railmaster;
using namespace railmaster::sim;

namespace {

std::string read_data(const std::string& name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData shipped() {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    return d;
}

World small_world(std::int32_t humans) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 96;
    cfg.seed = 9;
    cfg.rivals = 1;
    cfg.humans = humans;
    return World(cfg, shipped());
}

// The host's side, on a thread of its own as on its own computer: its game
// runs a tick at a time, letting guests in and playing their commands.
class HostThread {
public:
    HostThread(World& w, std::uint64_t until) : world_(w), host_(w, 0) {
        thread_ = std::thread([this, until] {
            while (!stop_) {
                std::lock_guard lock(mutex_);
                host_.poll();
                // The clock starts once a guest has joined, as the host
                // would start it once everyone is in.
                if (world_.total_ticks() < until && host_.players().size() > 1) {
                    world_.tick();
                    host_.after_tick();
                }
            }
            std::lock_guard lock(mutex_);
            host_.poll();
        });
    }
    ~HostThread() { stop(); }
    void stop() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }
    std::uint16_t port() const { return host_.port(); }
    template <class F>
    auto with(F&& f) {
        std::lock_guard lock(mutex_);
        return f(world_, host_);
    }

private:
    World& world_;
    net::Host host_;
    std::mutex mutex_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace

TEST_CASE("addresses with and without a port") {
    CHECK(net::split_address("192.168.1.20") == std::pair<std::string, std::uint16_t>{"192.168.1.20", net::kDefaultPort});
    CHECK(net::split_address("192.168.1.20:4000") == std::pair<std::string, std::uint16_t>{"192.168.1.20", 4000});
    CHECK(net::split_address("myhost:x").second == net::kDefaultPort);
    CHECK(net::split_address("::1").first == "::1");
}

TEST_CASE("a guest joins over the network, plays from its seat, and both copies stay alike (multiplayer)") {
    World host_world = small_world(2);
    const std::uint64_t until = 90 * World::kTicksPerDay; // past the first two monthly checksums
    HostThread host(host_world, until);

    World guest_world = small_world(1); // replaced by the host's game on joining
    net::Guest guest(guest_world, "127.0.0.1:" + std::to_string(host.port()), "Ann", shipped());
    CHECK(guest.seat() == 1);
    CHECK(guest_world.local_player() == 1);
    CHECK_FALSE(guest_world.player_company().has_value());

    // The guest's commands go to the host and come back with their results.
    const CommandResult founded =
        guest_world.execute(FoundCompany{"Ann's Line", Money::dollars(1'000'000), Money::dollars(1'000'000)});
    REQUIRE(founded.ok);
    REQUIRE(guest_world.player_company().has_value());
    CHECK(guest_world.company().name() == "Ann's Line");
    // Players go by their names (the others see them in the news).
    REQUIRE(guest_world.execute(RenamePlayer{"  Ann "}).ok);
    CHECK(guest_world.investor().name == "Ann");
    CHECK_FALSE(guest_world.execute(RenamePlayer{"You"}).ok); // the host's (player 1's) name
    CHECK_FALSE(guest_world.execute(RenamePlayer{""}).ok);
    const CommandResult bond = guest_world.execute(IssueBond{});
    CHECK(bond.ok);
    const CommandResult refused = guest_world.execute(SetPriority{.train = 999, .priority = 2});
    CHECK_FALSE(refused.ok);
    CHECK_FALSE(refused.error.empty());

    // Play on until the host's game has run its course, then catch up.
    while (host.with([&](World& w, net::Host&) { return w.total_ticks(); }) < until) {
        guest.poll();
        guest.advance(64);
    }
    host.stop();
    for (int i = 0; i < 200 && (guest.behind() > 0 || guest_world.total_ticks() < until); ++i) {
        guest.poll();
        guest.advance(1 << 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(guest_world.total_ticks() == host_world.total_ticks());
    CHECK_FALSE(guest.drift().has_value());
    CHECK(guest.verified() == 3); // days 30, 60 and 90
    CHECK(guest_world.checksum() == host_world.checksum());
    CHECK(guest_world.save() == host_world.save());
    // On the host's copy the guest's company is the guest's.
    REQUIRE(host_world.player_company(1).has_value());
    CHECK(host_world.company_of(1).name() == "Ann's Line");
    CHECK(host_world.company().name() != "Ann's Line");
    CHECK(host_world.command_log().back().who == 1);
    CHECK(host_world.investor(1).name == "Ann");
}

TEST_CASE("a game with no free seat turns a guest away, saying why") {
    World host_world = small_world(1);
    HostThread host(host_world, 0);
    World guest_world = small_world(1);
    std::string why;
    try {
        net::Guest guest(guest_world, "127.0.0.1:" + std::to_string(host.port()), "Bob", shipped());
    } catch (const std::runtime_error& e) {
        why = e.what();
    }
    CHECK(why.find("no seats") != std::string::npos);
}

TEST_CASE("joining where nobody hosts fails with a reason") {
    World w = small_world(1);
    // A port just opened and closed again: nobody is listening there.
    std::uint16_t port = 0;
    {
        std::string error;
        port = net::Listener::listen(0, error).port();
    }
    std::string why;
    try {
        net::Guest guest(w, "127.0.0.1:" + std::to_string(port), "Cy", shipped(), 2000);
    } catch (const std::runtime_error& e) {
        why = e.what();
    }
    CHECK_FALSE(why.empty());
}

TEST_CASE("when the host goes, the guest's game carries on alone") {
    World host_world = small_world(2);
    World guest_world = small_world(1);
    auto host = std::make_unique<HostThread>(host_world, 20 * World::kTicksPerDay);
    net::Guest guest(guest_world, "127.0.0.1:" + std::to_string(host->port()), "Di", shipped());
    CHECK(guest_world.has_remote());
    host.reset(); // the host quits
    for (int i = 0; i < 100 && guest_world.has_remote(); ++i) {
        guest.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_FALSE(guest_world.has_remote());
    const auto news = guest.take_news();
    REQUIRE_FALSE(news.empty());
    CHECK(news.back().find("host has left") != std::string::npos);
    // Commands now run here.
    CHECK(guest_world.execute(FoundCompany{"Alone", Money::dollars(1'000'000), Money::dollars(1'000'000)}).ok);
}

TEST_CASE("a guest's copy changed behind the host's back is found out by the monthly checksum") {
    World host_world = small_world(2);
    const std::uint64_t until = 40 * World::kTicksPerDay;
    HostThread host(host_world, until);
    World guest_world = small_world(1);
    net::Guest guest(guest_world, "127.0.0.1:" + std::to_string(host.port()), "Ed", shipped());
    // A command played here only, as a bug might: the copies part ways.
    REQUIRE(guest_world.play_logged({guest_world.total_ticks(), 1,
                                     FoundCompany{"Rogue", Money::dollars(1'000'000), Money::dollars(1'000'000)}})
                .ok);
    for (int i = 0; i < 2000 && guest_world.total_ticks() < until; ++i) {
        guest.poll();
        guest.advance(1 << 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(guest_world.total_ticks() >= 30 * World::kTicksPerDay);
    REQUIRE(guest.drift().has_value());
    CHECK(guest.drift()->find("no longer matches") != std::string::npos);
}

TEST_CASE("a guest builds a whole railway through the host, each step using what the last one made") {
    World host_world = small_world(2);
    const std::uint64_t until = 45 * World::kTicksPerDay;
    HostThread host(host_world, until);
    World guest_world = small_world(1);
    net::Guest guest(guest_world, "127.0.0.1:" + std::to_string(host.port()), "Flo", shipped());
    REQUIRE(guest_world.execute(FoundCompany{"Flo's Line", Money::dollars(1'000'000), Money::dollars(1'000'000)}).ok);
    // Track, stations on its new ends, trains on routes between them: every
    // command names what an earlier one created (CommandResult::created_id).
    (void)build_demo_network(guest_world);
    const auto mine = [](const World& w) {
        std::size_t n = 0;
        for (const Train& t : w.railway().trains()) if (t.owner == *w.player_company(1)) ++n;
        return n;
    };
    CHECK(mine(guest_world) > 0);
    while (host.with([&](World& w, net::Host&) { return w.total_ticks(); }) < until) {
        guest.poll();
        guest.advance(64);
    }
    host.stop();
    for (int i = 0; i < 200 && guest_world.total_ticks() < until; ++i) {
        guest.poll();
        guest.advance(1 << 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK_FALSE(guest.drift().has_value());
    CHECK(guest.verified() >= 1);
    CHECK(mine(host_world) == mine(guest_world));
    CHECK(guest_world.save() == host_world.save());
}

TEST_CASE("players chat: each line reaches everyone, with who said it") {
    World host_world = small_world(2);
    REQUIRE(host_world.execute(RenamePlayer{"Bo"}).ok);
    HostThread host(host_world, 0);
    World guest_world = small_world(1);
    net::Guest guest(guest_world, "127.0.0.1:" + std::to_string(host.port()), "Gil", shipped());
    REQUIRE(guest_world.execute(RenamePlayer{"Gil"}).ok);
    guest.chat("  hello\tthere ");
    host.with([](World&, net::Host& h) {
        h.chat("welcome");
        return 0;
    });
    std::vector<std::string> heard;
    for (int i = 0; i < 400 && heard.size() < 2; ++i) {
        guest.poll();
        for (auto& l : guest.take_chat()) heard.push_back(l);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(heard.size() == 2);
    // The host passes lines on in the order it has them (its own may come first).
    CHECK(std::find(heard.begin(), heard.end(), "Gil: hello there") != heard.end());
    CHECK(std::find(heard.begin(), heard.end(), "Bo: welcome") != heard.end());
    host.stop();
    const auto host_heard = host.with([](World&, net::Host& h) { return h.take_chat(); });
    CHECK(host_heard.size() == 2);
    // Who is playing, as the guest heard it.
    REQUIRE(guest.players().size() == 2);
    CHECK(guest.players()[1].first == 1);
}

TEST_CASE("a hosted game saved and hosted again: each player joining under their name takes their railroad back") {
    // An evening's game: Gil (player 2) founded a railroad, then it was saved.
    std::string saved;
    {
        World first = small_world(3);
        REQUIRE(first.execute(RenamePlayer{"Gil"}, 1).ok);
        REQUIRE(first.execute(FoundCompany{"Gil's Line", Money::dollars(1'000'000), Money::dollars(1'000'000)}, 1).ok);
        saved = first.save();
    }
    // Another evening: the save hosted again.
    World host_world = World::load(saved, shipped());
    HostThread host(host_world, 0);
    World newcomer_world = small_world(1);
    net::Guest newcomer(newcomer_world, "127.0.0.1:" + std::to_string(host.port()), "Hal", shipped());
    CHECK(newcomer.seat() == 2); // Gil's seat is kept for Gil
    World gil_world = small_world(1);
    net::Guest gil(gil_world, "127.0.0.1:" + std::to_string(host.port()), "Gil", shipped());
    CHECK(gil.seat() == 1);
    REQUIRE(gil_world.player_company().has_value());
    CHECK(gil_world.company().name() == "Gil's Line");
}
