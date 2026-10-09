#include "railmaster/sim/ai.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }

GameData coal_data() {
    GameData d;
    d.cargo = CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})");
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1}]})");
    d.industries = IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 365},
        {"key": "electric_plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 365}
    ]})",
                                           d.cargo);
    return d;
}

World flat_world(GameData data) {
    WorldConfig cfg;
    cfg.width_tiles = 60;
    cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.business_cycle = false;
    World w(cfg, std::move(data));
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 60; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 60; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

TrackEnd free_at(int cx, int cy) { return {TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; }

TrackEnd node_at(const World& w, int cx, int cy) {
    const TrackNetwork& net = w.railway().track();
    const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
    return {TrackEnd::Kind::Node, n, 0, net.node(n).pos};
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

std::string read_data(const char* name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData shipped_data() {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    return d;
}

} // namespace

TEST_CASE("each company owns what it builds, and builds stations only on its own track") {
    World w = flat_world(coal_data());
    const PlayerId rival = w.add_player_company("Rival Lines", "A. Rival");
    const CompanyId rival_co = *w.investors()[rival].chairs;
    CHECK(rival_co == 1);
    CHECK(w.companies().size() == 2);
    CHECK(w.company(rival_co).name() == "Rival Lines");

    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(30, 10)}).ok);
    const Money rival_cash = w.company(rival_co).cash();
    // The rival joins the player's track and carries on east.
    REQUIRE(w.execute(BuildTrack{.start = node_at(w, 30, 10), .end = free_at(50, 10)}, rival).ok);
    CHECK(w.company(rival_co).cash() < rival_cash);
    CHECK(w.company(rival_co).track_value() > Money{});
    for (const TrackEdge& e : w.railway().track().edges()) {
        const bool east = w.railway().track().node(e.a).pos.x_mm >= cell_centre(30, 10).x_mm &&
                          w.railway().track().node(e.b).pos.x_mm >= cell_centre(30, 10).x_mm;
        CHECK(e.owner == (east ? rival_co : CompanyId{0}));
    }

    // No station on someone else's track; fine on your own, or where yours meets it.
    CHECK_FALSE(w.execute(BuildStation{.at = node_at(w, 5, 10)}, rival).ok);
    CHECK(w.execute(BuildStation{.at = node_at(w, 50, 10)}, rival).ok);
    CHECK(w.execute(BuildStation{.at = node_at(w, 30, 10)}, rival).ok);
    CHECK_FALSE(w.execute(BuildServiceBuilding{.at = node_at(w, 5, 10)}, rival).ok);
    CHECK(w.railway().stations().back().owner == rival_co);

    // A player with no company can trade but not build.
    CHECK_FALSE(w.execute(BuildTrack{.start = free_at(5, 5), .end = free_at(9, 5)}, PlayerId{7}).ok);
}

TEST_CASE("running on a rival's track pays the owner its share of the income [D]") {
    World w = flat_world(coal_data());
    const IndustryRegistry& ind = w.data().industries;
    w.economy().add_site(ind, *ind.find("coal_mine"), 5, 10);
    w.economy().add_site(ind, *ind.find("electric_plant"), 50, 10);
    const PlayerId rival = w.add_player_company("Rival Lines", "A. Rival");
    const CompanyId rival_co = *w.investors()[rival].chairs;

    // The player owns the 25 km from the mine; the rival the 20 km to the plant.
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(30, 10)}).ok);
    const CommandResult mine = w.execute(BuildStation{.at = node_at(w, 5, 10), .size = StationSize::Small});
    REQUIRE(mine.ok);
    REQUIRE(w.execute(BuildTrack{.start = node_at(w, 30, 10), .end = free_at(50, 10)}, rival).ok);
    const CommandResult plant = w.execute(BuildStation{.at = node_at(w, 50, 10), .size = StationSize::Small}, rival);
    REQUIRE(plant.ok);
    // The rival's train uses the player's station and track.
    const CommandResult train =
        w.execute(BuyTrain{.loco = 0, .cars = 4, .route = {mine.created_id, plant.created_id}}, rival);
    REQUIRE(train.ok);
    CHECK(w.railway().train(train.created_id).owner == rival_co);

    run_days(w, 360); // within the first year's accounts
    const YearAccounts& player = w.company().this_year();
    const YearAccounts& them = w.company(rival_co).this_year();
    const auto line = [](const YearAccounts& y, Ledger l) { return y.lines[static_cast<std::size_t>(l)]; };
    const Money freight = line(them, Ledger::FreightRevenue);
    const Money trackage = line(player, Ledger::TrackageIncome);
    MESSAGE("rival freight $" << freight.whole_dollars() << ", trackage to the player $" << trackage.whole_dollars());
    REQUIRE(freight > Money{});
    CHECK(trackage == line(them, Ledger::TrackagePaid));
    // Loaded legs run 25 of 45 km on the player's track; empty legs earn nothing.
    CHECK(trackage > freight.scaled(50, 100));
    CHECK(trackage < freight.scaled(60, 100));
    // The runner pays all the fuel and maintenance; the player has no trains.
    CHECK(line(player, Ledger::Fuel) == Money{});
    CHECK(line(them, Ledger::Fuel) > Money{});
    CHECK(w.total_revenue() == Money{}); // the player's own trains earned nothing
}

