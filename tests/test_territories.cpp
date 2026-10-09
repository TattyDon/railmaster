#include "legacy_scale.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <set>
#include <sstream>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }
TrackEnd free_at(int cx, int cy) { return {TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; }

std::string read_data(const char* name) {
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

// A flat 40 x 20 km map: x below 20 is Westland (open), the rest Eastmark
// ($1M, the given modifiers).
World split_world(Territory east, GameData d = {}, std::int32_t rivals = 0) {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = 40;
    cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.industries_appear = false;
    cfg.business_cycle = false;
    cfg.rivals = rivals;
    World w(cfg, std::move(d));
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 40; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    TerritoryMap map;
    map.width = 40;
    map.height = 20;
    map.territories = {Territory{.name = "Westland"}, std::move(east)};
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x) map.cells.push_back(x < 20 ? 0 : 1);
    w.set_territories(std::move(map));
    return w;
}

Territory eastmark() { return Territory{.name = "Eastmark", .access_cost = Money::dollars(1'000'000)}; }

TrackEnd node_at(const World& w, int cx, int cy) {
    const TrackNetwork& net = w.railway().track();
    const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
    return {TrackEnd::Kind::Node, n, 0, net.node(n).pos};
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

} // namespace

TEST_CASE("generated territories cover the map; the first town's is free [C/I]") {
    const Balance::MapGeneration& b = default_balance().map;
    Random rng(4);
    const TerritoryMap m = generate_territories(64, 48, 5, 10, 10, rng, b);
    REQUIRE(m.territories.size() == 5);
    std::set<TerritoryId> seen;
    for (TerritoryId t : m.cells) {
        CHECK(t < 5);
        seen.insert(t);
    }
    CHECK(seen.size() >= 4); // seeds can coincide, but rarely
    const TerritoryId home = m.at(10, 10);
    CHECK(m.territories[home].open());
    for (std::size_t i = 0; i < m.territories.size(); ++i) {
        const Territory& t = m.territories[i];
        if (i == home) continue;
        const bool big = t.access_cost == Money::dollars(b.territory_access_big);
        const bool in_range = t.access_cost >= Money::dollars(b.territory_access_min) &&
                              t.access_cost <= Money::dollars(b.territory_access_max);
        CHECK(big != in_range);
        CHECK(t.station_cost_percent >= 100);
        CHECK(t.overhead_percent <= 100 + b.territory_overhead_max_percent);
    }
    Random again(4);
    CHECK(generate_territories(64, 48, 5, 10, 10, again, b).cells == m.cells);

    // A new world asked for territories gets them, with the first town at home.
    WorldConfig cfg;
    cfg.territories = 6;
    World w(cfg, shipped());
    REQUIRE(w.territories().territories.size() == 6);
    const Town& first = w.economy().towns().front();
    CHECK(w.territories().territories[w.territory_at(w.economy().node_centre(first.cx, first.cy))].open());
}

TEST_CASE("building needs access rights; buying them is a territory fee [D]") {
    World w = split_world(eastmark());
    CHECK(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(15, 10)}).ok); // all in Westland
    const CommandResult across = w.execute(BuildTrack{.start = node_at(w, 15, 10), .end = free_at(30, 10)});
    CHECK(across.error.find("Eastmark") != std::string::npos);
    const Money cash = w.company().cash();
    CHECK(w.execute(BuyTerritoryAccess{.territory = 0}).error.find("open") != std::string::npos);
    const CommandResult bought = w.execute(BuyTerritoryAccess{.territory = 1});
    REQUIRE(bought.ok);
    CHECK(w.company().cash() == cash - Money::dollars(1'000'000));
    CHECK(w.company().this_year().lines[static_cast<std::size_t>(Ledger::TerritoryFees)] == Money::dollars(1'000'000));
    CHECK(w.execute(BuyTerritoryAccess{.territory = 1}).error.find("already") != std::string::npos);
    REQUIRE(w.execute(BuildTrack{.start = node_at(w, 15, 10), .end = free_at(30, 10)}).ok);
    CHECK(w.execute(BuildStation{.at = node_at(w, 30, 10)}).ok);

    // A rival without access may not build there, even next to the player.
    const PlayerId rival = w.add_player_company("Rival", "R");
    CHECK(w.execute(BuildTrack{.start = free_at(25, 15), .end = free_at(35, 15)}, rival).error.find("Eastmark") !=
          std::string::npos);
}

