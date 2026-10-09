#include "legacy_scale.hpp"
#include "railmaster/sim/freight.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

CargoRegistry test_cargo() {
    return CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 30, "decay_sensitivity": 1},
        {"key": "steel", "name": "Steel", "base_price": 85, "decay_sensitivity": 1},
        {"key": "milk", "name": "Milk", "base_price": 110, "decay_sensitivity": 10}
    ]})");
}

IndustryRegistry test_industries(const CargoRegistry& cargo) {
    return IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 365},
        {"key": "electric_plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 365},
        {"key": "steel_user", "name": "Works", "kind": "sink", "inputs": [{"cargo": "steel"}], "rate": 365},
        {"key": "dairy", "name": "Dairy", "kind": "sink", "inputs": [{"cargo": "milk"}], "rate": 365}
    ]})",
                                       cargo);
}

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }

// An economy and railway with stations at the centres of the given cells,
// joined in a line, and no trains yet.
struct Setup {
    CargoRegistry cargo = test_cargo();
    IndustryRegistry ind = test_industries(cargo);
    Economy eco{40, 20, kKm, cargo};
    Railway rw;
    std::vector<StationId> stations;

    explicit Setup(std::vector<std::pair<int, int>> cells) {
        rw.set_rules({.breakdowns = false});
        NodeId prev = 0;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const NodeId n = rw.track().add_node(cell_centre(cells[i].first, cells[i].second), 0);
            if (i > 0) rw.track().add_edge(prev, n);
            stations.push_back(rw.add_station("S" + std::to_string(i), n, StationSize::Small));
            prev = n;
        }
    }
    CargoId c(const char* key) const { return *cargo.find(key); }
    void site(const char* key, int cx, int cy) { eco.add_site(ind, *ind.find(key), cx, cy); }
    void days(int n) {
        for (int i = 0; i < n; ++i) eco.step_day(cargo, ind, 1850);
    }
};

GameData world_data() {
    GameData d;
    d.cargo = test_cargo();
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800,
         "top_speed_mph": 60, "cost": 1, "maintenance_per_year": 1}]})");
    d.industries = test_industries(d.cargo);
    return d;
}

} // namespace

TEST_CASE("catchment grows with station size") {
    CHECK(catchment_radius(StationSize::Small) < catchment_radius(StationSize::Medium));
    CHECK(catchment_radius(StationSize::Medium) < catchment_radius(StationSize::Large));
}

TEST_CASE("cargo loses value in transit, perishables faster, and expires below 10%") {
    const auto cargo = test_cargo();
    const auto& coal = cargo.get(*cargo.find("coal"));
    const auto& milk = cargo.get(*cargo.find("milk"));
    CHECK(value_left_permille(coal, 0) == 1000);
    CHECK(value_left_permille(coal, 30) == 933);  // sensitivity 1: ~5% in 30 days
    CHECK(value_left_permille(milk, 30) == 501);  // sensitivity 10: ~half in 30 days
    CHECK(value_left_permille(milk, 100) == 100); // exactly 10%: still worth something
    CHECK(value_left_permille(milk, 101) == 0);   // expired
}

TEST_CASE("revenue modifiers: difficulty and station age") {
    CHECK(difficulty_revenue_permille(Difficulty::Easy) == 1200);
    CHECK(difficulty_revenue_permille(Difficulty::Medium) == 1000);
    CHECK(difficulty_revenue_permille(Difficulty::Hard) == 900);
    CHECK(difficulty_revenue_permille(Difficulty::Expert) == 800);
    CHECK(station_age_permille(0, false) == 1150);
    CHECK(station_age_permille(2 * 365, false) == 1075);
    CHECK(station_age_permille(4 * 365, false) == 1000);
    CHECK(station_age_permille(12 * 365, false) == 950);
    CHECK(station_age_permille(30 * 365, false) == 900);
    CHECK(station_age_permille(0, true) == 1075); // open country: half the effect
}

