#include "railmaster/sim/railway.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000; // millimetres

LocomotiveRegistry test_locos() {
    return LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "loco", "name": "Loco", "fuel": "steam", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1}]})");
}

// Two stations `km` apart on a straight line, with the far end `rise_m` higher.
struct Line {
    Railway rw;
    StationId west = 0, east = 0;
    EdgeId edge = 0;
};

// Breakdowns are off so these tests exercise movement alone; see test_servicing.cpp.
Line make_line(std::int64_t km, std::int64_t rise_m, bool double_track = false) {
    Line l;
    l.rw.set_rules({.breakdowns = false});
    const NodeId a = l.rw.track().add_node({0, 0}, 0);
    const NodeId b = l.rw.track().add_node({km * kKm, 0}, rise_m * 1000);
    l.edge = l.rw.track().add_edge(a, b, double_track);
    l.west = l.rw.add_station("West", a, StationSize::Small);
    l.east = l.rw.add_station("East", b, StationSize::Small);
    return l;
}

int ticks_until_stop(Railway& rw, const LocomotiveRegistry& locos, TrainId id, std::uint32_t stops) {
    for (int i = 0; i < 100'000; ++i) {
        if (rw.train(id).stops_made >= stops) return i;
        rw.tick(locos);
    }
    FAIL("train never arrived");
    return -1;
}

} // namespace

