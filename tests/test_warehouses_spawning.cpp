#include "legacy_scale.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace railmaster::sim;

namespace {

GameData data() {
    GameData d;
    d.cargo = CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 30, "decay_sensitivity": 5},
        {"key": "rubber", "name": "Rubber", "base_price": 30, "available_year": 1900}
    ]})");
    d.industries = IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 24},
        {"key": "rubber_plantation", "name": "Rubber Plantation", "kind": "raw", "outputs": ["rubber"], "rate": 24},
        {"key": "plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 24},
        {"key": "warehouse", "name": "Warehouse", "kind": "warehouse", "rate": 365,
         "inputs": [{"cargo": "coal"}], "outputs": ["rubber"]}
    ]})",
                                           d.cargo);
    return d;
}

World flat_world(Date start = Date::from_ymd(1850, 1, 1), bool appear = true) {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = cfg.height_tiles = 30;
    cfg.populate = false;
    cfg.business_cycle = false;
    cfg.town_growth = false;
    cfg.industries_appear = appear;
    cfg.start_date = start;
    World w(cfg, data());
    for (int y = 0; y <= 30; ++y)
        for (int x = 0; x <= 30; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

IndustryTypeId type(const World& w, const char* key) { return *w.data().industries.find(key); }

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

std::int32_t open_of(const World& w, const char* key) {
    std::int32_t n = 0;
    for (const Site& s : w.economy().sites()) n += s.type == type(w, key) && !s.closed;
    return n;
}

} // namespace

TEST_CASE("a warehouse keeps nearby cargo from spoiling, and earns what it saves [D/C]") {
    World w = flat_world(Date::from_ymd(1850, 1, 1), false);
    const CommandResult built = w.execute(BuildIndustry{.type = type(w, "warehouse"), .cx = 10, .cy = 10});
    REQUIRE(built.ok); // before 1900, though it would also import rubber: it trades what exists
    REQUIRE(w.execute(SetPortMode{.site = built.created_id, .mode = PortMode::Supply}).ok); // keep the coal
    const CargoId coal = *w.data().cargo.find("coal");
    w.economy().add_stock(coal, 11, 11, 20'000); // within two cells
    w.economy().add_stock(coal, 20, 20, 20'000); // out of reach
    run_days(w, 30);
    const std::int32_t kept = w.economy().stock_milli(coal, 11, 11);
    const std::int32_t lost = w.economy().stock_milli(coal, 20, 20);
    MESSAGE("after 30 days: near the warehouse " << kept << ", far away " << lost);
    CHECK(kept > lost);
    CHECK(20'000 - kept < (20'000 - lost) / 3); // a quarter of the usual spoilage, give or take rounding
    run_days(w, 5); // the month's accounts post on 1 February
    const YearAccounts& y = w.company().this_year();
    CHECK(y.lines[static_cast<std::size_t>(Ledger::IndustryIncome)] > Money{});
    CHECK(y.lines[static_cast<std::size_t>(Ledger::IndustryCosts)] == Money::dollars(30'000).scaled(1, 12));
}

TEST_CASE("a warehouse trades like a port, in the mode its owner sets [D/C]") {
    World w = flat_world(Date::from_ymd(1901, 1, 1), false);
    const CommandResult built = w.execute(BuildIndustry{.type = type(w, "warehouse"), .cx = 10, .cy = 10});
    REQUIRE(built.ok);
    const SiteId wh = built.created_id;
    const CargoId coal = *w.data().cargo.find("coal"), rubber = *w.data().cargo.find("rubber");
    w.economy().add_stock(coal, 10, 10, 10'000);
    run_days(w, 5); // exchange: coal out, rubber in
    CHECK(w.economy().stock_milli(coal, 10, 10) < 10'000);
    CHECK(w.economy().stock_milli(rubber, 10, 10) > 0);

    REQUIRE(w.execute(SetPortMode{.site = wh, .mode = PortMode::Receive}).ok);
    const std::int32_t rubber_now = w.economy().stock_milli(rubber, 10, 10);
    run_days(w, 3);
    CHECK(w.economy().stock_milli(rubber, 10, 10) <= rubber_now); // no more imports
    CHECK(w.economy().sites()[wh].port_mode == PortMode::Receive);

    // Only the owner, and only warehouses.
    const PlayerId rival = w.add_player_company("Rival", "R");
    CHECK(w.execute(SetPortMode{.site = wh, .mode = PortMode::Supply}, rival).error.find("your own") != std::string::npos);
    const SiteId mine = w.economy().add_site(w.data().industries, type(w, "coal_mine"), 3, 3);
    CHECK_FALSE(w.execute(SetPortMode{.site = mine}).ok);
    // Warehouses can be upgraded like plants.
    CHECK(w.execute(UpgradeIndustry{.site = wh}).ok);
    CHECK(w.economy().sites()[wh].level == 2);
}

TEST_CASE("industries below the map's usual number appear, a year at a time [C/I]") {
    World w = flat_world();
    const Balance::MapGeneration& m = default_balance().map;
    while (w.date().year() < 1851)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    // One of each type in use (coal mines and power plants, not rubber yet).
    CHECK(open_of(w, "coal_mine") == 1);
    CHECK(open_of(w, "plant") == 1);
    CHECK(open_of(w, "rubber_plantation") == 0);
    CHECK(open_of(w, "warehouse") == 0); // built by companies only
    while (w.date().year() < 1860)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    // Up to the usual number (4 coal mines on a small map), then rarely more, never past the cap.
    CHECK(open_of(w, "coal_mine") >= m.raw_per_type);
    CHECK(open_of(w, "coal_mine") <= m.raw_per_type * m.max_count_multiple);
    for (const Site& s : w.economy().sites()) CHECK_FALSE(w.economy().water(s.cx, s.cy));
}

TEST_CASE("new kinds of industry appear once their cargo exists") {
    World w = flat_world(Date::from_ymd(1899, 6, 1));
    run_days(w, 200); // to the end of 1899
    CHECK(open_of(w, "rubber_plantation") == 0);
    while (w.date().year() < 1900)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    // Rubber exists from 1900: the year's review on 1 January brings the first plantation.
    CHECK(open_of(w, "rubber_plantation") == 1);
    const auto news = w.take_news();
    CHECK(std::any_of(news.begin(), news.end(),
                      [](const std::string& n) { return n.find("A new Rubber Plantation has opened") != std::string::npos; }));
    while (w.date().year() < 1901)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    CHECK(open_of(w, "rubber_plantation") == 2); // one a year until the usual number
}

TEST_CASE("industries can be held to the map they started with") {
    World w = flat_world(Date::from_ymd(1850, 1, 1), /*appear=*/false);
    run_days(w, 365 * 3);
    CHECK(w.economy().sites().empty());
}