TEST_CASE("on a rival's track a train yields to the owner's trains, whatever its priority [M]") {
    const auto locos = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "loco", "name": "Loco", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1}]})");
    Railway rw;
    rw.set_rules({.breakdowns = false});
    const NodeId a = rw.track().add_node({0, 0}, 0);
    const NodeId b = rw.track().add_node({20 * kKm, 0}, 0);
    rw.track().add_edge(a, b, false, TrackKind::Ground, BridgeType::None, /*owner=*/0);
    const StationId west = rw.add_station("West", a, StationSize::Small, 0);
    const StationId east = rw.add_station("East", b, StationSize::Small, 0);
    const TrainId owners = rw.add_train(0, 0, {west, east}, /*priority=*/0, /*owner=*/0);
    const TrainId visitor = rw.add_train(0, 0, {east, west}, /*priority=*/9, /*owner=*/1);
    bool visitor_yielded = false;
    for (int i = 0; i < 300; ++i) {
        rw.tick(locos);
        CHECK_FALSE(rw.train(owners).yielding);
        visitor_yielded = visitor_yielded || rw.train(visitor).yielding;
    }
    CHECK(visitor_yielded);
}

TEST_CASE("tycoon data is validated") {
    const TycoonRegistry reg = TycoonRegistry::from_json(read_data("tycoons.json"));
    CHECK(reg.all().size() >= 5);
    CHECK_THROWS_AS(TycoonRegistry::from_json(R"({"tycoons": [{"key": "x", "name": "X"}]})"), std::runtime_error);
    CHECK_THROWS_AS(TycoonRegistry::from_json(
                        R"({"tycoons": [{"key": "x", "name": "X", "company": "C", "expansion": 101}]})"),
                    std::runtime_error);
    CHECK_THROWS_AS(TycoonRegistry::from_json(R"({"tycoons": [
        {"key": "x", "name": "X", "company": "C"}, {"key": "x", "name": "Y", "company": "D"}]})"),
                    std::runtime_error);
}

namespace {

WorldConfig rivals_config(std::uint64_t seed) {
    WorldConfig cfg;
    cfg.seed = seed;
    cfg.width_tiles = cfg.height_tiles = 80;
    cfg.rivals = 2;
    return cfg;
}

} // namespace

TEST_CASE("rival tycoons found companies, build working lines and make money") {
    World w(rivals_config(3), shipped_data());
    REQUIRE(w.rivals().size() == 2);
    REQUIRE(w.companies().size() == 3);
    CHECK(w.investors()[w.rivals()[0].player].name != w.investors()[w.rivals()[1].player].name);
    run_days(w, 540);

    Money rival_revenue;
    for (const Rival& r : w.rivals()) {
        const CompanyId co = *w.investors()[r.player].chairs;
        std::int32_t stations = 0, trains = 0;
        for (const Station& s : w.railway().stations()) stations += s.owner == co;
        for (const Train& t : w.railway().trains()) {
            if (t.owner != co) continue;
            ++trains;
            CHECK(t.state != TrainState::NoRoute); // every line it built is connected
        }
        MESSAGE(w.company(co).name() << ": " << stations << " stations, " << trains << " trains, cash $"
                                     << w.company(co).cash().whole_dollars());
        CHECK(stations >= 2);
        CHECK(trains >= 1);
        CHECK_FALSE(r.routes.empty());
        for (const Train& t : w.railway().trains()) {
            if (t.owner == co) rival_revenue += t.revenue;
        }
    }
    CHECK(rival_revenue > Money{});
    // The player was left alone.
    CHECK(w.company().track_value() == Money{});
}

TEST_CASE("rivals are deterministic, and can be held still") {
    const auto snapshot = [](bool ai) {
        WorldConfig cfg = rivals_config(11);
        cfg.rival_ai = ai;
        World w(cfg, shipped_data());
        run_days(w, 200);
        std::vector<std::int64_t> out;
        for (const Company& c : w.companies()) out.push_back(c.cash().in_cents());
        out.push_back(static_cast<std::int64_t>(w.railway().trains().size()));
        return out;
    };
    CHECK(snapshot(true) == snapshot(true));
    const auto still = snapshot(false);
    CHECK(still.back() == 0); // no trains bought
}
