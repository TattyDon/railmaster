#include "legacy_scale.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

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
    cfg.sandbox = true; // no breakdowns
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

// A 30 km line, west to east, with stations at both ends and in the middle.
struct Line {
    World w = flat_world();
    StationId west = 0, mid = 0, east = 0;
    Line() {
        REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(20, 10)}).ok);
        REQUIRE(w.execute(BuildTrack{.start = node_at(w, 20, 10), .end = free_at(35, 10)}).ok);
        west = w.execute(BuildStation{.at = node_at(w, 5, 10)}).created_id;
        mid = w.execute(BuildStation{.at = node_at(w, 20, 10)}).created_id;
        east = w.execute(BuildStation{.at = node_at(w, 35, 10)}).created_id;
    }
    TrainId buy(std::vector<StationId> route, LocoTypeId loco = 0) {
        const CommandResult r = w.execute(BuyTrain{.loco = loco, .cars = 2, .route = std::move(route)});
        REQUIRE(r.ok);
        return r.created_id;
    }
    void run_days(int days) {
        for (int i = 0; i < days * World::kTicksPerDay; ++i) w.tick();
    }
    const Train& train(TrainId t) const { return w.railway().train(t); }
};

} // namespace

TEST_CASE("a train's stops can be changed, keeping the consists of stops it still calls at [D]") {
    Line l;
    const TrainId t = l.buy({l.west, l.east});
    REQUIRE(l.w.execute(SetConsist{.train = t, .stop = 1, .rule = {.max = 1}}).ok);
    REQUIRE(l.w.execute(SetConsist{.train = t, .stop = 0, .rule = {.filter = CargoFilter::Freight, .max = 2}}).ok);

    REQUIRE(l.w.execute(SetRoute{.train = t, .route = {l.east, l.mid, l.west}}).ok);
    const Train& tr = l.train(t);
    CHECK(tr.route == std::vector<StationId>{l.east, l.mid, l.west});
    REQUIRE(tr.rules.size() == 3);
    CHECK(tr.rules[0].max == 1); // east kept its rule
    CHECK(tr.rules[2].max == 2); // and west its own
    CHECK(tr.rules[2].filter == CargoFilter::Freight);
    CHECK(tr.rules[1].max == 2); // the new stop takes the old first stop's
    CHECK(tr.service_stop == std::vector<std::uint8_t>{0, 0, 0});

    // The train now calls at the middle station too.
    l.run_days(30);
    CHECK(tr.stops_made >= 3);
    CHECK(tr.state != TrainState::NoRoute);
}

TEST_CASE("a train bound for a stop taken off its route turns for the first stop") {
    Line l;
    const TrainId t = l.buy({l.west, l.east});
    // Set off east; partway there, drop the east station.
    for (int i = 0; i < 40 && l.train(t).state != TrainState::Moving; ++i) l.w.tick();
    for (int i = 0; i < 20; ++i) l.w.tick();
    REQUIRE(l.train(t).state == TrainState::Moving);
    REQUIRE(l.train(t).route[l.train(t).stop_index] == l.east);
    REQUIRE(l.w.execute(SetRoute{.train = t, .route = {l.mid, l.west}}).ok);
    CHECK(l.train(t).stop_index == 0);

    const std::uint32_t before = l.train(t).stops_made;
    for (int i = 0; i < 20 * World::kTicksPerDay && l.train(t).stops_made == before; ++i) l.w.tick();
    REQUIRE(l.train(t).stops_made == before + 1);
    CHECK(l.train(t).at_node == l.w.railway().station(l.mid).node);
}

TEST_CASE("a train standing at a stop taken off its route leaves for the first stop") {
    Line l;
    const TrainId t = l.buy({l.west, l.east});
    l.run_days(20); // some way along
    // Catch it standing at a station.
    for (int i = 0; i < 20 * World::kTicksPerDay && l.train(t).state != TrainState::Dwelling; ++i) l.w.tick();
    REQUIRE(l.train(t).state == TrainState::Dwelling);
    const StationId here = l.train(t).route[l.train(t).stop_index];
    const StationId other = here == l.west ? l.east : l.west;
    REQUIRE(l.w.execute(SetRoute{.train = t, .route = {l.mid, other}}).ok);
    const std::uint32_t before = l.train(t).stops_made;
    for (int i = 0; i < 20 * World::kTicksPerDay && l.train(t).stops_made == before; ++i) l.w.tick();
    CHECK(l.train(t).at_node == l.w.railway().station(l.mid).node);
}