TEST_CASE("a station's best price is the dearest cell in its catchment") {
    Setup s({{5, 10}, {20, 10}});
    s.site("electric_plant", 21, 11); // inside the second station's 3 x 3 catchment
    s.days(200);
    const CatchmentPrices p = catchment_prices(s.eco, s.rw, s.rw.station(s.stations[1]), s.c("coal"));
    CHECK(p.best_cx == 21);
    CHECK(p.best_cy == 11);
    CHECK(p.best == s.eco.price(s.c("coal"), 21, 11));
}

TEST_CASE("stations gather only cargo a calling train could sell for more") {
    Setup s({{5, 10}, {30, 10}});
    s.site("coal_mine", 5, 10);
    s.site("electric_plant", 30, 10);
    s.days(100);
    gather_at_stations(s.rw, s.eco, s.cargo, s.ind, 1850);
    const StationId mine = s.stations[0];
    CHECK(s.rw.station(mine).waiting[s.c("coal")].milli == 0); // no train calls here yet

    s.rw.add_train(0, 4, {s.stations[0], s.stations[1]});
    for (int i = 0; i < 10; ++i) gather_at_stations(s.rw, s.eco, s.cargo, s.ind, 1850);
    const WaitingCargo& pool = s.rw.station(mine).waiting[s.c("coal")];
    CHECK(pool.milli > 0);
    // Bought at the station's price: the best in its catchment, a little
    // above the mine's own $15K because the plant's pull reaches this far.
    CHECK(pool.average_price() == catchment_prices(s.eco, s.rw, s.rw.station(mine), s.c("coal")).best);
    CHECK(pool.average_price() < 16'000);
    CHECK(s.rw.station(s.stations[1]).waiting[s.c("coal")].milli == 0); // never gathered at the buyer
}

TEST_CASE("a train loads the most valuable cargo first and only what sells further on") {
    Setup s({{5, 10}, {30, 10}});
    s.site("steel_user", 30, 10); // steel sells at the second stop; coal sells nowhere
    s.days(200);
    Station& st = s.rw.station_mut(s.stations[0]);
    st.waiting.resize(s.cargo.all().size());
    st.waiting[s.c("coal")] = {5 * kMilli, 5LL * kMilli * 15'000};
    st.waiting[s.c("steel")] = {2 * kMilli, 2LL * kMilli * 42'500};
    const TrainId t = s.rw.add_train(0, 4, {s.stations[0], s.stations[1]});

    handle_arrival(s.rw, s.eco, s.cargo, s.ind, t, s.stations[0], 0, 0);
    const Train& train = s.rw.train(t);
    CHECK(train.cars[0].cargo == s.c("steel"));
    CHECK(train.cars[1].cargo == s.c("steel"));
    CHECK_FALSE(train.cars[2].cargo.has_value()); // coal has no buyer on the route: left behind
    CHECK(train.cars[0].pickup_price == 42'500);
    CHECK(s.rw.station(s.stations[0]).waiting[s.c("steel")].milli == 0);
    CHECK(s.rw.station(s.stations[0]).waiting[s.c("coal")].milli == 5 * kMilli);
}

TEST_CASE("cargo is unloaded at the first stop that pays more, earning the difference") {
    Setup s({{5, 10}, {30, 10}});
    s.site("electric_plant", 30, 10);
    s.days(300);
    const TrainId t = s.rw.add_train(0, 2, {s.stations[0], s.stations[1]});
    Train& train = s.rw.train_mut(t);
    train.cars[0] = Car{s.c("coal"), kMilli, 15'000, 100, s.stations[0], std::nullopt};

    // Never sold back where it was loaded.
    CHECK(handle_arrival(s.rw, s.eco, s.cargo, s.ind, t, s.stations[0], 110, 1).total == Money{});
    CHECK(s.rw.train(t).cars[0].cargo.has_value());

    const std::int32_t plant_price = s.eco.price(s.c("coal"), 30, 10);
    const std::int32_t stock_before = s.eco.stock_milli(s.c("coal"), 30, 10);
    const Money income = handle_arrival(s.rw, s.eco, s.cargo, s.ind, t, s.stations[1], 110, 2).total;
    // Ten days in transit for sensitivity-1 coal: 97.7% of the price gain.
    CHECK(income == Money::dollars(std::int64_t{plant_price - 15'000} * 977 / 1000));
    CHECK_FALSE(s.rw.train(t).cars[0].cargo.has_value());
    CHECK(s.eco.stock_milli(s.c("coal"), 30, 10) == stock_before + kMilli); // fed into the local economy
    CHECK(s.rw.train(t).revenue == income);
    CHECK(s.rw.train(t).last_income_tick == 2);
}

TEST_CASE("spoiled cargo is dumped for nothing") {
    Setup s({{5, 10}, {30, 10}});
    s.days(10);
    const TrainId t = s.rw.add_train(0, 1, {s.stations[0], s.stations[1]});
    s.rw.train_mut(t).cars[0] = Car{s.c("milk"), kMilli, 55'000, 0, s.stations[0], std::nullopt};
    CHECK(handle_arrival(s.rw, s.eco, s.cargo, s.ind, t, s.stations[1], 120, 1).total == Money{});
    CHECK_FALSE(s.rw.train(t).cars[0].cargo.has_value());
}

// A flat 60 x 20 world with a coal mine and, 45 km away (far beyond drift
// reach), a power plant, joined by track, with stations and one train.
// Returns the train's id.
TrainId build_coal_line(World& w) {
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 60; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 60; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    const IndustryRegistry& ind = w.data().industries;
    w.economy().add_site(ind, *ind.find("coal_mine"), 5, 10);
    w.economy().add_site(ind, *ind.find("electric_plant"), 50, 10);

    const auto free_at = [](int cx, int cy) { return TrackEnd{TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; };
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(50, 10)}).ok);
    const TrackNetwork& net = w.railway().track();
    const auto node_at = [&](int cx, int cy) {
        const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
        return TrackEnd{TrackEnd::Kind::Node, n, 0, net.node(n).pos};
    };
    const CommandResult a = w.execute(BuildStation{.at = node_at(5, 10), .size = StationSize::Small});
    const CommandResult b = w.execute(BuildStation{.at = node_at(50, 10), .size = StationSize::Small});
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    const CommandResult t = w.execute(BuyTrain{.loco = 0, .cars = 4, .route = {a.created_id, b.created_id}});
    REQUIRE(t.ok);
    return t.created_id;
}

WorldConfig coal_line_config() {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = 60;
    cfg.height_tiles = 20;
    cfg.populate = false;
    return cfg;
}

void run_days(World& w, int days) {
    for (int day = 0; day < days; ++day)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

TEST_CASE("a train earns money hauling coal to a distant power plant") {
    World w(coal_line_config(), world_data());
    const TrainId id = build_coal_line(w);
    run_days(w, 365);

    const Train& train = w.railway().train(id);
    MESSAGE("revenue in a year: $" << train.revenue.whole_dollars() << " over " << train.stops_made << " stops");
    CHECK(train.revenue > Money::dollars(100'000));
    CHECK(w.total_revenue() == train.revenue);
    // A stop's income is at most four full carloads at the mine-to-plant gap
    // ($15K to $45K), plus up to 15% for a new station.
    CHECK(train.last_income <= Money::dollars(4 * 30'000).scaled(115, 100));
}

TEST_CASE("freight is deterministic") {
    auto run = [] {
        World w(coal_line_config(), world_data());
        build_coal_line(w);
        run_days(w, 200);
        return w.total_revenue().in_cents();
    };
    const auto first = run();
    CHECK(first > 0);
    CHECK(first == run());
}
