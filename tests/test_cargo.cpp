#include "railmaster/sim/cargo.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace railmaster::sim;

TEST_CASE("cargo registry parses entries and looks up by key") {
    const auto reg = CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 100},
        {"key": "milk", "name": "Milk", "base_price": 150, "decay_sensitivity": 10},
        {"key": "mail", "name": "Mail", "class": "express", "decay_sensitivity": 10}
    ]})");
    REQUIRE(reg.all().size() == 3);
    CHECK(reg.find("milk") == CargoId{1});
    CHECK(reg.get(1).decay_sensitivity == 10);
    CHECK(reg.get(0).cargo_class == CargoClass::Freight);
    CHECK(reg.get(2).cargo_class == CargoClass::Express);
    CHECK(reg.get(2).base_price == Money{});
    CHECK(reg.get(0).base_price == Money::dollars(100));
    CHECK_FALSE(reg.find("gold").has_value());
}

TEST_CASE("cargo registry rejects bad data") {
    CHECK_THROWS(CargoRegistry::from_json("{not json"));
    CHECK_THROWS(CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 1},
        {"key": "coal", "name": "Coal", "base_price": 1}]})"));
    CHECK_THROWS(CargoRegistry::from_json(R"({"cargo": [
        {"key": "x", "name": "X", "class": "bulk"}]})"));
    CHECK_THROWS(CargoRegistry::from_json(R"({"cargo": [
        {"key": "x", "name": "X", "decay_sensitivity": 11}]})"));
}

TEST_CASE("shipped cargo data loads") {
    std::ifstream in(RAILMASTER_DATA_DIR "/cargo.json");
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const auto reg = CargoRegistry::from_json(ss.str());
    CHECK(reg.all().size() == 41);
    REQUIRE(reg.find("steel").has_value());
    CHECK(reg.get(*reg.find("steel")).available_year == 1856);
    CHECK(reg.get(*reg.find("passengers")).cargo_class == CargoClass::Express);
}
