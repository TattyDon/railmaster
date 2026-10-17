#include "railmaster/sim/railway.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

TEST_CASE("car weights step up by era; express cars, cabooses and diners weigh 2/3 (§4.2 [D])") {
    CHECK(car_weight_kg(false, 1800) == 10'000); // before the first era: its weight
    CHECK(car_weight_kg(false, 1849) == 10'000);
    CHECK(car_weight_kg(true, 1849) == 6'670);
    CHECK(car_weight_kg(false, 1850) == 20'000);
    CHECK(car_weight_kg(false, 1899) == 20'000);
    CHECK(car_weight_kg(false, 1900) == 40'000); // the step a consist feels overnight
    CHECK(car_weight_kg(false, 2030) == 80'000);
    CHECK(reference_weight_kg(1930) == 160'000); // four freight cars

    Train t;
    t.cars.resize(3);
    t.cars[0].cargo = 0;
    t.cars[0].milli = 1000;
    t.cars[1].cargo = 1;
    t.cars[1].milli = 500;
    t.cars[1].destination = 3;
    t.caboose = true;
    t.diner = true;
    // A freight car, then an express car, caboose and diner at 2/3; the empty car stays behind.
    CHECK(train_weight_kg(t, 1930) == 40'000 + 3 * 26'680);
}

TEST_CASE("speed falls with load over the engine's free weight and with grade x load (§10.2 [I])") {
    const auto locos = test_locos(); // defaults: free weight 50 t, 18% a 1% grade at the reference load
    const auto& loco = locos.get(0);
    const std::int64_t top = mph_to_mm_per_tick(60);
    const std::int64_t ref = reference_weight_kg(1830); // 40 t
    CHECK(speed_for_weight_mm_per_tick(loco, 0, ref, 0) == top);
    CHECK(speed_for_weight_mm_per_tick(loco, 50'000, ref, 0) == top); // within the free weight
    // 30 t over: 1 - 0.5 x 30 / 40 = 0.625.
    CHECK(speed_for_weight_mm_per_tick(loco, 80'000, ref, 0) == top * 625 / 1000);
    CHECK(speed_for_weight_mm_per_tick(loco, 500'000, ref, 0) == top * 200 / 1000); // floor 20%
    // Grade at the reference load: 2% x 18% = 36% slower; downhill counts as flat.
    CHECK(speed_for_weight_mm_per_tick(loco, 40'000, ref, 200) == top * 640 / 1000);
    CHECK(speed_for_weight_mm_per_tick(loco, 40'000, ref, -300) == top);
    // Twice the load, twice the grade loss; an empty engine climbs freely.
    CHECK(speed_for_weight_mm_per_tick(loco, 80'000, ref, 200) < speed_for_weight_mm_per_tick(loco, 40'000, ref, 200));
    CHECK(speed_for_weight_mm_per_tick(loco, 0, ref, 300) == top);
    CHECK(speed_for_weight_mm_per_tick(loco, 40'000, ref, 5000) == top * 50 / 1000); // floor 5%
    // Without sand, three times the grade loss.
    CHECK(speed_for_weight_mm_per_tick(loco, 40'000, ref, 100, default_balance(), false) == top * 460 / 1000);
    // The Add Train chart: four freight cars of the era are the reference load.
    CHECK(chart_speed_mm_per_tick(loco, 4, false, 1830, 200) == top * 640 / 1000);
}

TEST_CASE("curves slow trains below the comfort radius (§10.2 [I])") {
    const MapPoint a{0, 0}, j{500'000, 0};
    CHECK(curve_speed_permille(a, j, {1'000'000, 0}) == 1000);            // straight on
    CHECK(curve_speed_permille(a, j, {1'000'000, 50'000}) == 1000);       // radius ~5 km
    const std::int32_t gentle = curve_speed_permille(a, j, {1'000'000, 125'000}); // ~2 km
    CHECK(gentle > 400);
    CHECK(gentle < 600);
    CHECK(curve_speed_permille(a, j, {500'000, 500'000}) == 250); // a right angle: the floor
    CHECK(curve_speed_permille(a, j, {0, 10}) == 250);             // doubling back
}

TEST_CASE("trains gather speed along a first-order lag, quick at first (§10.2 [I])") {
    const auto locos = test_locos(); // time constant half a day: 8 ticks
    Line l = make_line(200, 0);
    const TrainId t = l.rw.add_train(0, 2, {l.west, l.east});
    l.rw.tick(locos); // departs
    const std::int64_t top = mph_to_mm_per_tick(60);
    std::vector<std::int64_t> v;
    for (int i = 0; i < 40; ++i) {
        l.rw.tick(locos);
        v.push_back(l.rw.train(t).speed_mm_per_tick);
    }
    CHECK(v[0] == (top + 7) / 8);                 // an eighth of the gap at once
    CHECK(v[7] > top * 60 / 100);                 // about 63% after one time constant
    CHECK(v[7] < top * 70 / 100);
    CHECK(v[1] - v[0] > v[8] - v[7]);             // levelling off
    CHECK(v[39] >= top * 99 / 100);
    CHECK(v[39] <= top);
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
    // Eight loaded cars: empty ones are left behind and do not slow a train.
    for (Railway* rw : {&flat.rw, &hill.rw})
        for (Car& c : rw->train_mut(0).cars) c.cargo = CargoId{0};
    const int flat_ticks = ticks_until_stop(flat.rw, locos, tf, 1);
    const int hill_ticks = ticks_until_stop(hill.rw, locos, th, 1);
    CHECK(hill_ticks > flat_ticks * 3 / 2);

    // The same train running light climbs faster.
    Line light = make_line(20, 400);
    const TrainId tl = light.rw.add_train(0, 8, {light.west, light.east});
    CHECK(ticks_until_stop(light.rw, locos, tl, 1) < hill_ticks);
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

TEST_CASE("no overtaking on single track: a fast train follows a slow one at a gap (§10.3 [I])") {
    const auto locos = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "slow", "name": "Slow", "fuel": "diesel", "available_from": 1800, "top_speed_mph": 20, "cost": 1,
         "maintenance_per_year": 1},
        {"key": "fast", "name": "Fast", "fuel": "diesel", "available_from": 1800, "top_speed_mph": 90, "cost": 1,
         "maintenance_per_year": 1}]})");
    const auto race = [&](bool double_track) {
        Line l = make_line(30, 0, double_track);
        const TrainId slow = l.rw.add_train(0, 0, {l.west, l.east});
        for (int i = 0; i < 20; ++i) l.rw.tick(locos); // a head start
        const TrainId fast = l.rw.add_train(1, 0, {l.west, l.east});
        std::int64_t closest = 1'000'000'000;
        for (int i = 0; i < 2000 && l.rw.train(slow).stops_made < 2; ++i) {
            l.rw.tick(locos);
            const Train& a = l.rw.train(slow);
            const Train& b = l.rw.train(fast);
            // Both out on the line together (the fast one held at the
            // platform until the slow one is a gap ahead counts as waiting).
            if (a.state == TrainState::Moving && b.state == TrainState::Moving && a.stops_made == b.stops_made &&
                b.offset_mm > 0) {
                closest = std::min(closest, a.offset_mm - b.offset_mm);
            }
            if (l.rw.train(fast).stops_made == 2) return std::pair{false, closest}; // fast got there first
        }
        return std::pair{true, closest};
    };
    const auto [slow_first, gap] = race(false);
    CHECK(slow_first);
    CHECK(gap >= default_balance().trains.follow_gap_mm);
    CHECK(gap < default_balance().trains.follow_gap_mm + 200'000); // it closed right up
    // On double track the fast train overtakes.
    CHECK_FALSE(race(true).first);
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

TEST_CASE("the shipped roster plays its roles: freight engines haul uphill, single-drivers run light [I]") {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/locomotives.json");
    std::stringstream ss;
    ss << in.rdbuf();
    const auto reg = LocomotiveRegistry::from_json(ss.str());
    const auto& american = reg.get(*reg.find("american_440"));
    const auto& consolidation = reg.get(*reg.find("consolidation_280"));
    const auto& shay = reg.get(*reg.find("shay_2truck"));
    // Eight freight cars up 2% in 1870: the Consolidation keeps going, the American crawls.
    CHECK(chart_speed_mm_per_tick(consolidation, 8, false, 1870, 200) >
          3 * chart_speed_mm_per_tick(american, 8, false, 1870, 200));
    // Six cars up 3% in 1885: the Shay, slow on the flat, out-climbs the American.
    CHECK(chart_speed_mm_per_tick(american, 6, false, 1885, 0) > chart_speed_mm_per_tick(shay, 6, false, 1885, 0));
    CHECK(chart_speed_mm_per_tick(shay, 6, false, 1885, 300) > chart_speed_mm_per_tick(american, 6, false, 1885, 300));
    // Every engine reaches its top speed light on the flat.
    for (const LocomotiveType& l : reg.all()) CHECK(chart_speed_mm_per_tick(l, 0, false, 1900, 0) == mph_to_mm_per_tick(l.top_speed_mph));
}
