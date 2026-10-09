#include "railmaster/sim/locomotive.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace railmaster::sim;

TEST_CASE("locomotive registry parses fields and availability") {
    const auto reg = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "a", "name": "A", "fuel": "steam", "available_from": 1830, "available_until": 1850,
         "top_speed_mph": 30, "cost": 10000, "maintenance_per_year": 5000},
        {"key": "b", "name": "B", "fuel": "electric", "available_from": 2000,
         "top_speed_mph": 200, "cost": 1, "maintenance_per_year": 1, "grade_rating": 150},
        {"key": "c", "name": "C", "fuel": "diesel", "available_from": 1900,
         "top_speed_mph": 80, "cost": 1, "maintenance_per_year": 1, "scenario_only": true}
    ]})");
    const auto& a = reg.get(*reg.find("a"));
    CHECK(a.fuel == Fuel::Steam);
    CHECK(a.cost == Money::dollars(10000));
    CHECK(a.grade_rating == 100);
    CHECK_FALSE(a.available_in(1829));
    CHECK(a.available_in(1830));
    CHECK(a.available_in(1850));
    CHECK_FALSE(a.available_in(1851));

    const auto& b = reg.get(*reg.find("b"));
    CHECK(b.available_in(2999)); // no end year
    CHECK(b.grade_rating == 150);

    CHECK_FALSE(reg.get(*reg.find("c")).available_in(1950)); // scenario-only
}

TEST_CASE("locomotive registry rejects bad data") {
    CHECK_THROWS(LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "a", "name": "A", "fuel": "coal", "available_from": 1830,
         "top_speed_mph": 30, "cost": 1, "maintenance_per_year": 1}]})"));
    CHECK_THROWS(LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "a", "name": "A", "fuel": "steam", "available_from": 1850, "available_until": 1840,
         "top_speed_mph": 30, "cost": 1, "maintenance_per_year": 1}]})"));
}

TEST_CASE("shipped locomotive data loads") {
    std::ifstream in(RAILMASTER_DATA_DIR "/locomotives.json");
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const auto reg = LocomotiveRegistry::from_json(ss.str());
    CHECK(reg.all().size() == 35);
    const auto& planet = reg.get(*reg.find("planet_220"));
    CHECK(planet.top_speed_mph == 25);
    CHECK(planet.available_in(1830));
    CHECK(reg.get(*reg.find("e88")).top_speed_mph == 300);
}
