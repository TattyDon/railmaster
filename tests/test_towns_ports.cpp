#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

GameData town_data() {
    GameData d;
    d.cargo = CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 30},
        {"key": "rubber", "name": "Rubber", "base_price": 30, "available_year": 1800},
        {"key": "passengers", "name": "Passengers", "class": "express", "fare_per_km": 500, "generation": 4}
    ]})");
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1}]})");
    d.industries = IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 365},
        {"key": "plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 365},
        {"key": "port", "name": "Port", "kind": "port", "rate": 365, "inputs": [{"cargo": "coal"}], "outputs": ["rubber"]},
        {"key": "house", "name": "Houses", "kind": "house", "rate": 1, "inputs": [{"cargo": "passengers"}],
         "outputs": ["passengers"]}
    ]})",
                                           d.cargo);
    return d;
}

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }

World flat_world(bool growth = true) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 30;
    cfg.populate = false;
    cfg.business_cycle = false;
    cfg.town_growth = growth;
    World w(cfg, town_data());
    for (int y = 0; y <= 30; ++y)
        for (int x = 0; x <= 30; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

// A town of 20 houses in four cells around (cx, cy); the first is at (10, 10).
void add_town(World& w, std::int32_t cx = 10, std::int32_t cy = 10) {
    const std::size_t t = w.economy().towns().size();
    w.economy().add_town({"Town " + std::to_string(t), cx, cy});
    const IndustryTypeId house = *w.data().industries.find("house");
    for (const auto& [dx, dy] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
        const SiteId s = w.economy().add_site(w.data().industries, house, cx + dx, cy + dy, 5);
        w.economy().town_mut(t).houses.push_back(s);
    }
}

// Track from the town to (20, 10) with a station at each end and a train.
void connect(World& w) {
    const auto free_at = [](int cx, int cy) { return TrackEnd{TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; };
    REQUIRE(w.execute(BuildTrack{.start = free_at(13, 10), .end = free_at(20, 10)}).ok);
    const TrackNetwork& net = w.railway().track();
    const auto node_at = [&](int cx, int cy) {
        const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
        return TrackEnd{TrackEnd::Kind::Node, n, 0, net.node(n).pos};
    };
    const CommandResult a = w.execute(BuildStation{.at = node_at(13, 10)});
    const CommandResult b = w.execute(BuildStation{.at = node_at(20, 10)});
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(w.railway().station(a.created_id).town == std::size_t{0});
    REQUIRE(w.execute(BuyTrain{.loco = 0, .cars = 1, .route = {a.created_id, b.created_id}}).ok);
}

void next_month(World& w) {
    const auto month = w.date().month();
    while (w.date().month() == month)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

} // namespace

TEST_CASE("towns are rated one to five stars by houses [D/I]") {
    const Balance::Towns& b = default_balance().towns;
    CHECK(town_stars(5, b) == 1);
    CHECK(town_stars(19, b) == 1);
    CHECK(town_stars(20, b) == 2);
    CHECK(town_stars(50, b) == 3);
    CHECK(town_stars(120, b) == 4);
    CHECK(town_stars(300, b) == 5);
}

TEST_CASE("an unserved town barely grows [D/C]") {
    World w = flat_world();
    add_town(w);
    for (int m = 0; m < 24; ++m) next_month(w);
    // 0.8% a year, cut to 30% for no railway: 20 houses x 2 per mille / 12 a month.
    CHECK(w.economy().town_houses(0) == 20);
    CHECK(w.economy().towns()[0].growth_milli == 24 * (20 * 2 / 12));
}

TEST_CASE("service makes a town grow, near its stations [D/I]") {
    World w = flat_world();
    add_town(w);
    connect(w);
    for (int m = 0; m < 12; ++m) {
        // $150,000 a house a year in fares: the rate tops out at 15% a year.
        w.economy().town_mut(0).express_this_month += Money::dollars(150'000 * 20 / 12);
        next_month(w);
    }
    const std::int64_t houses = w.economy().town_houses(0);
    MESSAGE("houses after a year of good service: " << houses);
    CHECK(houses >= 23); // 20 x 15% = 3 a year, plus whatever the train itself earned
    CHECK(houses <= 25);
    // New houses gather towards the station at (13, 10), never past the town's radius.
    for (const SiteId s : w.economy().towns()[0].houses) {
        const Site& h = w.economy().sites()[s];
        CHECK(h.level <= default_balance().towns.max_houses_per_cell);
        CHECK(std::max(std::abs(h.cx - 10), std::abs(h.cy - 10)) <= default_balance().towns.max_radius_cells);
    }
    const Site& nearest = w.economy().sites()[w.economy().towns()[0].houses.back()];
    CHECK(nearest.cx >= 11);

    World still = flat_world(/*growth=*/false);
    add_town(still);
    for (int m = 0; m < 12; ++m) {
        still.economy().town_mut(0).express_this_month += Money::dollars(150'000 * 20 / 12);
        next_month(still);
    }
    CHECK(still.economy().town_houses(0) == 20);
}

TEST_CASE("income at a town's stations is counted towards its growth") {
    World w = flat_world();
    add_town(w);
    add_town(w, 21, 10); // passengers need somewhere to go
    connect(w);
    for (int m = 0; m < 3; ++m) next_month(w);
    const Town& t = w.economy().towns()[0];
    Money express;
    for (const Money& m : t.express_months) express += m;
    CHECK(express > Money{}); // the train's passenger fares at the town's station
}

TEST_CASE("ports export what they receive and import what they supply, by mode [C]") {
    World w = flat_world(false);
    const IndustryRegistry& ind = w.data().industries;
    const IndustryTypeId port = *ind.find("port");
    const CargoId coal = *w.data().cargo.find("coal"), rubber = *w.data().cargo.find("rubber");
    const SiteId exchange = w.economy().add_site(ind, port, 5, 5);
    const SiteId receive = w.economy().add_site(ind, port, 15, 5);
    const SiteId supply = w.economy().add_site(ind, port, 25, 5);
    w.economy().site_mut(receive).port_mode = PortMode::Receive;
    w.economy().site_mut(supply).port_mode = PortMode::Supply;
    for (const auto& [x, y] : {std::pair{5, 5}, {15, 5}, {25, 5}}) w.economy().add_stock(coal, x, y, 10'000);
    for (int d = 0; d < 5; ++d) w.economy().step_day(w.data().cargo, ind, 1850);
    // Exchange and receive ports take the coal away; the supply port leaves it.
    CHECK(w.economy().stock_milli(coal, 5, 5) < 10'000);
    CHECK(w.economy().stock_milli(coal, 15, 5) < 10'000);
    CHECK(w.economy().stock_milli(coal, 25, 5) > 9'000);
    // Exchange and supply ports bring rubber in; the receive port does not.
    CHECK(w.economy().stock_milli(rubber, 5, 5) > 0);
    CHECK(w.economy().stock_milli(rubber, 15, 5) == 0);
    CHECK(w.economy().stock_milli(rubber, 25, 5) > 0);
    // A receiving port pays consumer prices for what it exports.
    CHECK(w.economy().price(coal, 15, 5) > w.economy().price(coal, 25, 5));
    (void)exchange;
    (void)supply;
}

TEST_CASE("unowned ports and consumers expand when demand fills them [C/I]") {
    World w = flat_world(false);
    const IndustryRegistry& ind = w.data().industries;
    const SiteId plant = w.economy().add_site(ind, *ind.find("plant"), 5, 5);
    const SiteId idle = w.economy().add_site(ind, *ind.find("plant"), 20, 20);
    // A mine in the same cell keeps the plant fully supplied.
    w.economy().add_site(ind, *ind.find("coal_mine"), 5, 5);
    while (w.date().year() < 1831)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    CHECK(w.economy().sites()[plant].level == 2);
    CHECK(w.economy().sites()[idle].level == 1);
    const auto news = w.take_news();
    CHECK(std::any_of(news.begin(), news.end(), [](const std::string& n) { return n.find("expanded") != std::string::npos; }));
}

namespace {

std::string read_data(const char* name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("new maps get ports on the coast") {
    GameData d;
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    WorldConfig cfg;
    World w(cfg, std::move(d));
    std::int32_t ports = 0;
    for (const Site& s : w.economy().sites()) {
        if (w.data().industries.get(s.type).kind != IndustryKind::Port) continue;
        ++ports;
        bool coast = false;
        for (const auto& [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const int x = s.cx + dx, y = s.cy + dy;
            if (x >= 0 && y >= 0 && x < 128 && y < 128) coast |= w.terrain().ground(x, y) == GroundType::Water;
        }
        CHECK(coast);
        CHECK(w.terrain().ground(s.cx, s.cy) != GroundType::Water);
    }
    CHECK(ports == default_balance().map.ports);
}
