#include "railmaster/sim/random.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace railmaster::sim;

namespace {

// Rates of 365 a year: a carload a day.
GameData industry_data() {
    GameData d;
    d.balance.stock.founder_fortune = 13'000'000; // deep pockets for the merger test
    d.balance.economy.output_full_percent = 0;    // steady output, so accounts are exact
    d.cargo = CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 30},
        {"key": "iron", "name": "Iron", "base_price": 30},
        {"key": "steel", "name": "Steel", "base_price": 85}
    ]})");
    d.industries = IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 365},
        {"key": "iron_mine", "name": "Iron Mine", "kind": "raw", "outputs": ["iron"], "rate": 365},
        {"key": "steel_mill", "name": "Steel Mill", "kind": "processor", "rule": "all",
         "inputs": [{"cargo": "iron"}, {"cargo": "coal"}], "outputs": ["steel"], "rate": 365},
        {"key": "plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 365},
        {"key": "house", "name": "Houses", "kind": "house", "rate": 1, "inputs": [{"cargo": "steel"}]}
    ]})",
                                           d.cargo);
    return d;
}

World flat_world(bool cycle = false) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.business_cycle = cycle;
    cfg.chairman_can_be_fired = false;
    World w(cfg, industry_data());
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

IndustryTypeId type(const World& w, const char* key) { return *w.data().industries.find(key); }

SiteId add(World& w, const char* key, int cx, int cy) {
    return w.economy().add_site(w.data().industries, type(w, key), cx, cy);
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

Money line(const YearAccounts& y, Ledger l) { return y.lines[static_cast<std::size_t>(l)]; }

} // namespace

