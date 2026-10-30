#pragma once

// Multiplayer over a network: one player hosts, the others join. Every
// player runs their own copy of the game, and the copies stay alike because
// the simulation is deterministic and is given the same commands at the
// same ticks (lockstep).
//
// The host's copy leads. A guest joining is sent the host's game as a save
// and takes a seat (a human player: WorldConfig::humans). From then on the
// host sends, every frame, how far its game has run and the commands given
// since (its own and the guests'), and each guest's copy plays on that far.
// A guest's own commands go to the host, which plays them for everyone; the
// guest's copy waits for its command to come back, then answers with the
// result it got playing it itself (World::set_remote), so the game's tools
// work as in a game of one's own. Once a month of game time the host sends
// a checksum of its game, for the guests to find drift (desync).

#include "railmaster/net/socket.hpp"
#include "railmaster/sim/world.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace railmaster::net {

constexpr std::uint16_t kDefaultPort = 27630;

class Host {
public:
    // Open `world` (made with WorldConfig::humans above 1, for seats) to
    // guests on `port` (0: any free port). Throws std::runtime_error.
    Host(sim::World& world, std::uint16_t port = kDefaultPort);
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    std::uint16_t port() const { return listener_.port(); }
    // Each frame, after the game has ticked: let guests in, play their
    // commands, and send everyone what has happened.
    void poll();
    // After each tick of the game: the month's checksum, when one is due.
    void after_tick();
    // Who is playing: (seat, name), the host first.
    std::vector<std::pair<sim::PlayerId, std::string>> players() const;
    // News for the host's screen ("Ann joined as player 2").
    std::vector<std::string> take_news();
    void set_name(std::string name) { name_ = std::move(name); }
    // The host's game speed (sim::GameSpeed, 0 paused), shown to the guests.
    void set_pace(std::uint8_t pace) { pace_ = pace; }
    // Say something to everyone (chat); take_chat() has everyone's lines,
    // "Ann: hello", the host's own included.
    void chat(const std::string& text);
    std::vector<std::string> take_chat();

private:
    struct Seat {
        Connection conn;
        sim::PlayerId seat = 0;
        std::string name;
        bool joined = false;
    };
    void handle(Seat& s, const Message& m);
    void send_step();
    void say(const std::string& text); // to the host's screen and every guest
    void send_players();               // everyone's names, to every guest
    sim::World& world_;
    Listener listener_;
    std::vector<Seat> seats_;
    std::size_t sent_ = 0;         // commands of the log sent to the guests
    std::uint64_t sent_tick_ = 0;  // how far they have been told to run
    std::uint8_t pace_ = 0, sent_pace_ = 0xFF;
    std::string name_ = "Host";
    std::vector<std::string> news_;
    std::vector<std::string> chat_;
    void broadcast_chat(const std::string& line);
};

class Guest {
public:
    // Join the game hosted at `address` (a name or address, with ":port" or
    // not): connect, take a seat and replace `world` with the host's game,
    // played from that seat. `data` is this copy's game data (as for
    // World::load). Waits up to `timeout_ms`. Throws std::runtime_error with
    // the reason when it cannot join.
    Guest(sim::World& world, const std::string& address, const std::string& name, sim::GameData data,
          int timeout_ms = 8000);
    ~Guest();
    Guest(const Guest&) = delete;
    Guest& operator=(const Guest&) = delete;

    // Each frame: take in what the host has sent.
    void poll();
    // Play the game on as far as the host has, at most `max_ticks` ticks.
    void advance(int max_ticks);
    // How many ticks this copy is behind the host's last word.
    std::uint64_t behind() const;
    bool connected() const { return conn_.open(); }
    sim::PlayerId seat() const { return world_.local_player(); }
    // News for the guest's screen: who joined or left, the host gone.
    std::vector<std::string> take_news();
    // Who is playing, as the host last said: (seat, name), the host first.
    const std::vector<std::pair<sim::PlayerId, std::string>>& players() const { return players_; }
    // The host's game speed (sim::GameSpeed, 0 paused).
    std::uint8_t host_pace() const { return pace_; }
    // Say something to everyone (chat); take_chat() has everyone's lines as
    // the host passes them on, "Ann: hello", this player's own included.
    void chat(const std::string& text);
    std::vector<std::string> take_chat();
    // Set when this copy has drifted from the host's (a bug): what was found.
    const std::optional<std::string>& drift() const { return drift_; }
    // How many of the host's checksums this copy has matched.
    int verified() const { return verified_; }

private:
    sim::CommandResult send_command(const sim::Command& cmd);
    void handle(const Message& m);
    void lost();   // the host has gone: the game carries on here alone
    void verify(); // compare the host's checksum for this tick, if there is one
    sim::World& world_;
    Connection conn_;
    std::deque<sim::World::LoggedCommand> queue_; // to play as their ticks come
    std::uint64_t granted_ = 0;                   // the host's game has run this far
    std::uint8_t pace_ = 0;
    std::vector<std::pair<sim::PlayerId, std::string>> players_;
    std::map<std::uint64_t, std::uint64_t> checks_;
    std::map<std::size_t, sim::CommandResult> own_results_; // by place in the log
    struct Done {
        std::uint32_t seq = 0;
        bool ok = false;
        std::uint64_t index = 0;
        std::string error;
    };
    std::optional<Done> done_;
    std::uint32_t seq_ = 0;
    std::vector<std::string> news_;
    std::vector<std::string> chat_;
    std::optional<std::string> drift_;
    int verified_ = 0;
};

// "host" or "host:port" split, with the default port when none is given.
std::pair<std::string, std::uint16_t> split_address(const std::string& address);

} // namespace railmaster::net
