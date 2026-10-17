#include "legacy_scale.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }

GameData data() {
    GameData d;
    d.cargo = CargoRegistry::from_json(R"({"cargo": [
        {"key": "passengers", "name": "Passengers", "class": "express", "fare_per_km": 500, "generation": 4,
         "decay_sensitivity": 9},
        {"key": "mail", "name": "Mail", "class": "express", "fare_per_km": 700, "generation": 2,
         "decay_sensitivity": 10}
    ]})");
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1}]})");
    d.industries = IndustryRegistry::from_json(R"({"industries": [
        {"key": "house", "name": "Houses", "kind": "house", "rate": 10,
         "inputs": [{"cargo": "passengers"}, {"cargo": "mail"}], "outputs": ["passengers", "mail"]}
    ]})",
                                           d.cargo);
    return d;
}

// Two towns 15 km apart, joined by a line with a station in each and a train.
struct Line {
    World w;
    StationId west = 0, east = 0;

    Line() : w(make()) {
        const IndustryTypeId house = *w.data().industries.find("house");
        for (const auto& [x, name] : {std::pair{5, "West"}, {20, "East"}}) {
            const std::size_t t = w.economy().towns().size();
            w.economy().add_town({name, x, 10});
            for (int dy = 0; dy < 2; ++dy) {
                const SiteId s = w.economy().add_site(w.data().industries, house, x, 10 + dy, 6);
                w.economy().town_mut(t).houses.push_back(s);
            }
        }
        const auto free_at = [](int cx, int cy) { return TrackEnd{TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; };
        REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(20, 10)}).ok);
        const TrackNetwork& net = w.railway().track();
        const auto node_at = [&](int cx, int cy) {
            const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
            return TrackEnd{TrackEnd::Kind::Node, n, 0, net.node(n).pos};
        };
        west = w.execute(BuildStation{.at = node_at(5, 10)}).created_id;
        east = w.execute(BuildStation{.at = node_at(20, 10)}).created_id;
        REQUIRE(w.execute(BuyTrain{.loco = 0, .cars = 4, .route = {west, east}}).ok);
    }
    static World make() {
        WorldConfig cfg;
        testing::legacy_scale(cfg);
        cfg.width_tiles = cfg.height_tiles = 30;
        cfg.populate = false;
        cfg.business_cycle = false;
        cfg.town_growth = false;
        cfg.industries_appear = false;
        World w(cfg, data());
        for (int y = 0; y <= 30; ++y)
            for (int x = 0; x <= 30; ++x) w.terrain().set_corner_height(x, y, 10);
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 30; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
        return w;
    }
    MapPoint station_pos(StationId s) const { return w.railway().track().node(w.railway().station(s).node).pos; }
    CommandResult build(StationBuildingType type, MapPoint p, PlayerId who = kHumanPlayer) {
        return w.execute(BuildStationBuilding{.type = type, .pos = p}, who);
    }
    void run_days(int days) {
        for (int d = 0; d < days; ++d)
            for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    }
    Money income(CompanyId c) const {
        return w.company(c).this_year().lines[static_cast<std::size_t>(Ledger::StationBuildingIncome)];
    }
};

MapPoint offset(MapPoint p, std::int64_t dx_m) { return {p.x_mm + dx_m * 1000, p.y_mm}; }

} // namespace

TEST_CASE("station buildings go on dry land near any station, and cost money [C/I]") {
    Line l;
    const MapPoint p = offset(l.station_pos(l.west), 200);
    const Money cash = l.w.company().cash();
    const CommandResult r = l.build(StationBuildingType::Hotel, p);
    REQUIRE(r.ok);
    CHECK(r.cost == Money::dollars(100'000));
    CHECK(l.w.company().cash() == cash - Money::dollars(100'000));
    CHECK(l.w.company().building_value() > Money{});
    CHECK(l.w.railway().station_buildings().back().owner == l.w.company().id());
    CHECK(l.build(StationBuildingType::Hotel, cell_centre(12, 25)).error.find("near a station") != std::string::npos);
    l.w.terrain().set_ground(5, 11, GroundType::Water);
    CHECK(l.build(StationBuildingType::Tavern, cell_centre(5, 11)).error.find("dry land") != std::string::npos);
    // A rival may build next to your station.
    const PlayerId rival = l.w.add_player_company("Rival", "R");
    CHECK(l.build(StationBuildingType::Restaurant, offset(l.station_pos(l.west), -300), rival).ok);
}

TEST_CASE("restaurants and taverns earn from passengers; post offices earn nothing [C]") {
    Line l;
    REQUIRE(l.build(StationBuildingType::Restaurant, offset(l.station_pos(l.west), 300)).ok);
    REQUIRE(l.build(StationBuildingType::Tavern, offset(l.station_pos(l.east), 300)).ok);
    REQUIRE(l.build(StationBuildingType::PostOffice, offset(l.station_pos(l.east), -300)).ok);
    l.run_days(150);
    const Money earned = l.income(l.w.company().id());
    MESSAGE("station buildings earned $" << earned.whole_dollars() << " in five months");
    CHECK(earned > Money{});
    CHECK(earned < Money::dollars(100'000)); // small beside the railway itself [C]
}

TEST_CASE("a station's trade is shared, the nearest building taking the most [D/C]") {
    Line near_only, both;
    const MapPoint at = near_only.station_pos(near_only.west);
    REQUIRE(near_only.build(StationBuildingType::Restaurant, offset(at, 100)).ok);
    REQUIRE(both.build(StationBuildingType::Restaurant, offset(at, 100)).ok);
    const PlayerId rival = both.w.add_player_company("Rival", "R");
    const CompanyId rival_co = *both.w.investors()[rival].chairs;
    REQUIRE(both.build(StationBuildingType::Restaurant, offset(at, 400), rival).ok);
    near_only.run_days(120);
    both.run_days(120);
    const Money alone = near_only.income(0), shared = both.income(0), theirs = both.income(rival_co);
    MESSAGE("alone $" << alone.whole_dollars() << ", sharing $" << shared.whole_dollars() << " vs the rival's $"
                      << theirs.whole_dollars());
    REQUIRE(alone > Money{});
    // Adding a competitor does not grow the market: the two split what one had.
    CHECK(shared + theirs <= alone + Money::dollars(1));
    CHECK(shared > theirs * 10); // 100 m against 400 m: 1/d² weights about 17 to 1
}

TEST_CASE("a hotel keeps passengers waiting longer, and a post office mail [D]") {
    Line plain, served;
    REQUIRE(served.build(StationBuildingType::Hotel, offset(served.station_pos(served.west), 200)).ok);
    REQUIRE(served.build(StationBuildingType::PostOffice, offset(served.station_pos(served.west), -200)).ok);
    // Watch the loads waiting at the west station build up and dwindle. The
    // train empties the pools every few days, so add up what waits each day.
    const auto waiting = [](const Line& l, const char* key) {
        const CargoId c = *l.w.data().cargo.find(key);
        std::int64_t n = 0;
        for (const ExpressWaiting& e : l.w.railway().station(l.west).express)
            if (e.cargo == c) n += e.milli;
        return n;
    };
    std::int64_t pax_plain = 0, pax_served = 0, mail_plain = 0, mail_served = 0;
    for (int d = 0; d < 20; ++d) {
        plain.run_days(1);
        served.run_days(1);
        pax_plain += waiting(plain, "passengers");
        pax_served += waiting(served, "passengers");
        mail_plain += waiting(plain, "mail");
        mail_served += waiting(served, "mail");
    }
    CHECK(pax_served > pax_plain);
    CHECK(mail_served > mail_plain);
}
