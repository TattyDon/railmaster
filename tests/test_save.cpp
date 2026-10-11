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
    cfg.seed = 7;
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
    CHECK(error(saved.substr(0, saved.size() / 2), shipped()).find("ends early") != std::string::npos);
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
