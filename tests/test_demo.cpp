#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/freight.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace railmaster::sim;

namespace {

std::string read_data(const std::string& name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData shipped() {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    return d;
}

} // namespace

TEST_CASE("the demo network runs the fastest steam or diesel engine of its year") {
    const GameData data = shipped();
    for (const auto& [year, expected] : {std::pair{1900, "Eight Wheeler 4-4-0"}, std::pair{1950, "Mallard 4-6-2"}}) {
        CAPTURE(year);
        WorldConfig cfg;
        cfg.seed = 1;
        cfg.start_date = Date::from_ymd(year, 1, 1);
        World w(cfg, data);
        (void)build_demo_network(w);
        REQUIRE_FALSE(w.railway().trains().empty());
        for (const Train& t : w.railway().trains()) {
            const LocomotiveType& l = data.locomotives.get(t.loco);
            CHECK(l.fuel != Fuel::Electric); // its track is not electrified
            CHECK(l.name == expected);
        }
    }
}

TEST_CASE("the demo network can be afforded on every calibration map") {
    const GameData data = shipped();
    for (const std::uint64_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
        CAPTURE(seed);
        WorldConfig cfg;
        cfg.seed = seed;
        World w(cfg, data);
        CHECK_NOTHROW((void)build_demo_network(w));
    }
}

TEST_CASE("towns make more passengers and mail in later eras (§8.3 [I])") {
    Balance b;
    b.express.era_volume_percent = {{1830, 100}, {1950, 160}};
    CHECK(express_era_percent(1800, b) == 100);
    CHECK(express_era_percent(1830, b) == 100);
    CHECK(express_era_percent(1890, b) == 130);
    CHECK(express_era_percent(1950, b) == 160);
    CHECK(express_era_percent(2100, b) == 160);
}

TEST_CASE("an era factor scales the passengers waiting at a station") {
    // The same game with the factor at 100% and at 200%: about twice the
    // passengers waiting.
    auto waiting = [](std::int32_t percent) {
        GameData data = shipped();
        data.balance.express.era_volume_percent = {{1830, percent}};
        WorldConfig cfg;
        cfg.seed = 1;
        World w(cfg, data);
        (void)build_demo_network(w);
        for (int i = 0; i < 5 * World::kTicksPerDay; ++i) w.tick();
        std::int64_t milli = 0;
        for (const Station& s : w.railway().stations()) {
            for (const ExpressWaiting& e : s.express) milli += e.milli;
        }
        return milli;
    };
    const std::int64_t one = waiting(100), two = waiting(200);
    MESSAGE("waiting after 5 days: x1 " << one << ", x2 " << two);
    CHECK(one > 0);
    CHECK(two > one * 3 / 2);
}
