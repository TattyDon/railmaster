#include "legacy_scale.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <set>
#include <sstream>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

CargoRegistry test_cargo() {
    return CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 30},
        {"key": "iron", "name": "Iron", "base_price": 30},
        {"key": "steel", "name": "Steel", "base_price": 85},
        {"key": "wool", "name": "Wool", "base_price": 30},
        {"key": "cotton", "name": "Cotton", "base_price": 30},
        {"key": "clothing", "name": "Clothing", "base_price": 95},
        {"key": "corn", "name": "Corn", "base_price": 25, "decay_sensitivity": 3},
        {"key": "fertilizer", "name": "Fertilizer", "base_price": 80},
        {"key": "goods", "name": "Goods", "base_price": 170, "available_year": 1800},
        {"key": "mail", "name": "Mail", "class": "express", "decay_sensitivity": 10}
    ]})");
}

// Rates of 365 a year make the daily amounts round: one carload a day.
IndustryRegistry test_industries(const CargoRegistry& cargo) {
    return IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 365},
        {"key": "iron_mine", "name": "Iron Mine", "kind": "raw", "outputs": ["iron"], "rate": 365},
        {"key": "corn_farm", "name": "Corn Farm", "kind": "raw", "outputs": ["corn"], "boosters": ["fertilizer"], "rate": 365},
        {"key": "steel_mill", "name": "Steel Mill", "kind": "processor", "rule": "all",
         "inputs": [{"cargo": "iron"}, {"cargo": "coal"}], "outputs": ["steel"], "rate": 365},
        {"key": "textile_mill", "name": "Textile Mill", "kind": "processor",
         "inputs": [{"cargo": "cotton"}, {"cargo": "wool"}], "outputs": ["clothing"], "rate": 365},
        {"key": "electric_plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 365},
        {"key": "house", "name": "Houses", "kind": "house", "rate": 365,
         "inputs": [{"cargo": "goods"}, {"cargo": "coal", "until": 1910}]}
    ]})",
                                       cargo);
}

// The fixture's industries make a carload a day, about 150 times the
// spec's rates, so stock saturates prices over days rather than months.
Balance fast_saturation() {
    Balance b;
    b.economy.saturation_days = 30;
    b.economy.industry_saturation_days = 120;
    b.economy.supply_saturation_days = 60;
    return b;
}

struct Fixture {
    CargoRegistry cargo = test_cargo();
    IndustryRegistry ind = test_industries(cargo);
    Economy eco{20, 20, kKm, cargo, fast_saturation()};

    IndustryTypeId type(const char* key) const { return *ind.find(key); }
    CargoId c(const char* key) const { return *cargo.find(key); }
    void days(int n, std::int32_t year = 1850) {
        for (int i = 0; i < n; ++i) eco.step_day(cargo, ind, year);
    }
};

