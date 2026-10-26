#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

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

// A small busy game: the demo network, two rivals, territories.
World busy_world() {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 96;
    cfg.seed = 9;
    cfg.rivals = 2;
    cfg.territories = 3;
    World w(cfg, shipped());
    (void)build_demo_network(w);
    return w;
}

void run_days(World& w, int days) {
    for (int i = 0; i < days * World::kTicksPerDay; ++i) w.tick();
}

} // namespace

TEST_CASE("a saved game loads as the same game, and plays on exactly as the original") {
    World original = busy_world();
    run_days(original, 400); // into a second year: accounts, rivals' lines, trains under way
    const std::string saved = original.save();
    World loaded = World::load(saved, shipped());
    CHECK(loaded.save() == saved); // nothing lost or changed

    CHECK(loaded.date() == original.date());
    CHECK(loaded.company().cash() == original.company().cash());
    CHECK(loaded.railway().trains().size() == original.railway().trains().size());
    CHECK(loaded.economy().sites().size() == original.economy().sites().size());
    CHECK(loaded.rivals().size() == original.rivals().size());

    // Determinism carries across the save: both run on to the same state.
    run_days(original, 200);
    run_days(loaded, 200);
    CHECK(loaded.save() == original.save());
    CHECK(loaded.company().cash() == original.company().cash());
}

TEST_CASE("a scenario's goals and medal survive saving") {
    const Scenario s = Scenario::from_json(read_data("scenarios/lowland_junction.json"));
    WorldConfig cfg = scenario_config(s);
    cfg.width_tiles = cfg.height_tiles = 256;
    World w(cfg, shipped());
    run_days(w, 40);
    World loaded = World::load(w.save(), shipped());
    REQUIRE(loaded.scenario().has_value());
    CHECK(loaded.scenario()->name == "Lowland Junction");
    CHECK(loaded.scenario()->goals_for(Medal::Gold).size() == 5);
    CHECK(loaded.scenario_result().medal == w.scenario_result().medal);
}

TEST_CASE("the save keeps the balance it was played with") {
    GameData d = shipped();
    d.balance.track.remove_refund_percent = 33;
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 64;
    World w(cfg, std::move(d));
    const World loaded = World::load(w.save(), shipped()); // shipped balance says 10
    CHECK(loaded.data().balance.track.remove_refund_percent == 33);
}

TEST_CASE("bad save files are refused with a reason") {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 64;
    const World w(cfg, shipped());
    const std::string saved = w.save();
    const auto error = [](std::string_view bytes, GameData d) {
        try {
            (void)World::load(bytes, std::move(d));
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    CHECK(error("not a save at all", shipped()).find("not a Railmaster save") != std::string::npos);
    const std::string cut = error(saved.substr(0, saved.size() / 2), shipped());
    CHECK((cut.find("ends early") != std::string::npos || cut.find("damaged") != std::string::npos));
    CHECK(error(saved + "x", shipped()).find("unexpected data") != std::string::npos);
    std::string newer = saved;
    newer[8] = 99; // the version number
    CHECK(error(newer, shipped()).find("version 99") != std::string::npos);
    GameData other = shipped();
    other.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "x", "name": "X", "fuel": "steam", "available_from": 1800, "top_speed_mph": 30, "cost": 1,
         "maintenance_per_year": 1}]})");
    CHECK(error(saved, other).find("different game data") != std::string::npos);
}

TEST_CASE("a save replays from its command log to the same game, byte for byte (§16)") {
    World w = busy_world(); // the demo network is built with logged commands
    run_days(w, 40);
    REQUIRE(w.execute(IssueBond{}).ok);
    const auto& stations = w.railway().stations();
    REQUIRE(stations.size() >= 2);
    REQUIRE(w.execute(CopyTrain{.train = 0}).ok);
    run_days(w, 70);
    REQUIRE(w.execute(SetPriority{.train = 0, .priority = 3}).ok);
    run_days(w, 20);

    // Only commands from outside the simulation are logged, not the rivals'.
    const auto& log = w.command_log();
    REQUIRE(log.size() > 5);
    for (const auto& c : log) CHECK(c.who == kHumanPlayer);
    CHECK(std::holds_alternative<SetPriority>(log.back().cmd));

    const std::string saved = w.save();
    const World again = World::replay(saved, shipped());
    CHECK(again.total_ticks() == w.total_ticks());
    CHECK(again.save() == saved);

    // A loaded game keeps its log and config, and carries on logging.
    World loaded = World::load(saved, shipped());
    CHECK(loaded.command_log().size() == log.size());
    REQUIRE(loaded.execute(SetPriority{.train = 0, .priority = 1}).ok);
    run_days(loaded, 5);
    CHECK(World::replay(loaded.save(), shipped()).save() == loaded.save());
}

TEST_CASE("a replay watched as it plays ends as the save did, and refuses the watcher's commands") {
    World w = busy_world();
    run_days(w, 30);
    REQUIRE(w.execute(IssueBond{}).ok);
    run_days(w, 30);
    REQUIRE(w.execute(SetPriority{.train = 0, .priority = 3}).ok);
    run_days(w, 10);
    const std::string saved = w.save();
    const World original = World::load(saved, shipped());

    GameData d = shipped();
    d.balance = original.data().balance;
    World watched(original.config(), std::move(d));
    watched.set_watch_only(true);
    std::size_t next = 0;
    const auto& log = original.command_log();
    while (watched.total_ticks() < original.total_ticks() || next < log.size()) {
        while (next < log.size() && log[next].tick <= watched.total_ticks()) REQUIRE(watched.play_logged(log[next++]).ok);
        if (watched.total_ticks() < original.total_ticks()) watched.tick();
    }
    // The watcher may not play the game; taking it over lets them.
    const CommandResult refused = watched.execute(SetPriority{.train = 0, .priority = 1});
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.find("replay") != std::string::npos);
    CHECK(watched.save() == saved);
    watched.set_watch_only(false);
    CHECK(watched.execute(SetPriority{.train = 0, .priority = 1}).ok);
}

TEST_CASE("saves are compressed") {
    const World w = busy_world();
    const std::string saved = w.save();
    CHECK(saved.substr(0, 7) == "RMSAVEZ");
    MESSAGE("a 96 x 96 game saves to " << saved.size() / 1024 << " KB");
    CHECK(saved.size() < 1'000'000);
}