TEST_CASE("station prices") {
    CHECK(station_cost(StationSize::Small) == Money::dollars(50'000));
    CHECK(station_cost(StationSize::Medium) == Money::dollars(100'000));
    CHECK(station_cost(StationSize::Large) == Money::dollars(200'000));
}

TEST_CASE("trains are limited to eight cars and need a route") {
    Line l = make_line(10, 0);
    CHECK_NOTHROW(l.rw.add_train(0, 8, {l.west, l.east}));
    CHECK_THROWS(l.rw.add_train(0, 9, {l.west, l.east}));
    CHECK_THROWS(l.rw.add_train(0, 0, {}));
}

TEST_CASE("grade slows trains in proportion to load") {
    const auto locos = test_locos();
    const auto& loco = locos.get(0);
    const std::int64_t top = mph_to_mm_per_tick(60);
    CHECK(target_speed_mm_per_tick(loco, 8, 0) == top);
    CHECK(target_speed_mm_per_tick(loco, 8, -300) == top);     // downhill: no penalty
    CHECK(target_speed_mm_per_tick(loco, 8, 200) == top / 2);  // 2% with 8 cars halves speed
    CHECK(target_speed_mm_per_tick(loco, 0, 200) > target_speed_mm_per_tick(loco, 8, 200));
    CHECK(target_speed_mm_per_tick(loco, 8, 5000) == top / 10); // floor at 10%
}

TEST_CASE("a train shuttles between two stations and dwells at each") {
    const auto locos = test_locos();
    Line l = make_line(20, 0);
    const TrainId t = l.rw.add_train(0, 2, {l.west, l.east});

    l.rw.tick(locos); // departs
    CHECK(l.rw.train(t).state == TrainState::Moving);

    ticks_until_stop(l.rw, locos, t, 1);
    CHECK(l.rw.train(t).state == TrainState::Dwelling);
    CHECK(l.rw.train(t).at_node == l.rw.station(l.east).node);
    CHECK(l.rw.train_position(t) == MapPoint{20 * kKm, 0});

    ticks_until_stop(l.rw, locos, t, 2);
    CHECK(l.rw.train(t).at_node == l.rw.station(l.west).node);
}

TEST_CASE("uphill trips take longer than flat ones") {
    const auto locos = test_locos();
    Line flat = make_line(20, 0);
    Line hill = make_line(20, 400); // 2% grade
    const TrainId tf = flat.rw.add_train(0, 8, {flat.west, flat.east});
    const TrainId th = hill.rw.add_train(0, 8, {hill.west, hill.east});
    const int flat_ticks = ticks_until_stop(flat.rw, locos, tf, 1);
    const int hill_ticks = ticks_until_stop(hill.rw, locos, th, 1);
    CHECK(hill_ticks > flat_ticks * 3 / 2);
}

TEST_CASE("on single track the lower-priority train waits while the other passes") {
    const auto locos = test_locos();
    Line l = make_line(20, 0);
    const TrainId slow = l.rw.add_train(0, 0, {l.west, l.east}, /*priority=*/0);
    const TrainId fast = l.rw.add_train(0, 0, {l.east, l.west}, /*priority=*/5);

    bool slow_yielded = false;
    for (int i = 0; i < 200 && l.rw.train(fast).stops_made == 0; ++i) {
        l.rw.tick(locos);
        CHECK_FALSE(l.rw.train(fast).yielding);
        slow_yielded = slow_yielded || l.rw.train(slow).yielding;
    }
    CHECK(slow_yielded);
    CHECK(l.rw.train(fast).stops_made == 1);

    // Once passed, the waiting train carries on and arrives.
    ticks_until_stop(l.rw, locos, slow, 1);
    CHECK(l.rw.train(slow).at_node == l.rw.station(l.east).node);
}

TEST_CASE("equal priority: the older train has right of way") {
    const auto locos = test_locos();
    Line l = make_line(20, 0);
    const TrainId older = l.rw.add_train(0, 0, {l.west, l.east});
    const TrainId newer = l.rw.add_train(0, 0, {l.east, l.west});
    bool newer_yielded = false;
    for (int i = 0; i < 200; ++i) {
        l.rw.tick(locos);
        CHECK_FALSE(l.rw.train(older).yielding);
        newer_yielded = newer_yielded || l.rw.train(newer).yielding;
    }
    CHECK(newer_yielded);
}

TEST_CASE("double track removes the wait") {
    const auto locos = test_locos();
    Line l = make_line(20, 0, /*double_track=*/true);
    const TrainId a = l.rw.add_train(0, 0, {l.west, l.east});
    const TrainId b = l.rw.add_train(0, 0, {l.east, l.west});
    for (int i = 0; i < 200; ++i) {
        l.rw.tick(locos);
        CHECK_FALSE(l.rw.train(a).yielding);
        CHECK_FALSE(l.rw.train(b).yielding);
    }
}

TEST_CASE("a train with no route to its next stop waits and recovers") {
    const auto locos = test_locos();
    Railway rw;
    rw.set_rules({.breakdowns = false});
    const NodeId a = rw.track().add_node({0, 0}, 0);
    const NodeId b = rw.track().add_node({5 * kKm, 0}, 0);
    const StationId sa = rw.add_station("A", a, StationSize::Small);
    const StationId sb = rw.add_station("B", b, StationSize::Small);
    const TrainId t = rw.add_train(0, 0, {sa, sb});
    rw.tick(locos);
    CHECK(rw.train(t).state == TrainState::NoRoute);

    rw.track().add_edge(a, b);
    rw.tick(locos);
    CHECK(rw.train(t).state == TrainState::Moving);
}

TEST_CASE("train position interpolates along the track") {
    const auto locos = test_locos();
    Line l = make_line(100, 0);
    const TrainId t = l.rw.add_train(0, 0, {l.west, l.east});
    for (int i = 0; i < 20; ++i) l.rw.tick(locos);
    const MapPoint p = l.rw.train_position(t);
    CHECK(p.y_mm == 0);
    CHECK(p.x_mm == l.rw.train(t).offset_mm);
    CHECK(p.x_mm > 0);
}

TEST_CASE("at equal priority, the train with more valuable cargo has right of way") {
    const auto locos = test_locos();
    Line l = make_line(20, 0);
    const TrainId older = l.rw.add_train(0, 1, {l.west, l.east});
    const TrainId richer = l.rw.add_train(0, 1, {l.east, l.west});
    l.rw.train_mut(richer).cars[0] = Car{CargoId{0}, 1000, 0, 0, l.east, std::nullopt, 50'000};
    bool older_yielded = false;
    for (int i = 0; i < 200; ++i) {
        l.rw.tick(locos);
        CHECK_FALSE(l.rw.train(richer).yielding);
        older_yielded = older_yielded || l.rw.train(older).yielding;
    }
    CHECK(older_yielded);
}

TEST_CASE("wooden bridges slow trains a lot, other bridges a little") {
    const auto locos = test_locos();
    auto trip = [&](TrackKind kind, BridgeType bridge) {
        Railway rw;
        rw.set_rules({.breakdowns = false});
        const NodeId a = rw.track().add_node({0, 0}, 0);
        const NodeId b = rw.track().add_node({40 * kKm, 0}, 0);
        rw.track().add_edge(a, b, false, kind, bridge);
        const StationId sa = rw.add_station("A", a, StationSize::Small);
        const StationId sb = rw.add_station("B", b, StationSize::Small);
        const TrainId t = rw.add_train(0, 0, {sa, sb});
        return ticks_until_stop(rw, locos, t, 1);
    };
    const int ground = trip(TrackKind::Ground, BridgeType::None);
    const int steel = trip(TrackKind::Bridge, BridgeType::Steel);
    const int wood = trip(TrackKind::Bridge, BridgeType::Wood);
    CHECK(steel > ground);
    CHECK(wood > steel * 3 / 2);
}

TEST_CASE("an unreliable train eventually crashes and is destroyed") {
    const auto locos = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "bad", "name": "Bad", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1, "reliability": 1}]})");
    Line l = make_line(1000, 0);
    l.rw.set_rules({.breakdowns = true});
    const TrainId t = l.rw.add_train(0, 2, {l.west, l.east});
    std::vector<TrainId> crashed;
    for (int i = 0; i < 2'000'000 && crashed.empty(); ++i) {
        l.rw.tick(locos);
        for (TrainId c : l.rw.take_crashes()) crashed.push_back(c);
    }
    REQUIRE(crashed.size() == 1);
    CHECK(crashed[0] == t);
    CHECK(l.rw.train(t).state == TrainState::Crashed);
    const MapPoint wreck = l.rw.train_position(t);
    for (int i = 0; i < 100; ++i) l.rw.tick(locos);
    CHECK(l.rw.train_position(t) == wreck); // it never moves again
}

TEST_CASE("no crashes when the rules turn breakdowns and crashes off") {
    const auto locos = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "bad", "name": "Bad", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1, "reliability": 1}]})");
    Line l = make_line(100, 0);
    l.rw.add_train(0, 2, {l.west, l.east});
    for (int i = 0; i < 200'000; ++i) l.rw.tick(locos);
    CHECK(l.rw.take_crashes().empty());
}
