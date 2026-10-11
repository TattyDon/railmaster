#include "legacy_scale.hpp"
#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

GameData test_data() {
    return {CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})"),
            LocomotiveRegistry::from_json(R"({"locomotives": [
                {"key": "loco", "name": "Loco", "fuel": "diesel", "available_from": 1800,
                 "top_speed_mph": 40, "cost": 10000, "maintenance_per_year": 1000}]})")};
}

// A flat, dry 30 x 30 km world.
World flat_world() {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = cfg.height_tiles = 30;
    cfg.start_date = Date::from_ymd(1835, 1, 1);
    cfg.business_cycle = false;
    World w(cfg, test_data());
    w.railway().set_rules({.breakdowns = false});
    for (int y = 0; y <= 30; ++y)
        for (int x = 0; x <= 30; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

TrackEnd free_at(std::int64_t x_km, std::int64_t y_km) { return {TrackEnd::Kind::Free, 0, 0, {x_km * kKm, y_km * kKm}}; }

TrackEnd node_at(const World& w, std::int64_t x_km, std::int64_t y_km) {
    const TrackNetwork& net = w.railway().track();
    const NodeId n = *net.nearest_node({x_km * kKm, y_km * kKm}, 10'000);
    return {TrackEnd::Kind::Node, n, 0, net.node(n).pos};
}

Money edge_costs(const World& w) {
    Money sum;
    for (const TrackEdge& e : w.railway().track().edges())
        if (!e.removed) sum += e.cost;
    return sum;
}

std::vector<EdgeId> edges_of(const World& w) {
    std::vector<EdgeId> out;
    for (const TrackEdge& e : w.railway().track().edges())
        if (!e.removed) out.push_back(e.id);
    return out;
}

} // namespace

TEST_CASE("every piece of track carries its cost, through splits and wiring") {
    World w = flat_world();
    const CommandResult line = w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(15, 10)});
    REQUIRE(line.ok);
    CHECK(edge_costs(w) == line.cost);
    CHECK(w.company().track_value() == line.cost);
    // A branch from the middle of a piece splits it; the halves share its cost.
    const TrackNetwork& net = w.railway().track();
    const EdgePoint mid = *net.nearest_edge_point({10 * kKm + 250'000, 10 * kKm}, 10'000);
    const CommandResult branch = w.execute(
        BuildTrack{.start = {TrackEnd::Kind::OnEdge, 0, mid.edge, mid.pos}, .end = free_at(10, 15)});
    REQUIRE(branch.ok);
    CHECK(edge_costs(w) == w.company().track_value());
    REQUIRE(w.execute(ElectrifyTrack{}).ok);
    CHECK(edge_costs(w) == w.company().track_value());
}

TEST_CASE("bulldozing your track returns 10% and writes off the rest (§11.2 [D/I])") {
    World w = flat_world();
    const CommandResult line = w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(15, 10)});
    REQUIRE(line.ok);
    const Money cash = w.company().cash();
    const std::vector<EdgeId> run = w.railway().run_of(edges_of(w).front());
    CHECK(run == edges_of(w)); // no junctions or stations: the whole line
    const CommandResult r = w.execute(RemoveTrack{.edges = run});
    REQUIRE(r.ok);
    CHECK(w.company().cash() == cash + line.cost.scaled(10, 100));
    CHECK(w.company().track_value() == Money{});
    CHECK(edges_of(w).empty());
    CHECK_FALSE(w.railway().track().nearest_edge_point({10 * kKm, 10 * kKm}, kKm).has_value());
    CHECK(w.execute(RemoveTrack{.edges = run}).error == "no such track");
    CHECK(w.execute(RemoveTrack{}).error == "no track chosen");
}

