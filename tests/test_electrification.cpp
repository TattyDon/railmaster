#include "legacy_scale.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }
TrackEnd free_at(int cx, int cy) { return {TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; }

GameData data() {
    GameData d;
    d.cargo = CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})");
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "steam", "name": "Steam", "fuel": "steam", "available_from": 1800, "top_speed_mph": 40,
         "cost": 20000, "maintenance_per_year": 1000},
        {"key": "electric", "name": "Electric", "fuel": "electric", "available_from": 1800, "top_speed_mph": 80,
         "cost": 60000, "maintenance_per_year": 1000}]})");
    return d;
}

World flat_world() {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = 40;
    cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.industries_appear = false;
    cfg.business_cycle = false;
    World w(cfg, data());
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 40; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

TrackEnd node_at(const World& w, int cx, int cy) {
    const TrackNetwork& net = w.railway().track();
    const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
    return {TrackEnd::Kind::Node, n, 0, net.node(n).pos};
}

// A 20 km line with stations at both ends.
struct Line {
    World w = flat_world();
    StationId a = 0, b = 0;
    Line() {
        REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(25, 10)}).ok);
        a = w.execute(BuildStation{.at = node_at(w, 5, 10)}).created_id;
        b = w.execute(BuildStation{.at = node_at(w, 25, 10)}).created_id;
    }
    std::vector<EdgeId> own_edges() const {
        std::vector<EdgeId> out;
        for (const TrackEdge& e : w.railway().track().edges()) out.push_back(e.id);
        return out;
    }
};

} // namespace

TEST_CASE("electrifying costs 75% of laying single track on open ground [I]") {
    Line l;
    Money expected;
    for (const TrackEdge& e : l.w.railway().track().edges()) {
        CHECK_FALSE(e.electrified);
        expected += Money::dollars(25'000).scaled(e.length_mm * 75, kKm * 100);
    }
    const Money cash = l.w.company().cash();
    const Money track = l.w.company().track_value();
    const CommandResult r = l.w.execute(ElectrifyTrack{}); // all of it
    REQUIRE(r.ok);
    CHECK(r.cost == expected);
    CHECK(r.cost > Money::dollars(25'000 * 20 * 75 / 100 - 1'000)); // about 20 km
    CHECK(l.w.company().cash() == cash - expected);
    CHECK(l.w.company().track_value() == track + expected); // carried as track
    for (const TrackEdge& e : l.w.railway().track().edges()) CHECK(e.electrified);
    CHECK(l.w.execute(ElectrifyTrack{}).error.find("already") != std::string::npos);
    // Naming a piece already done costs nothing.
    CHECK(l.w.execute(ElectrifyTrack{.edges = {0, 0}}).cost == Money{});

    // Double track costs the double-track share more.
    Line d;
    for (EdgeId e : d.own_edges()) d.w.railway().track().set_double_track(e, true);
    CHECK(d.w.execute(ElectrifyTrack{}).cost == expected.scaled(170, 100));

    // Only your own track.
    Line r2;
    const PlayerId rival = r2.w.add_player_company("Rival", "R");
    CHECK(r2.w.execute(ElectrifyTrack{.edges = {0}}, rival).error.find("your own") != std::string::npos);
}

TEST_CASE("electric engines need electrified track between every pair of stops [D]") {
    Line l;
    const std::vector<StationId> route{l.a, l.b};
    CHECK(l.w.execute(BuyTrain{.loco = 1, .cars = 2, .route = route}).error.find("electrified") != std::string::npos);
    CHECK(l.w.execute(BuyTrain{.loco = 0, .cars = 2, .route = route}).ok); // steam runs anywhere
    // Half the line: still refused.
    const std::vector<EdgeId> edges = l.own_edges();
    REQUIRE(edges.size() >= 2);
    REQUIRE(l.w.execute(ElectrifyTrack{.edges = {edges.front()}}).ok);
    CHECK_FALSE(l.w.execute(BuyTrain{.loco = 1, .cars = 2, .route = route}).ok);
    REQUIRE(l.w.execute(ElectrifyTrack{}).ok);
    const CommandResult bought = l.w.execute(BuyTrain{.loco = 1, .cars = 2, .route = route});
    REQUIRE(bought.ok);
    for (int d = 0; d < 20; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) l.w.tick();
    CHECK(l.w.railway().train(bought.created_id).stops_made >= 2);
    CHECK(l.w.railway().train(bought.created_id).state != TrainState::NoRoute);

    // Re-engining an unwired route with an electric engine is refused too.
    Line s;
    const TrainId steam = s.w.execute(BuyTrain{.loco = 0, .cars = 2, .route = {s.a, s.b}}).created_id;
    CHECK(s.w.execute(ReplaceLocomotive{.train = steam, .loco = 1}).error.find("electrified") != std::string::npos);
    REQUIRE(s.w.execute(ElectrifyTrack{}).ok);
    CHECK(s.w.execute(ReplaceLocomotive{.train = steam, .loco = 1}).ok);
}

TEST_CASE("electric trains keep to the wires, even the long way round [D]") {
    // A to B directly (unwired), or round by C (wired).
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({10 * kKm, 0}, 0);
    const NodeId c = net.add_node({5 * kKm, 6 * kKm}, 0);
    const EdgeId direct = net.add_edge(a, b);
    const EdgeId ac = net.add_edge(a, c);
    const EdgeId cb = net.add_edge(c, b);
    net.set_electrified(ac, true);
    net.set_electrified(cb, true);
    const auto any = net.shortest_path(a, b);
    const auto wired = net.shortest_path(a, b, true);
    REQUIRE(any);
    REQUIRE(wired);
    CHECK(any->size() == 1);
    CHECK(any->front().edge == direct);
    CHECK(wired->size() == 2);
    net.set_electrified(cb, false);
    CHECK_FALSE(net.shortest_path(a, b, true));
    // Splitting a wired piece keeps both halves wired.
    net.set_electrified(cb, true);
    EdgeId second = 0;
    net.split_edge(cb, {7 * kKm, 3 * kKm}, &second);
    CHECK(net.edge(cb).electrified);
    CHECK(net.edge(second).electrified);
}

TEST_CASE("rivals buy only steam and diesel engines, as they do not electrify") {
    const auto read = [](const char* name) {
        std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    GameData d;
    d.balance = Balance::from_json(read("balance.json"));
    d.cargo = CargoRegistry::from_json(read("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read("tycoons.json"));
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = cfg.height_tiles = 80;
    cfg.start_date = Date::from_ymd(1960, 1, 1); // electric engines are the fastest on offer
    cfg.rivals = 2;
    World w(cfg, std::move(d));
    for (int day = 0; day < 365; ++day)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    std::int32_t trains = 0;
    for (const Train& t : w.railway().trains()) {
        if (t.owner == w.company().id()) continue;
        ++trains;
        CHECK(w.data().locomotives.get(t.loco).fuel != Fuel::Electric);
    }
    CHECK(trains > 0);
}