TEST_CASE("territory modifiers: station cost, overhead and credit grades [C]") {
    Territory east = eastmark();
    east.station_cost_percent = 120;
    east.overhead_percent = 150;
    east.credit_grades = 1;
    World w = split_world(east);
    const CreditRating before = w.company().credit_rating();
    REQUIRE(w.execute(BuyTerritoryAccess{.territory = 1}).ok);
    // One grade better (A+ stays A+).
    CHECK(static_cast<int>(w.company().credit_rating()) == std::max(0, static_cast<int>(before) - 1));

    // Stations in Eastmark cost 20% more.
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(35, 10)}).ok);
    const Money west = w.execute(BuildStation{.at = node_at(w, 5, 10)}).cost;
    const Money eastern = w.execute(BuildStation{.at = node_at(w, 35, 10)}).cost;
    CHECK(eastern == west.scaled(120, 100));

    // Track upkeep is weighted by where the track lies: half of this line is
    // in Eastmark at 150%, so about 125% overall; buildings, one station
    // each side, likewise.
    const Money track = w.company().track_value();
    run_days(w, 31);
    const Money upkeep = w.company().this_year().lines[static_cast<std::size_t>(Ledger::TrackUpkeep)];
    const Money plain = track.scaled(200, 10000 * 12);
    CHECK(upkeep > plain.scaled(120, 100));
    CHECK(upkeep < plain.scaled(130, 100));
}

TEST_CASE("closed borders stop middlemen and price signals [C]") {
    Territory east = eastmark();
    east.closed_border = true;
    World w = split_world(east);
    const Economy& eco = w.economy();
    CHECK(eco.east_weight(19, 5) == 0); // across the border
    CHECK(eco.east_weight(18, 5) > 0);  // within Westland
    CHECK(eco.south_weight(25, 5) > 0); // within Eastmark
    // Terrain refreshes keep the border shut.
    w.terrain().set_corner_height(3, 3, 12);
    run_days(w, 1);
    CHECK(w.economy().east_weight(19, 5) == 0);
}

TEST_CASE("a merger brings the target's access rights") {
    Company a("A", Money::dollars(1'000'000), 1850, default_balance(), 0);
    Company b("B", Money::dollars(1'000'000), 1850, default_balance(), 1);
    b.grant_access(3, 1);
    a.grant_access(2, 0);
    a.absorb(b);
    CHECK(a.has_access(2));
    CHECK(a.has_access(3));
    CHECK(b.access().empty());
}

TEST_CASE("rivals buy access to the territories their lines cross [I]") {
    GameData d = shipped();
    d.tycoons = TycoonRegistry::from_json(R"({"tycoons": [{"key": "t", "name": "T", "company": "T Lines",
        "expansion": 100, "leverage": 50}]})");
    World w = split_world(Territory{.name = "Eastmark", .access_cost = Money::dollars(100'000)}, std::move(d), 1);
    // Mines in Westland, the power plant in Eastmark.
    const auto add = [&](const char* key, int cx, int cy) {
        w.economy().add_site(w.data().industries, *w.data().industries.find(key), cx, cy);
    };
    add("coal_mine", 8, 10);
    add("coal_mine", 8, 11);
    add("electric_plant", 28, 10);
    run_days(w, 2 * 365);
    const CompanyId co = *w.investors()[w.rivals()[0].player].chairs;
    CHECK(w.company(co).has_access(1));
    std::int32_t east_stations = 0;
    for (const Station& s : w.railway().stations())
        if (s.owner == co && w.territory_at(w.railway().track().node(s.node).pos) == 1) ++east_stations;
    CHECK(east_stations >= 1);
}
