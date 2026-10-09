#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace railmaster::sim;

namespace {

std::string read_data(const char* name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

World small_world(const Balance& b) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 20;
    cfg.populate = false;
    GameData d;
    d.balance = b;
    return World(cfg, std::move(d));
}

} // namespace

TEST_CASE("data/balance.json matches the compiled defaults") {
    // If this fails after changing a default, regenerate the file from
    // default_balance().to_json() (or edit it to match).
    CHECK(Balance::from_json(read_data("balance.json")) == default_balance());
}

TEST_CASE("balance: round trip, partial files and comments") {
    CHECK(Balance::from_json(default_balance().to_json()) == default_balance());
    CHECK(Balance::from_json("{}") == default_balance());

    const Balance b = Balance::from_json(R"({"_note": "x", "version": 1,
        "finance": {"_why": "test", "bond_face_value": 123, "bond_rate_bp": [1, 2, 3, 4, 5, 6, 7, 8]}})");
    CHECK(b.finance.bond_face_value == 123);
    CHECK(b.finance.bond_rate_bp[7] == 8);
    CHECK(b.finance.max_bonds == default_balance().finance.max_bonds); // untouched keys keep defaults
    CHECK(b.stock.salary_per_year == default_balance().stock.salary_per_year);
    CHECK_FALSE(b == default_balance());
}

TEST_CASE("balance: typos and wrong versions are errors") {
    CHECK_THROWS_AS(Balance::from_json(R"({"finance": {"bond_face_valu": 1}})"), std::runtime_error);
    CHECK_THROWS_AS(Balance::from_json(R"({"finanse": {}})"), std::runtime_error);
    CHECK_THROWS_AS(Balance::from_json(R"({"version": 2})"), std::runtime_error);
    CHECK_THROWS_AS(Balance::from_json(R"({"finance": {"bond_face_value": "lots"}})"), std::runtime_error);
    CHECK_THROWS_AS(Balance::from_json("[1, 2]"), std::runtime_error);
    CHECK_THROWS_AS(Balance::from_json("{"), std::runtime_error);
}

TEST_CASE("balance: the timeliness step is exp(-0.0023) in Q30") {
    CHECK(default_balance().decay_step_q30() == 1071275056);
    Balance none;
    none.freight.decay_ppm_per_sensitivity_day = 0;
    CHECK(none.decay_step_q30() == std::int64_t{1} << 30);
}

TEST_CASE("a world takes its numbers from its balance") {
    Balance b;
    b.stock.founder_fortune = 1'000'000;
    b.stock.founder_investment = 600'000;
    b.stock.outside_investment = 1'400'000;
    b.stock.founding_share_price_cents = 2'000; // $20 a share
    b.finance.bond_face_value = 100'000;
    b.finance.max_bonds = 1;
    World w = small_world(b);
    CHECK(w.company().cash() == Money::dollars(2'000'000));
    CHECK(w.company().shares_outstanding() == 100'000);
    CHECK(w.company().share_price() == Money::dollars(20));
    CHECK(w.investor().cash == Money::dollars(400'000));
    CHECK(w.investor().shares_in(0) == 30'000);

    REQUIRE(w.execute(IssueBond{}).ok);
    CHECK(w.company().debt() == Money::dollars(100'000));
    const CommandResult second = w.execute(IssueBond{});
    CHECK_FALSE(second.ok);
    CHECK(second.error.find("maximum of 1 bonds") != std::string::npos);

    // An explicit starting cash in the config still wins.
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.starting_cash = 5'000;
    GameData d;
    d.balance = b;
    const World w2(cfg, std::move(d));
    CHECK(w2.company().cash() == Money::dollars(5'000));
}

TEST_CASE("station costs and cargo prices follow the balance") {
    Balance b;
    b.stations.small_cost = 1;
    CHECK(station_cost(StationSize::Small, b) == Money::dollars(1));
    CHECK(station_cost(StationSize::Small) == Money::dollars(default_balance().stations.small_cost));
    const auto reg = CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 3}]})", 10);
    CHECK(reg.get(0).base_price == Money::dollars(30));
}