TEST_CASE("every industry keeps accounts: output, less inputs, labour and overhead, at base prices [I]") {
    World w = flat_world();
    const SiteId mine = add(w, "coal_mine", 3, 3);
    run_days(w, 31); // January, closed on 1 February
    const Site& s = w.economy().sites()[mine];
    REQUIRE(s.monthly_profit.size() == 1);
    // 31 carloads of coal at $30,000, less 20% labour and a month of $30,000 overhead.
    const Money revenue = Money::dollars(30'000) * 31;
    CHECK(s.monthly_profit.back() == revenue - revenue.scaled(20, 100) - Money::dollars(30'000).scaled(1, 12));
    CHECK(annual_profit(s) == s.monthly_profit.back() * 12);
    CHECK(s.utilisation_permille >= 990);
}

TEST_CASE("an industry's price is ten years' profit, or the floor for a poor one [C]") {
    const Balance::Industries& b = default_balance().industries;
    Site s;
    s.monthly_profit = {Money::dollars(50'000)};
    CHECK(industry_price(s, b) == Money::dollars(6'000'000)); // $600K a year x 10
    s.monthly_profit = {Money::dollars(-2'500)};
    CHECK(industry_price(s, b) == Money::dollars(300'000));
    s.level = 2;
    CHECK(industry_price(s, b) == Money::dollars(600'000));
    CHECK(industry_build_cost(b) == Money::dollars(450'000));       // 150% of an existing one's floor [C]
    CHECK(industry_upgrade_cost(s, b) == Money::dollars(450'000)); // half of building two more [I]
}

TEST_CASE("buying an industry: its profit comes to the company's books [C]") {
    World w = flat_world();
    const SiteId mine = add(w, "coal_mine", 3, 3);
    const SiteId plant = add(w, "plant", 8, 8);
    run_days(w, 31);
    const Money price = industry_price(w.economy().sites()[mine], w.data().balance.industries);
    w.company().post(Ledger::FreightRevenue, price); // a carload a day makes it a costly mine
    const Money cash = w.company().cash();
    const CommandResult r = w.execute(BuyIndustry{.site = mine});
    REQUIRE(r.ok);
    CHECK(r.cost == price);
    CHECK(w.company().cash() == cash - price);
    CHECK(w.company().industry_value() == price);
    CHECK(w.company().total_assets() == w.company().cash() + price);
    CHECK(w.economy().sites()[mine].owner == w.company().id());
    CHECK(w.company().this_year().industries_bought == price);

    CHECK(w.execute(BuyIndustry{.site = mine}).error.find("already owns") != std::string::npos);
    CHECK_FALSE(w.execute(BuyIndustry{.site = plant}).ok); // consumers are not for sale
    CHECK_FALSE(w.execute(BuyIndustry{.site = 99}).ok);

    run_days(w, 28); // February's accounts post on 1 March
    const YearAccounts& y = w.company().this_year();
    CHECK(line(y, Ledger::IndustryIncome) == Money::dollars(30'000) * 28);
    CHECK(line(y, Ledger::IndustryCosts) > Money{});
    CHECK(line(y, Ledger::IndustryIncome) - line(y, Ledger::IndustryCosts) == w.economy().sites()[mine].monthly_profit.back());

    // A rival cannot buy it from under you.
    const PlayerId rival = w.add_player_company("Rival", "R");
    CHECK(w.execute(BuyIndustry{.site = mine}, rival).error.find("belongs to") != std::string::npos);
}

TEST_CASE("building a processing plant: anywhere on open dry land, never a producer [C/D]") {
    World w = flat_world();
    add(w, "coal_mine", 3, 3);
    w.terrain().set_ground(10, 10, GroundType::Water);
    const auto build = [&](const char* key, int cx, int cy) {
        return w.execute(BuildIndustry{.type = type(w, key), .cx = cx, .cy = cy});
    };
    CHECK(build("coal_mine", 5, 5).error.find("only be bought") != std::string::npos);
    CHECK(build("steel_mill", 10, 10).error.find("dry land") != std::string::npos);
    CHECK(build("steel_mill", 3, 3).error.find("already built") != std::string::npos);
    CHECK(build("steel_mill", 25, 3).error.find("off the map") != std::string::npos);
    const Money cash = w.company().cash();
    const CommandResult r = build("steel_mill", 5, 5);
    REQUIRE(r.ok);
    CHECK(r.cost == Money::dollars(450'000));
    CHECK(w.company().cash() == cash - Money::dollars(450'000));
    const Site& mill = w.economy().sites()[r.created_id];
    CHECK(mill.owner == w.company().id());
    CHECK(mill.level == 1);

    // In a boom it costs more, like all construction.
    w.set_economic_state(EconomicState::Boom);
    CHECK(build("steel_mill", 6, 6).cost == Money::dollars(450'000).scaled(115, 100));
}

TEST_CASE("upgrading doubles a plant's capacity and overhead; an idle big plant loses more [D]") {
    World w = flat_world();
    const CommandResult r = w.execute(BuildIndustry{.type = type(w, "steel_mill"), .cx = 5, .cy = 5});
    REQUIRE(r.ok);
    const SiteId mill = r.created_id;
    CHECK_FALSE(w.execute(UpgradeIndustry{.site = 99}).ok);
    const CommandResult up = w.execute(UpgradeIndustry{.site = mill});
    REQUIRE(up.ok);
    CHECK(up.cost == Money::dollars(225'000)); // half of building another of level 1
    CHECK(w.economy().sites()[mill].level == 2);
    CHECK(w.economy().sites()[mill].owner_paid == Money::dollars(450'000 + 225'000));
    CHECK(w.execute(UpgradeIndustry{.site = mill}).cost == Money::dollars(450'000));
    CHECK(w.economy().sites()[mill].level == 4);

    // With no iron or coal it makes nothing and pays four levels of overhead.
    run_days(w, 31);
    CHECK(w.economy().sites()[mill].monthly_profit.back() == -Money::dollars(4 * 30'000).scaled(1, 12));
    CHECK(line(w.company().this_year(), Ledger::IndustryCosts) == Money::dollars(4 * 30'000).scaled(1, 12));

    // Supplied, it turns iron and coal into steel at a profit.
    add(w, "iron_mine", 5, 5);
    add(w, "coal_mine", 5, 5);
    run_days(w, 60);
    CHECK(w.economy().sites()[mill].monthly_profit.back() > Money{});

    const PlayerId rival = w.add_player_company("Rival", "R");
    CHECK(w.execute(UpgradeIndustry{.site = mill}, rival).error.find("your own") != std::string::npos);
}

TEST_CASE("unowned industries that lose money for five years may close [I]") {
    World w = flat_world();
    std::vector<SiteId> idle;
    for (int i = 0; i < 12; ++i) idle.push_back(add(w, "steel_mill", 1 + i, 2)); // no inputs anywhere
    const CommandResult mine = w.execute(BuildIndustry{.type = type(w, "steel_mill"), .cx = 1, .cy = 10});
    REQUIRE(mine.ok);
    run_days(w, 365 * 4 + 360); // four year-ends: nothing closes yet
    for (SiteId s : idle) CHECK_FALSE(w.economy().sites()[s].closed);
    run_days(w, 365 * 5);
    std::int32_t closed = 0;
    for (SiteId s : idle) closed += w.economy().sites()[s].closed;
    CHECK(closed > 0);
    CHECK(closed < 12);
    CHECK_FALSE(w.economy().sites()[mine.created_id].closed); // owned plants never close
    const Site& gone = *std::find_if(w.economy().sites().begin(), w.economy().sites().end(),
                                     [](const Site& s) { return s.closed; });
    CHECK_FALSE(w.execute(BuyIndustry{.site = gone.id}).ok);
}

TEST_CASE("a merger brings the target's industries with it [C]") {
    World w = flat_world();
    const SiteId mine = add(w, "coal_mine", 3, 3);
    run_days(w, 31);
    const PlayerId rival = w.add_player_company("Rival", "R");
    const CompanyId rival_co = *w.investors()[rival].chairs;
    w.company(rival_co).post(Ledger::FreightRevenue,
                             industry_price(w.economy().sites()[mine], w.data().balance.industries));
    REQUIRE(w.execute(BuyIndustry{.site = mine}, rival).ok);
    const Money value = w.company(rival_co).industry_value();
    REQUIRE(w.execute(SellShares{.blocks = 10, .company = rival_co}, rival).ok);
    REQUIRE(w.execute(BuyShares{.blocks = 301, .company = rival_co}).ok);
    w.company().post(Ledger::FreightRevenue, Money::dollars(20'000'000)); // to pay the other holders
    const CommandResult merged =
        w.execute(AttemptMerger{.target = rival_co, .offer_per_share = w.company(rival_co).share_price()});
    REQUIRE_MESSAGE(merged.ok, merged.error);
    CHECK(w.economy().sites()[mine].owner == w.company().id());
    CHECK(w.company().industry_value() == value);
}
