#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000; // millimetres

GameData test_data() {
    return {CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})"),
            LocomotiveRegistry::from_json(R"({"locomotives": [
                {"key": "old", "name": "Old", "fuel": "steam", "available_from": 1800, "available_until": 1840,
                 "top_speed_mph": 30, "cost": 10000, "maintenance_per_year": 1000},
                {"key": "new", "name": "New", "fuel": "steam", "available_from": 1900,
                 "top_speed_mph": 90, "cost": 90000, "maintenance_per_year": 1000}]})")};
}

// A flat, dry 20 x 20 km world.
World flat_world() {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 20;
    cfg.start_date = Date::from_ymd(1835, 1, 1);
    World w(cfg, test_data());
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

TrackEnd free_at(std::int64_t x_km, std::int64_t y_km) {
    return {TrackEnd::Kind::Free, 0, 0, {x_km * kKm, y_km * kKm}};
}

BuildTrack track(TrackEnd from, TrackEnd to) { return {.start = from, .end = to}; }

} // namespace

TEST_CASE("picking prefers nodes, then track, then open ground") {
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({10 * kKm, 0}, 0);
    const EdgeId e = net.add_edge(a, b);

    const TrackEnd near_node = pick_track_end(net, {200'000, 100'000}, 500'000);
    CHECK(near_node.kind == TrackEnd::Kind::Node);
    CHECK(near_node.node == a);

    const TrackEnd on_track = pick_track_end(net, {4 * kKm, 300'000}, 500'000);
    CHECK(on_track.kind == TrackEnd::Kind::OnEdge);
    CHECK(on_track.edge == e);
    CHECK(on_track.pos == MapPoint{4 * kKm, 0});

    const TrackEnd open = pick_track_end(net, {4 * kKm, 3 * kKm}, 500'000);
    CHECK(open.kind == TrackEnd::Kind::Free);
}

TEST_CASE("picking works far from the origin without overflow") {
    TrackNetwork net;
    const NodeId a = net.add_node({500 * kKm, 500 * kKm}, 0);
    const NodeId b = net.add_node({900 * kKm, 700 * kKm}, 0);
    net.add_edge(a, b);
    const auto ep = net.nearest_edge_point({700 * kKm, 600 * kKm + 1000}, kKm);
    REQUIRE(ep.has_value());
    CHECK(std::abs(ep->pos.x_mm - 700 * kKm) < 1000);
    CHECK(std::abs(ep->pos.y_mm - 600 * kKm) < 1000);
}

TEST_CASE("splitting an edge keeps the line connected and interpolates height") {
    TrackNetwork net;
    const NodeId a = net.add_node({0, 0}, 0);
    const NodeId b = net.add_node({10 * kKm, 0}, 100'000);
    const EdgeId e = net.add_edge(a, b, true, TrackKind::Bridge, BridgeType::Steel);
    EdgeId second = 0;
    const NodeId mid = net.split_edge(e, {4 * kKm, 0}, &second);
    CHECK(net.node(mid).z_mm == 40'000);
    CHECK(net.edge(e).b == mid);
    CHECK(net.edge(e).length_mm == 4 * kKm);
    CHECK(net.edge(second).length_mm == 6 * kKm);
    CHECK(net.edge(second).bridge == BridgeType::Steel);
    CHECK(net.edge(second).double_track);
    CHECK(net.shortest_path(a, b)->size() == 2);
    CHECK(net.edges_at(mid).size() == 2);
    CHECK_THROWS(net.split_edge(e, {0, 0}));
}

TEST_CASE("a train on a piece of track that is split carries on undisturbed") {
    auto locos = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 30, "cost": 1, "maintenance_per_year": 1}]})");
    for (const bool eastbound : {true, false}) {
        Railway rw;
        rw.set_rules({.breakdowns = false});
        const NodeId a = rw.track().add_node({0, 0}, 0);
        const NodeId b = rw.track().add_node({100 * kKm, 0}, 0);
        const EdgeId e = rw.track().add_edge(a, b);
        const StationId sa = rw.add_station("A", a, StationSize::Small);
        const StationId sb = rw.add_station("B", b, StationSize::Small);
        const TrainId t = eastbound ? rw.add_train(0, {}, {sa, sb}) : rw.add_train(0, {}, {sb, sa});
        for (int i = 0; i < 40; ++i) rw.tick(locos);
        const MapPoint before = rw.train_position(t);

        rw.split_edge(e, {50 * kKm, 0});
        CHECK(rw.train_position(t) == before);
        CHECK(rw.train(t).path.size() == 2);

        // And it still arrives.
        for (int i = 0; i < 20'000 && rw.train(t).stops_made == 0; ++i) rw.tick(locos);
        CHECK(rw.train(t).at_node == (eastbound ? b : a));
    }
}

TEST_CASE("building track on open ground, then branching from the middle of it") {
    World w = flat_world();
    BuildTrack line = track(free_at(2, 5), free_at(12, 5));
    const CommandResult r = w.execute(line);
    REQUIRE(r.ok);
    CHECK(r.cost > Money{});
    CHECK(w.total_spent() == r.cost);
    const std::size_t edges_before = w.railway().track().edges().size();

    // Branch south from a point part-way along the line.
    BuildTrack branch =
        // Nodes are every 0.5 km, so pick midway between two, close to the line.
        track(pick_track_end(w.railway().track(), {7 * kKm + 250'000, 5 * kKm + 100'000}, 150'000), free_at(7, 12));
    REQUIRE(branch.start.kind == TrackEnd::Kind::OnEdge);
    const CommandResult rb = w.execute(branch);
    REQUIRE(rb.ok);
    CHECK(w.railway().track().edges().size() > edges_before + 1); // split + new pieces
    // Every end of the T is reachable from every other.
    const NodeId west = *w.railway().track().nearest_node({2 * kKm, 5 * kKm}, 1);
    CHECK(w.railway().track().shortest_path(west, rb.created_id).has_value());
}

TEST_CASE("joining two existing lines end to end with a curve") {
    World w = flat_world();
    const CommandResult a = w.execute(track(free_at(2, 2), free_at(8, 2)));
    const CommandResult b = w.execute(track(free_at(14, 8), free_at(14, 14)));
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    const TrackNetwork& net = w.railway().track();
    BuildTrack join = track({TrackEnd::Kind::Node, a.created_id, 0, net.node(a.created_id).pos},
                            {TrackEnd::Kind::Node, *net.nearest_node({14 * kKm, 8 * kKm}, 1), 0, {14 * kKm, 8 * kKm}});
    join.curve_control = continuing_control_point(net, a.created_id, join.end.pos);
    REQUIRE(join.curve_control.has_value());
    const PlanResult preview = w.preview(join);
    REQUIRE(preview.plan.has_value());
    const CommandResult r = w.execute(join);
    REQUIRE(r.ok);
    CHECK(r.cost == preview.plan->total_cost); // what you see is what you pay
    CHECK(net.shortest_path(*net.nearest_node({2 * kKm, 2 * kKm}, 1), *net.nearest_node({14 * kKm, 14 * kKm}, 1))
              .has_value());
}

TEST_CASE("failed commands change nothing") {
    World w = flat_world();
    w.terrain().set_ground(10, 10, GroundType::Water);
    const std::size_t nodes = w.railway().track().nodes().size();
    const CommandResult r = w.execute(track(free_at(5, 10), {TrackEnd::Kind::Free, 0, 0, {10'500'000, 10'500'000}}));
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(w.railway().track().nodes().size() == nodes);
    CHECK(w.total_spent() == Money{});

    CHECK_FALSE(w.execute(track(free_at(5, 5), free_at(5, 5))).ok);
    CHECK_FALSE(w.execute(BuildStation{.at = free_at(5, 5)}).ok);
    CHECK_FALSE(w.execute(BuildServiceBuilding{.at = free_at(5, 5)}).ok);
}

TEST_CASE("stations and support buildings go on track; trains need a valid route and loco") {
    World w = flat_world();
    REQUIRE(w.execute(track(free_at(2, 5), free_at(12, 5))).ok);
    const TrackNetwork& net = w.railway().track();
    const auto at = [&](std::int64_t x_km) { return pick_track_end(net, {x_km * kKm, 5 * kKm}, 600'000); };

    const CommandResult s1 = w.execute(BuildStation{.at = at(2), .size = StationSize::Small, .name = "West"});
    const CommandResult s2 = w.execute(BuildStation{.at = at(12), .size = StationSize::Large});
    REQUIRE(s1.ok);
    REQUIRE(s2.ok);
    CHECK(s1.cost == Money::dollars(50'000));
    CHECK(w.railway().station(s2.created_id).name == "Station 2");
    CHECK_FALSE(w.execute(BuildStation{.at = at(2)}).ok); // already a station there

    const CommandResult tower = w.execute(BuildServiceBuilding{.at = at(7), .type = ServiceType::ServiceTower});
    REQUIRE(tower.ok);
    CHECK(w.railway().service_buildings().size() == 1);

    CHECK_FALSE(w.execute(BuyTrain{.loco = 1, .cars = 4, .route = {s1.created_id, s2.created_id}}).ok); // "New" is from 1900
    CHECK_FALSE(w.execute(BuyTrain{.loco = 0, .cars = 9, .route = {s1.created_id, s2.created_id}}).ok); // too many cars
    CHECK_FALSE(w.execute(BuyTrain{.loco = 0, .cars = 4, .route = {s1.created_id}}).ok);                // one stop
    const CommandResult train = w.execute(BuyTrain{.loco = 0, .cars = 4, .route = {s1.created_id, s2.created_id}});
    REQUIRE(train.ok);
    CHECK(train.cost == Money::dollars(10'000));
    CHECK(w.railway().train(train.created_id).cars.size() == 4);

    for (int i = 0; i < 5000 && w.railway().train(train.created_id).stops_made == 0; ++i) w.tick();
    CHECK(w.railway().train(train.created_id).stops_made == 1);
}

TEST_CASE("station age counts from a town's first station; open country gets half the effect") {
    World w = flat_world();
    w.economy().add_town({"Townsville", 5, 5});
    REQUIRE(w.execute(track(free_at(2, 5), free_at(18, 5))).ok);
    const TrackNetwork& net = w.railway().track();
    const auto at = [&](std::int64_t x_km) { return pick_track_end(net, {x_km * kKm, 5 * kKm}, 600'000); };

    const CommandResult in_town = w.execute(BuildStation{.at = at(5)});
    const CommandResult country = w.execute(BuildStation{.at = at(16)});
    REQUIRE(in_town.ok);
    REQUIRE(country.ok);
    CHECK(w.railway().station(in_town.created_id).town == std::size_t{0});
    CHECK_FALSE(w.railway().station(country.created_id).town.has_value());
    CHECK(w.revenue_permille(in_town.created_id) == 1150);
    CHECK(w.revenue_permille(country.created_id) == 1075);

    for (int d = 0; d < 2 * 365; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    // A second station in the same town is as "old" as the town's first.
    const CommandResult second = w.execute(BuildStation{.at = at(7)});
    REQUIRE(second.ok);
    CHECK(w.revenue_permille(second.created_id) == w.revenue_permille(in_town.created_id));
    CHECK(w.revenue_permille(second.created_id) < 1100);
}