TEST_CASE("route changes are checked") {
    Line l;
    const TrainId t = l.buy({l.west, l.east});
    CHECK(l.w.execute(SetRoute{.train = t, .route = {l.west}}).error.find("two stops") != std::string::npos);
    CHECK(l.w.execute(SetRoute{.train = t, .route = {l.west, 99}}).error.find("unknown station") != std::string::npos);
    CHECK(l.w.execute(SetRoute{.train = t, .route = {l.west, l.west, l.east}}).error.find("twice") != std::string::npos);
    CHECK_FALSE(l.w.execute(SetRoute{.train = t, .route = {l.west, l.east}, .service_stops = {true, false, true}}).ok);
    CHECK(l.w.execute(SetRoute{.train = 42, .route = {l.west, l.east}}).error.find("no such train") != std::string::npos);
    const PlayerId rival = l.w.add_player_company("Rival", "R");
    CHECK(l.w.execute(SetRoute{.train = t, .route = {l.west, l.mid}}, rival).error.find("your own") != std::string::npos);
    CHECK(l.w.execute(SetPriority{.train = t, .priority = 3}, rival).error.find("your own") != std::string::npos);
}

TEST_CASE("an electric train's new route must be wired all the way [D]") {
    Line l;
    // Wire only the western half.
    std::vector<EdgeId> west_half;
    const MapPoint middle = l.w.railway().track().node(l.w.railway().station(l.mid).node).pos;
    for (const TrackEdge& e : l.w.railway().track().edges()) {
        if (l.w.railway().track().node(e.a).pos.x_mm <= middle.x_mm && l.w.railway().track().node(e.b).pos.x_mm <= middle.x_mm) {
            west_half.push_back(e.id);
        }
    }
    REQUIRE(l.w.execute(ElectrifyTrack{.edges = west_half}).ok);
    const TrainId t = l.buy({l.west, l.mid}, 1);
    CHECK(l.w.execute(SetRoute{.train = t, .route = {l.west, l.east}}).error.find("electrified") != std::string::npos);
    CHECK(l.train(t).route == std::vector<StationId>{l.west, l.mid});
    CHECK(l.w.execute(SetRoute{.train = t, .route = {l.mid, l.west}}).ok);
}

TEST_CASE("at a service stop a train fills up even when not low, and takes the time [D]") {
    auto stops = [](bool service) {
        Line l;
        REQUIRE(l.w.execute(BuildServiceBuilding{.at = node_at(l.w, 35, 10)}).ok);
        REQUIRE(l.w.execute(BuildServiceBuilding{.at = node_at(l.w, 35, 10), .type = ServiceType::MaintenanceFacility}).ok);
        const TrainId t = l.buy({l.west, l.east});
        REQUIRE(l.w.execute(SetRoute{.train = t, .route = {l.west, l.east}, .service_stops = {false, service}}).ok);
        // Run to the east station.
        for (int i = 0; i < 40 * World::kTicksPerDay && l.train(t).stops_made < 1; ++i) l.w.tick();
        REQUIRE(l.train(t).stops_made == 1);
        REQUIRE(l.train(t).at_node == l.w.railway().station(l.east).node);
        const Train& tr = l.train(t);
        return std::tuple{tr.service_stops, tr.oil, tr.wait_ticks_left};
    };
    const auto [plain_stops, plain_oil, plain_wait] = stops(false);
    const auto [service_stops, service_oil, service_wait] = stops(true);
    CHECK(plain_stops == 0); // 30 km does not run the gauges low
    CHECK(plain_oil < kGaugeFull);
    CHECK(service_stops == 1);
    CHECK(service_oil == kGaugeFull);
    CHECK(service_wait > plain_wait);
}

TEST_CASE("copying a train copies its service stops; priority can be changed [D]") {
    Line l;
    const TrainId t = l.buy({l.west, l.east});
    REQUIRE(l.w.execute(SetRoute{.train = t, .route = {l.west, l.mid, l.east}, .service_stops = {false, true}}).ok);
    CHECK(l.train(t).service_stop == std::vector<std::uint8_t>{0, 1, 0});
    const CommandResult copy = l.w.execute(CopyTrain{.train = t});
    REQUIRE(copy.ok);
    CHECK(l.train(copy.created_id).route == l.train(t).route);
    CHECK(l.train(copy.created_id).service_stop == l.train(t).service_stop);

    REQUIRE(l.w.execute(SetPriority{.train = t, .priority = 2}).ok);
    CHECK(l.train(t).priority == 2);
}

TEST_CASE("service stops survive saving and loading") {
    Line l;
    const TrainId t = l.buy({l.west, l.east});
    REQUIRE(l.w.execute(SetRoute{.train = t, .route = {l.west, l.mid, l.east}, .service_stops = {true, false, true}}).ok);
    const World loaded = World::load(l.w.save(), data());
    CHECK(loaded.railway().train(t).route == l.train(t).route);
    CHECK(loaded.railway().train(t).service_stop == std::vector<std::uint8_t>{1, 0, 1});
}
