#include "railmaster/sim/cargo.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace railmaster::sim;

TEST_CASE("cargo registry parses entries and looks up by key") {
    const auto reg = CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 100},
        {"key": "milk", "name": "Milk", "base_price": 150, "decay_days": 30}
    ]})");
    REQUIRE(reg.all().size() == 2);
    CHECK(reg.find("milk") == CargoId{1});
    CHECK(reg.get(1).decay_days == 30);
    CHECK(reg.get(0).base_price == Money::dollars(100));
    CHECK_FALSE(reg.find("gold").has_value());
}

TEST_CASE("cargo registry rejects bad data") {
    CHECK_THROWS(CargoRegistry::from_json("{not json"));
    CHECK_THROWS(CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 1},
        {"key": "coal", "name": "Coal", "base_price": 1}]})"));
}

TEST_CASE("shipped cargo data loads") {
    std::ifstream in(RAILMASTER_DATA_DIR "/cargo.json");
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const auto reg = CargoRegistry::from_json(ss.str());
    CHECK(reg.all().size() > 0);
}
