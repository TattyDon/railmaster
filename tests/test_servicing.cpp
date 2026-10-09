#include "railmaster/sim/railway.hpp"

#include <doctest/doctest.h>

#include <utility>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000; // millimetres

LocomotiveRegistry locos(std::int32_t reliability = 100) {
    return LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "steam", "name": "Steam", "fuel": "steam", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 10000, "reliability": )" +
                                         std::to_string(reliability) + R"(},
        {"key": "diesel", "name": "Diesel", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 10000}]})");
}

constexpr LocoTypeId kSteam = 0;
constexpr LocoTypeId kDiesel = 1;

// A straight line of `pieces` 1 km pieces, rising `rise_m_per_piece` each,
// with stations at both ends. Node i is i km from the west end.
struct Line {
    Railway rw;
    std::vector<NodeId> nodes;
    StationId west = 0, east = 0;
};

Line make_line(int pieces, std::int64_t rise_m_per_piece = 0) {
    Line l;
    l.rw.set_rules({.breakdowns = false});
    for (int i = 0; i <= pieces; ++i) l.nodes.push_back(l.rw.track().add_node({i * kKm, 0}, i * rise_m_per_piece * 1000));
    for (int i = 0; i < pieces; ++i) l.rw.track().add_edge(l.nodes[static_cast<std::size_t>(i)], l.nodes[static_cast<std::size_t>(i + 1)]);
    l.west = l.rw.add_station("West", l.nodes.front(), StationSize::Small);
    l.east = l.rw.add_station("East", l.nodes.back(), StationSize::Small);
    return l;
}

void run_until(Railway& rw, const LocomotiveRegistry& reg, TrainId t, std::uint32_t stops) {
    for (int i = 0; i < 200'000 && rw.train(t).stops_made < stops; ++i) rw.tick(reg);
    REQUIRE(rw.train(t).stops_made >= stops);
}

} // namespace

TEST_CASE("steam engines use water; diesels do not; everyone uses oil") {
    const auto reg = locos();
    Line l = make_line(60);
    const TrainId steam = l.rw.add_train(kSteam, 0, {l.west, l.east});
    const TrainId diesel = l.rw.add_train(kDiesel, 0, {l.west, l.east});
    run_until(l.rw, reg, steam, 1);
    run_until(l.rw, reg, diesel, 1);
    // 60 km of a 150 km tank: 40% used.
    CHECK(l.rw.train(steam).water == 600);
    CHECK(l.rw.train(diesel).water == kGaugeFull);
    CHECK(l.rw.train(steam).oil == 960); // 60 of 1,500 km
    CHECK(l.rw.train(diesel).oil == 960);
    CHECK(l.rw.train(steam).sand == kGaugeFull); // flat: no sand needed
}