TEST_CASE("a run stops at stations and junctions; others' track and occupied track stay") {
    World w = flat_world();
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(15, 10)}).ok);
    REQUIRE(w.execute(BuildTrack{.start = node_at(w, 15, 10), .end = free_at(25, 10)}).ok);
    REQUIRE(w.execute(BuildStation{.at = node_at(w, 15, 10)}).ok);
    const TrackNetwork& net = w.railway().track();
    const EdgeId west = net.nearest_edge_point({8 * kKm, 10 * kKm}, 10'000)->edge;
    const EdgeId east = net.nearest_edge_point({20 * kKm, 10 * kKm}, 10'000)->edge;
    const std::vector<EdgeId> run = w.railway().run_of(west);
    CHECK(std::find(run.begin(), run.end(), east) == run.end()); // the station ends the run
    CHECK(run.size() + w.railway().run_of(east).size() == edges_of(w).size());

    const PlayerId rival = w.add_player_company("Rival Line", "Someone");
    CHECK(w.execute(RemoveTrack{.edges = {west}}, rival).error == "you can only remove your own track");

    REQUIRE(w.execute(BuildStation{.at = node_at(w, 25, 10)}).ok);
    const CommandResult t = w.execute(BuyTrain{.loco = 0, .cars = 0, .route = {0, 1}});
    REQUIRE(t.ok);
    for (int i = 0; i < 40 && w.railway().train(t.created_id).state != TrainState::Moving; ++i) w.tick();
    const EdgeId under = w.railway().train(t.created_id).path[w.railway().train(t.created_id).step].edge;
    CHECK(w.execute(RemoveTrack{.edges = {under}}).error == "a train is on that track");
}

TEST_CASE("a train whose way ahead is bulldozed finds another") {
    World w = flat_world();
    // A straight line from A to B, and a longer detour through (15, 18).
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(25, 10)}).ok);
    REQUIRE(w.execute(BuildTrack{.start = node_at(w, 5, 10), .end = free_at(15, 18)}).ok);
    REQUIRE(w.execute(BuildTrack{.start = node_at(w, 15, 18), .end = node_at(w, 25, 10)}).ok);
    REQUIRE(w.execute(BuildStation{.at = node_at(w, 5, 10)}).ok);
    REQUIRE(w.execute(BuildStation{.at = node_at(w, 25, 10)}).ok);
    const CommandResult bought = w.execute(BuyTrain{.loco = 0, .cars = 0, .route = {0, 1}});
    REQUIRE(bought.ok);
    const TrainId id = bought.created_id;
    for (int i = 0; i < 200 && w.railway().train(id).state != TrainState::Moving; ++i) w.tick();
    REQUIRE(w.railway().train(id).state == TrainState::Moving);
    // It takes the straight line; take up a piece of it well ahead.
    const EdgeId ahead = w.railway().track().nearest_edge_point({20 * kKm, 10 * kKm}, 10'000)->edge;
    const auto& path = w.railway().train(id).path;
    REQUIRE(std::any_of(path.begin(), path.end(), [&](const PathStep& s) { return s.edge == ahead; }));
    REQUIRE(w.execute(RemoveTrack{.edges = {ahead}}).ok);
    CHECK(w.railway().train(id).replan);
    const std::uint32_t stops = w.railway().train(id).stops_made;
    for (int i = 0; i < 20'000 && w.railway().train(id).stops_made == stops; ++i) {
        w.tick();
        REQUIRE(w.railway().train(id).state != TrainState::NoRoute);
    }
    CHECK(w.railway().train(id).stops_made == stops + 1);
    CHECK(w.railway().train(id).at_node == w.railway().station(1).node);
}

TEST_CASE("undo takes back track at full price until the Track tool closes (§11.1 [D])") {
    World w = flat_world();
    const Money cash = w.company().cash();
    const CommandResult a = w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(15, 10)});
    const CommandResult b = w.execute(BuildTrack{.start = node_at(w, 15, 10), .end = free_at(20, 15)});
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(w.undoable_track() == 2);
    const CommandResult u1 = w.execute(UndoTrack{});
    REQUIRE(u1.ok);
    CHECK(u1.cost == Money{} - b.cost); // a refund
    CHECK(w.company().cash() == cash - a.cost);
    REQUIRE(w.execute(UndoTrack{}).ok);
    CHECK(w.company().cash() == cash);
    CHECK(w.company().track_value() == Money{});
    CHECK(w.company().history().back().track_built == Money{});
    CHECK(edges_of(w).empty());
    CHECK(w.execute(UndoTrack{}).error == "nothing to undo");

    // Closing the tool ends it.
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 20), .end = free_at(15, 20)}).ok);
    REQUIRE(w.execute(EndTrackUndo{}).ok);
    CHECK(w.execute(UndoTrack{}).error == "nothing to undo");
}

TEST_CASE("undo is refused once a station stands on the new track or a train is on it") {
    World w = flat_world();
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(15, 10)}).ok);
    REQUIRE(w.execute(BuildStation{.at = node_at(w, 15, 10)}).ok);
    CHECK(w.execute(UndoTrack{}).error == "a station stands on that track");
    // Undo is per player: a rival has nothing to undo.
    const PlayerId rival = w.add_player_company("Rival Line", "Someone");
    CHECK(w.execute(UndoTrack{}, rival).error == "nothing to undo");
}

TEST_CASE("the demo network cannot be undone") {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 96;
    cfg.industries_appear = false;
    // Any world with towns will do; the shipped data has them.
    GameData d;
    const auto read = [](const char* name) {
        std::ifstream f(std::string(RAILMASTER_DATA_DIR) + "/" + name);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    d.balance = Balance::from_json(read("balance.json"));
    d.cargo = CargoRegistry::from_json(read("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read("industries.json"), d.cargo);
    World w(cfg, std::move(d));
    (void)build_demo_network(w);
    CHECK(w.undoable_track() == 0);
    CHECK(w.execute(UndoTrack{}).error == "nothing to undo");
}