std::string read_data(const char* name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("industry registry resolves cargo and validates") {
    const auto cargo = test_cargo();
    const auto ind = test_industries(cargo);
    const auto& mill = ind.get(*ind.find("steel_mill"));
    CHECK(mill.kind == IndustryKind::Processor);
    CHECK(mill.rule == InputRule::All);
    CHECK(mill.inputs.size() == 2);
    CHECK(ind.get(*ind.find("corn_farm")).inputs.size() == 1); // the booster

    CHECK_THROWS(IndustryRegistry::from_json(
        R"({"industries": [{"key": "x", "name": "X", "kind": "raw", "outputs": ["gold"], "rate": 1}]})", cargo));
    CHECK_THROWS(IndustryRegistry::from_json(
        R"({"industries": [{"key": "x", "name": "X", "kind": "processor", "outputs": ["steel"], "rate": 1}]})", cargo));
    CHECK_THROWS(IndustryRegistry::from_json(
        R"({"industries": [{"key": "x", "name": "X", "kind": "raw", "outputs": ["coal"], "rate": 0}]})", cargo));
}

TEST_CASE("shipped industry data covers every freight cargo") {
    const auto cargo = CargoRegistry::from_json(read_data("cargo.json"));
    const auto ind = IndustryRegistry::from_json(read_data("industries.json"), cargo);
    std::set<std::string> produced, consumed;
    for (const auto& t : ind.all()) {
        for (CargoId o : t.outputs) produced.insert(cargo.get(o).key);
        for (const auto& i : t.inputs) consumed.insert(cargo.get(i.cargo).key);
    }
    // Known gap: no source names a consumer of diesel (refineries make it).
    for (const auto& c : cargo.all()) {
        if (c.cargo_class != CargoClass::Freight) continue;
        INFO(c.key);
        CHECK(produced.count(c.key) == 1);
        if (c.key != "diesel") CHECK(consumed.count(c.key) == 1);
    }
}

TEST_CASE("prices rise from producer to consumer") {
    Fixture f;
    f.eco.add_site(f.ind, f.type("coal_mine"), 4, 10);
    f.eco.add_site(f.ind, f.type("electric_plant"), 12, 10);
    f.days(400);
    const CargoId coal = f.c("coal");
    // At most 50% of $30K at the mine, less as unsold coal piles up there.
    CHECK(f.eco.price(coal, 4, 10) <= 15'000);
    CHECK(f.eco.price(coal, 4, 10) > 0);
    CHECK(f.eco.price(coal, 12, 10) > 30'000);  // above base at the plant
    for (int x = 5; x <= 12; ++x) {
        INFO("x = " << x);
        CHECK(f.eco.price(coal, x, 10) >= f.eco.price(coal, x - 1, 10));
    }
    // Far from the plant the pull has faded back to the neutral price.
    CHECK(f.eco.price(coal, 12, 0) < f.eco.price(coal, 12, 9));
}

TEST_CASE("cargo drifts from producer towards consumer without any trains") {
    Fixture f;
    f.eco.add_site(f.ind, f.type("coal_mine"), 4, 10);
    f.eco.add_site(f.ind, f.type("electric_plant"), 9, 10);
    f.days(300);
    const CargoId coal = f.c("coal");
    CHECK(f.eco.stock_milli(coal, 7, 10) > 0);                         // on its way east
    CHECK(f.eco.stock_milli(coal, 2, 10) == 0);                         // nothing goes west
    CHECK(f.eco.stock_milli(coal, 5, 10) > f.eco.stock_milli(coal, 3, 10));
}

TEST_CASE("a steel mill needs both iron and coal; a textile mill needs either input") {
    Fixture f;
    const SiteId mill = f.eco.add_site(f.ind, f.type("steel_mill"), 10, 10);
    const SiteId textile = f.eco.add_site(f.ind, f.type("textile_mill"), 5, 5);
    for (int d = 0; d < 10; ++d) {
        f.eco.add_stock(f.c("iron"), 10, 10, kMilli);
        f.eco.add_stock(f.c("wool"), 5, 5, kMilli);
        f.days(1);
    }
    CHECK(f.eco.sites()[mill].produced_milli == 0);
    CHECK(f.eco.sites()[textile].produced_milli > 5 * kMilli);

    for (int d = 0; d < 10; ++d) {
        f.eco.add_stock(f.c("iron"), 10, 10, kMilli);
        f.eco.add_stock(f.c("coal"), 10, 10, kMilli);
        f.days(1);
    }
    CHECK(f.eco.sites()[mill].produced_milli >= 9 * kMilli);
    CHECK(f.eco.stock_milli(f.c("steel"), 10, 10) > 0);
}

TEST_CASE("processors never exceed capacity") {
    Fixture f;
    const SiteId mill = f.eco.add_site(f.ind, f.type("steel_mill"), 10, 10);
    f.eco.add_stock(f.c("iron"), 10, 10, 40 * kMilli);
    f.eco.add_stock(f.c("coal"), 10, 10, 40 * kMilli);
    f.days(10);
    CHECK(f.eco.sites()[mill].produced_milli <= 10 * kMilli);
}

TEST_CASE("a supplied booster raises a farm's output") {
    Fixture f;
    // Without price-responsive output, so only the booster differs.
    Balance steady;
    steady.economy.output_full_percent = 0;
    Economy eco{20, 20, kKm, f.cargo, steady};
    const SiteId plain = eco.add_site(f.ind, f.type("corn_farm"), 3, 3);
    const SiteId fed = eco.add_site(f.ind, f.type("corn_farm"), 15, 15);
    for (int d = 0; d < 20; ++d) {
        eco.add_stock(f.c("fertilizer"), 15, 15, kMilli);
        eco.step_day(f.cargo, f.ind, 1850);
    }
    CHECK(eco.sites()[fed].produced_milli == eco.sites()[plain].produced_milli * 3 / 2);
}

TEST_CASE("unsold output lowers a producer's price, and it slows down, then recovers when served [C/I]") {
    Fixture f;
    const SiteId mine = f.eco.add_site(f.ind, f.type("coal_mine"), 3, 3);
    const CargoId coal = f.c("coal");
    f.days(30);
    // Middlemen carry its coal off as it is made: full pace.
    CHECK(f.eco.sites()[mine].pace_permille == 1000);
    // Coal piles up unsold (40 carloads, 40 days' output): the price falls
    // to about 30% of base and the mine slows to about two thirds.
    f.eco.add_stock(coal, 3, 3, 40'000);
    const std::int64_t before = f.eco.sites()[mine].produced_milli;
    f.days(1);
    CHECK(f.eco.price(coal, 3, 3) < 15'000 * 35 / 50);
    CHECK(f.eco.sites()[mine].pace_permille < 750);
    CHECK(f.eco.sites()[mine].produced_milli - before < kMilli * 3 / 4);
    // A railroad takes the stock away: the pace recovers.
    f.eco.take_stock(coal, 3, 3, 1'000'000);
    f.days(1);
    CHECK(f.eco.sites()[mine].pace_permille == 1000);
}

TEST_CASE("oversupply pushes a consumer's price down") {
    Fixture f;
    f.eco.add_site(f.ind, f.type("house"), 5, 5, 1);
    f.eco.add_site(f.ind, f.type("house"), 15, 15, 1);
    for (int d = 0; d < 30; ++d) {
        f.eco.add_stock(f.c("goods"), 15, 15, 10 * kMilli); // ten times what it uses
        f.days(1);
    }
    CHECK(f.eco.price(f.c("goods"), 15, 15) < f.eco.price(f.c("goods"), 5, 5) / 2);
}

TEST_CASE("demand that ends in a given year stops pulling prices") {
    Fixture f;
    f.eco.add_site(f.ind, f.type("house"), 10, 10, 5);
    f.days(50, 1900);
    const CargoId coal = f.c("coal");
    CHECK(f.eco.price(coal, 10, 10) > 30'000); // houses burn coal before 1910
    f.days(1, 1911);
    CHECK_FALSE(f.eco.active(coal));
}

TEST_CASE("express cargo is not priced by the field") {
    Fixture f;
    f.eco.add_site(f.ind, f.type("house"), 10, 10, 5);
    f.days(5);
    CHECK_FALSE(f.eco.active(f.c("mail")));
}

TEST_CASE("the economy is deterministic") {
    auto run = [] {
        Fixture f;
        Random rng(7);
        Terrain t(20, 20, 1000);
        f.eco.set_terrain(t);
        populate_economy(f.eco, f.cargo, f.ind, rng, 1850);
        f.days(100);
        std::vector<std::int32_t> out;
        for (const auto& c : f.cargo.all())
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 20; ++x) {
                    out.push_back(f.eco.price(c.id, x, y));
                    out.push_back(f.eco.stock_milli(c.id, x, y));
                }
        return out;
    };
    CHECK(run() == run());
}

TEST_CASE("a new world is populated with towns and industries on dry land") {
    GameData data;
    data.cargo = CargoRegistry::from_json(read_data("cargo.json"));
    data.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    data.industries = IndustryRegistry::from_json(read_data("industries.json"), data.cargo);
    WorldConfig cfg; // the shipped Small map: 256 x 256 cells of 0.5 mile, 128 x 128 economy nodes
    World w(cfg, std::move(data));
    const Economy& eco = w.economy();
    CHECK(eco.width() == 128);
    CHECK(eco.height() == 128);
    // 206 km across: 2.6 times the area of a 128 km map, so about 20 towns.
    CHECK(eco.area_permille() > 2500);
    CHECK(eco.towns().size() >= 15);
    CHECK(eco.sites().size() > 100);
    std::set<std::pair<int, int>> industry_cells;
    for (const Site& s : eco.sites()) {
        CHECK_FALSE(w.economy().water(s.cx, s.cy));
        const auto& t = w.data().industries.get(s.type);
        if (t.kind != IndustryKind::House) CHECK(industry_cells.insert({s.cx, s.cy}).second);
        // Nothing whose products do not exist yet (the default start is 1830).
        // (Consumers and houses are exempt: houses' only freight output,
        // waste, starts in 1990, and barracks produce only troops.)
        bool makes_something_available =
            t.outputs.empty() || t.kind == IndustryKind::House || t.kind == IndustryKind::Sink;
        for (CargoId o : t.outputs) makes_something_available |= w.data().cargo.get(o).available_year <= 1830;
        CHECK(makes_something_available);
    }
    CHECK(eco.active(*w.data().cargo.find("coal")));
}