TEST_CASE("sand is used only when climbing") {
    const auto reg = locos();
    Line l = make_line(10, 10); // 1% grade, 100 m total climb
    const TrainId t = l.rw.add_train(kDiesel, 0, {l.west, l.east});
    run_until(l.rw, reg, t, 1);
    const std::int32_t after_climb = l.rw.train(t).sand;
    CHECK(after_climb < kGaugeFull);
    CHECK(after_climb == kGaugeFull - 100'000LL * kGaugeFull / default_balance().servicing.sand_range_climb_mm);
    run_until(l.rw, reg, t, 2); // back downhill
    CHECK(l.rw.train(t).sand == after_climb);
}

TEST_CASE("a steam engine with no water crawls") {
    const auto reg = locos();
    const auto& steam = reg.get(kSteam);
    Train full, dry;
    dry.water = 0;
    const std::int64_t normal = target_speed_mm_per_tick(steam, full, 0);
    CHECK(target_speed_mm_per_tick(steam, dry, 0) == normal * default_balance().servicing.no_water_speed_permille / 1000);
    // Diesels do not care about water.
    CHECK(target_speed_mm_per_tick(reg.get(kDiesel), dry, 0) == normal);
}

TEST_CASE("without sand an engine loses climbing ability but not speed on the flat") {
    const auto reg = locos();
    const auto& diesel = reg.get(kDiesel);
    Train sanded, unsanded;
    unsanded.sand = 0;
    CHECK(target_speed_mm_per_tick(diesel, unsanded, 0) == target_speed_mm_per_tick(diesel, sanded, 0));
    CHECK(target_speed_mm_per_tick(diesel, unsanded, 150) < target_speed_mm_per_tick(diesel, sanded, 150));
}

TEST_CASE("a train passing a service tower stops to refill only when low") {
    const auto reg = locos();
    Line l = make_line(200);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[50]);  // passed with water at 66%
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[120]); // passed with water at 20%
    const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});

    bool seen_servicing = false;
    std::int32_t lowest_water = kGaugeFull;
    for (int i = 0; i < 100'000 && l.rw.train(t).stops_made == 0; ++i) {
        l.rw.tick(reg);
        seen_servicing = seen_servicing || l.rw.train(t).state == TrainState::Servicing;
        lowest_water = std::min(lowest_water, l.rw.train(t).water);
    }
    CHECK(seen_servicing);
    CHECK(l.rw.train(t).service_stops == 1); // skipped the first tower: not yet low
    CHECK(lowest_water < default_balance().servicing.service_threshold_permille);
    CHECK(l.rw.train(t).water > 400); // refilled at km 120, then ran 80 km
}

TEST_CASE("maintenance facilities refill oil; service towers do not") {
    const auto reg = locos();
    // 1,000 km line: oil falls below half after 750 km.
    Line l = make_line(1000);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[800]); // does not touch oil
    l.rw.add_service_building(ServiceType::MaintenanceFacility, l.nodes[900]);
    const TrainId t = l.rw.add_train(kDiesel, 0, {l.west, l.east});
    run_until(l.rw, reg, t, 1);
    CHECK(l.rw.train(t).service_stops == 1); // diesel: tower not needed, facility needed
    CHECK(l.rw.train(t).oil == kGaugeFull - 100 * kGaugeFull / 1500); // refilled at 900, ran 100 km
}

TEST_CASE("nothing is serviced when nothing is low") {
    const auto reg = locos();
    Line l = make_line(10);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[3]);
    l.rw.add_service_building(ServiceType::MaintenanceFacility, l.nodes[6]);
    const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});
    run_until(l.rw, reg, t, 1);
    CHECK(l.rw.train(t).service_stops == 0);
}

TEST_CASE("a service building at a station services during the station stop") {
    const auto reg = locos();
    Line l = make_line(100);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes.back());
    const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});
    run_until(l.rw, reg, t, 1);
    CHECK(l.rw.train(t).state == TrainState::Dwelling);
    CHECK(l.rw.train(t).water == kGaugeFull);
    CHECK(l.rw.train(t).service_stops == 1);
}

TEST_CASE("breakdowns happen at roughly the expected rate and stop the train") {
    const auto reg = locos();
    Line l = make_line(500);
    l.rw.set_rules({.breakdowns = true});
    l.rw.add_service_building(ServiceType::MaintenanceFacility, l.nodes[0]);
    l.rw.add_service_building(ServiceType::MaintenanceFacility, l.nodes[250]);
    l.rw.add_service_building(ServiceType::MaintenanceFacility, l.nodes[500]);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[100]);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[200]);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[300]);
    l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[400]);
    const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});

    run_until(l.rw, reg, t, 40); // 40 trips of 500 km = 20,000 km
    // About 10 expected at one per 2,000 km with oil kept high; allow wide variance.
    const auto n = l.rw.train(t).breakdowns;
    CHECK(n >= 3);
    CHECK(n <= 25);
}

TEST_CASE("a broken-down train stays put, then carries on") {
    const auto reg = locos(5); // unreliable, so a breakdown comes quickly
    Line l = make_line(500);
    l.rw.set_rules({.breakdowns = true});
    const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});
    for (int i = 0; i < 100'000 && l.rw.train(t).breakdowns == 0; ++i) l.rw.tick(reg);
    REQUIRE(l.rw.train(t).breakdowns == 1);
    CHECK(l.rw.train(t).state == TrainState::BrokenDown);
    const MapPoint where = l.rw.train_position(t);
    const auto progress = [&] { return std::pair{l.rw.train(t).step, l.rw.train(t).offset_mm}; };
    const auto stopped_at = progress();
    for (int i = 0; i < default_balance().breakdowns.breakdown_ticks - 1; ++i) {
        l.rw.tick(reg);
        CHECK(l.rw.train_position(t) == where);
    }
    l.rw.tick(reg); // repaired
    CHECK(l.rw.train(t).state == TrainState::Moving);
    l.rw.tick(reg); // under way again, further along its path
    CHECK(progress() > stopped_at);
}

TEST_CASE("breakdowns are off when the rules say so") {
    const auto reg = locos(1); // terrible reliability
    Line l = make_line(100);
    const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});
    run_until(l.rw, reg, t, 10);
    CHECK(l.rw.train(t).breakdowns == 0);
}

TEST_CASE("low reliability and low oil mean more breakdowns") {
    auto count = [](std::int32_t reliability, bool maintained) {
        const auto reg = locos(reliability);
        Line l = make_line(300);
        l.rw.set_rules({.breakdowns = true});
        l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[100]);
        l.rw.add_service_building(ServiceType::ServiceTower, l.nodes[200]);
        if (maintained) l.rw.add_service_building(ServiceType::MaintenanceFacility, l.nodes[150]);
        const TrainId t = l.rw.add_train(kSteam, 0, {l.west, l.east});
        run_until(l.rw, reg, t, 30);
        return l.rw.train(t).breakdowns;
    };
    CHECK(count(50, true) > count(200, true));
    CHECK(count(100, false) > count(100, true));
}

TEST_CASE("simulation with breakdowns is deterministic for a given seed") {
    auto run = [] {
        const auto reg = locos(30);
        Railway rw(42);
        std::vector<NodeId> n;
        for (int i = 0; i <= 50; ++i) n.push_back(rw.track().add_node({i * kKm, 0}, 0));
        for (int i = 0; i < 50; ++i) rw.track().add_edge(n[static_cast<std::size_t>(i)], n[static_cast<std::size_t>(i + 1)]);
        const StationId a = rw.add_station("A", n.front(), StationSize::Small);
        const StationId b = rw.add_station("B", n.back(), StationSize::Small);
        rw.add_service_building(ServiceType::ServiceTower, n[25]);
        rw.add_train(kSteam, 0, {a, b});
        rw.add_train(kDiesel, 0, {b, a});
        for (int i = 0; i < 20'000; ++i) rw.tick(reg);
        return std::vector<std::uint32_t>{rw.train(0).breakdowns, rw.train(1).breakdowns, rw.train(0).stops_made,
                                          rw.train(1).stops_made, static_cast<std::uint32_t>(rw.train(0).offset_mm)};
    };
    CHECK(run() == run());
}

TEST_CASE("maintenance grows with age to 3x at 20 years and doubles on low oil") {
    const auto reg = locos();
    const auto& loco = reg.get(kSteam); // $10,000 a year new
    CHECK(annual_maintenance(loco, 0, kGaugeFull) == Money::dollars(10'000));
    CHECK(annual_maintenance(loco, 10, kGaugeFull) == Money::dollars(20'000));
    CHECK(annual_maintenance(loco, 20, kGaugeFull) == Money::dollars(30'000));
    CHECK(annual_maintenance(loco, 35, kGaugeFull) == Money::dollars(30'000)); // capped
    CHECK(annual_maintenance(loco, 0, 100) == Money::dollars(20'000));
}

TEST_CASE("support building prices") {
    CHECK(service_building_cost(ServiceType::ServiceTower) < service_building_cost(ServiceType::MaintenanceFacility));
}
