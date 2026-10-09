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
        {"key": "passengers", "name": "Passengers", "class": "express", "fare_per_km": 500, "generation": 4,
         "decay_sensitivity": 1}
    ]})");
}

IndustryRegistry test_industries(const CargoRegistry& cargo) {
    return IndustryRegistry::from_json(R"({"industries": [
        {"key": "works", "name": "Works", "kind": "sink", "inputs": [{"cargo": "steel"}, {"cargo": "coal"}], "rate": 365}
    ]})",
                                       cargo);
}

MapPoint cell_centre(std::int32_t cx, std::int32_t cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }

// Two stations 20 km apart; a works at the second buys coal and steel.
struct Line {
    CargoRegistry cargo = test_cargo();
    IndustryRegistry ind = test_industries(cargo);
    Economy eco{40, 20, kKm, cargo};
    Railway rw;
    StationId a = 0, b = 0;
    TrainId t = 0;

    Line() {
        rw.set_rules({.breakdowns = false});
        const NodeId na = rw.track().add_node(cell_centre(5, 10), 0);
        const NodeId nb = rw.track().add_node(cell_centre(25, 10), 0);
        rw.track().add_edge(na, nb);
        a = rw.add_station("A", na, StationSize::Small);
        b = rw.add_station("B", nb, StationSize::Small);
        eco.add_site(ind, 0, 25, 10);
        for (int d = 0; d < 400; ++d) eco.step_day(cargo, ind, 1850);
        t = rw.add_train(0, 4, {a, b});
    }
    CargoId c(const char* key) const { return *cargo.find(key); }
    // Cargo waiting at A, bought cheaply.
    void waiting(const char* key, int loads) {
        Station& st = rw.station_mut(a);
        st.waiting.resize(cargo.all().size());
        st.waiting[c(key)] = {loads * kMilli, std::int64_t{loads} * kMilli * 1'000};
    }
    void passengers(int loads) { rw.station_mut(a).express.push_back({c("passengers"), b, loads * kMilli}); }
    void arrive_at_a() { handle_arrival(rw, eco, cargo, ind, t, a, 0, 0); }
    std::int32_t count(const char* key) const {
        std::int32_t n = 0;
        for (const Car& car : rw.train(t).cars) n += car.cargo == c(key);
        return n;
    }
};

} // namespace

TEST_CASE("by default a train takes up to 4 cars of anything, best first [D]") {
    Line l;
    CHECK(l.rw.train(l.t).rules.size() == 2);
    CHECK(l.rw.train(l.t).rules[0] == ConsistRule{});
    l.waiting("coal", 3);
    l.waiting("steel", 3);
    l.passengers(3);
    l.arrive_at_a();
    CHECK(l.rw.train(l.t).loaded_cars() == 4);
    CHECK(l.count("steel") == 3); // steel pays most here
    CHECK_FALSE(l.rw.train(l.t).holding);
}

TEST_CASE("an auto consist can be held to freight or to express [D]") {
    Line freight, express;
    for (Line* l : {&freight, &express}) {
        l->waiting("coal", 3);
        l->passengers(3);
    }
    freight.rw.train_mut(freight.t).rules[0].filter = CargoFilter::Freight;
    express.rw.train_mut(express.t).rules[0].filter = CargoFilter::Express;
    freight.arrive_at_a();
    express.arrive_at_a();
    CHECK(freight.count("coal") == 3);
    CHECK(freight.count("passengers") == 0);
    CHECK(express.count("passengers") == 3);
    CHECK(express.count("coal") == 0);
}

TEST_CASE("the maximum caps the cars taken; empty cars are not hauled [D]") {
    Line l;
    l.rw.train_mut(l.t).rules[0].max = 2;
    l.waiting("steel", 5);
    l.arrive_at_a();
    CHECK(l.rw.train(l.t).cars.size() == 2);
    CHECK(l.count("steel") == 2);
    l.rw.train_mut(l.t).caboose = true;
    CHECK(l.rw.train(l.t).hauled_cars() == 3);
}

TEST_CASE("a custom consist takes exactly the listed cars [D]") {
    Line l;
    l.rw.train_mut(l.t).rules[0] = ConsistRule{.custom = true, .cars = {l.c("coal"), l.c("coal"), l.c("passengers")}};
    l.waiting("coal", 3);
    l.waiting("steel", 3); // worth more, but not listed
    l.passengers(3);
    l.arrive_at_a();
    CHECK(l.count("coal") == 2);
    CHECK(l.count("passengers") == 1);
    CHECK(l.count("steel") == 0);
}

TEST_CASE("with a minimum the train waits at the stop for a full load [D]") {
    Line l;
    const LocomotiveRegistry locos = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800, "top_speed_mph": 60, "cost": 1,
         "maintenance_per_year": 1}]})");
    l.rw.train_mut(l.t).rules[0].min = 3;
    l.waiting("steel", 1);
    // It sets off and comes straight back to A, its first stop, to load.
    l.rw.tick(locos);
    l.rw.take_arrivals();
    for (int i = 0; i < 20'000 && l.rw.train(l.t).stops_made < 2; ++i) l.rw.tick(locos);
    REQUIRE(l.rw.train(l.t).stops_made == 2);
    l.rw.take_arrivals();
    l.arrive_at_a();
    CHECK(l.rw.train(l.t).loaded_cars() == 1);
    CHECK(l.rw.train(l.t).holding);
    // Still there after many dwells, asking each time to load again.
    int reloads = 0;
    for (int i = 0; i < 200; ++i) {
        l.rw.tick(locos);
        reloads += static_cast<int>(l.rw.take_arrivals().size());
    }
    CHECK(l.rw.train(l.t).state == TrainState::Dwelling);
    CHECK(reloads > 5);
    // Two more loads arrive: it fills up and leaves.
    l.waiting("steel", 2);
    l.arrive_at_a();
    CHECK(l.rw.train(l.t).loaded_cars() == 3);
    CHECK_FALSE(l.rw.train(l.t).holding);
    for (int i = 0; i < 50; ++i) l.rw.tick(locos);
    CHECK(l.rw.train(l.t).state == TrainState::Moving);
}

TEST_CASE("a dining car makes passengers pay 20% more [D]") {
    const auto fare = [](bool diner) {
        Line l;
        l.rw.train_mut(l.t).diner = diner;
        l.passengers(1);
        l.arrive_at_a();
        return handle_arrival(l.rw, l.eco, l.cargo, l.ind, l.t, l.b, 0, 0).total;
    };
    CHECK(fare(true) == fare(false).scaled(120, 100));
}

TEST_CASE("a caboose halves breakdowns per distance run [D]") {
    // Breakdowns per 1,000 km.
    const auto rate = [](bool caboose) {
        const LocomotiveRegistry locos = LocomotiveRegistry::from_json(R"({"locomotives": [
            {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800, "top_speed_mph": 60, "cost": 1,
             "maintenance_per_year": 1, "reliability": 5}]})");
        Line l;
        l.rw.set_rules({.breakdowns = true});
        l.rw.train_mut(l.t).caboose = caboose;
        for (int i = 0; i < 200'000; ++i) l.rw.tick(locos);
        const Train& t = l.rw.train(l.t);
        return static_cast<double>(t.breakdowns) * 1e9 / static_cast<double>(t.distance_mm);
    };
    const double without = rate(false), with = rate(true);
    MESSAGE("breakdowns per 1,000 km: without a caboose " << without << ", with " << with);
    CHECK(with < without * 0.65);
    CHECK(with > without * 0.35);
}

namespace {

World coal_world() {
    GameData d;
    d.cargo = test_cargo();
    d.industries = test_industries(d.cargo);
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "diesel", "available_from": 1800, "top_speed_mph": 60, "cost": 50000,
         "maintenance_per_year": 1}]})");
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = 40;
    cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.industries_appear = false;
    World w(cfg, std::move(d));
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 40; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

TrainId bought_train(World& w) {
    const auto free_at = [](int cx, int cy) { return TrackEnd{TrackEnd::Kind::Free, 0, 0, cell_centre(cx, cy)}; };
    REQUIRE(w.execute(BuildTrack{.start = free_at(5, 10), .end = free_at(25, 10)}).ok);
    const TrackNetwork& net = w.railway().track();
    const auto node_at = [&](int cx, int cy) {
        const NodeId n = *net.nearest_node(cell_centre(cx, cy), 1);
        return TrackEnd{TrackEnd::Kind::Node, n, 0, net.node(n).pos};
    };
    const StationId a = w.execute(BuildStation{.at = node_at(5, 10)}).created_id;
    const StationId b = w.execute(BuildStation{.at = node_at(25, 10)}).created_id;
    const CommandResult r = w.execute(BuyTrain{.loco = 0, .cars = 4, .route = {a, b}, .priority = 3});
    REQUIRE(r.ok);
    return r.created_id;
}

} // namespace

TEST_CASE("setting consists and special cars: 8 slots between them [D]") {
    World w = coal_world();
    const TrainId t = bought_train(w);
    const ConsistRule six{.max = 6};
    REQUIRE(w.execute(SetConsist{.train = t, .rule = six}).ok); // every stop
    CHECK(w.railway().train(t).rules[0].max == 6);
    CHECK(w.railway().train(t).rules[1].max == 6);
    CHECK(w.execute(SetConsist{.train = t, .stop = 1, .rule = {.filter = CargoFilter::Express, .max = 2}}).ok);
    CHECK(w.railway().train(t).rules[1].filter == CargoFilter::Express);
    CHECK(w.railway().train(t).rules[0].max == 6);
    CHECK(w.execute(SetSpecialCars{.train = t, .caboose = true, .diner = true}).ok); // 6 + 2 = 8
    CHECK(w.execute(SetConsist{.train = t, .stop = 1, .rule = {.max = 7}}).error.find("slots") != std::string::npos);
    CHECK(w.execute(SetConsist{.train = t, .rule = {.min = 5, .max = 4}}).error.find("minimum") != std::string::npos);
    CHECK(w.execute(SetConsist{.train = t, .stop = 2, .rule = {}}).error.find("no such stop") != std::string::npos);
    REQUIRE(w.execute(SetSpecialCars{.train = t, .caboose = false, .diner = false}).ok);
    REQUIRE(w.execute(SetConsist{.train = t, .rule = {.max = 8}}).ok);
    CHECK(w.execute(SetSpecialCars{.train = t, .caboose = true}).error.find("slots") != std::string::npos);
    const PlayerId rival = w.add_player_company("Rival", "R");
    CHECK(w.execute(SetConsist{.train = t, .rule = {}}, rival).error.find("your own") != std::string::npos);
}

TEST_CASE("copying a train clones engine, route, consist and special cars; retiring ends it [D]") {
    World w = coal_world();
    const TrainId t = bought_train(w);
    REQUIRE(w.execute(SetConsist{.train = t, .stop = 0, .rule = {.filter = CargoFilter::Freight, .min = 2, .max = 3}}).ok);
    REQUIRE(w.execute(SetSpecialCars{.train = t, .caboose = true}).ok);
    const Money cash = w.company().cash();
    const CommandResult copy = w.execute(CopyTrain{.train = t});
    REQUIRE(copy.ok);
    CHECK(copy.cost == Money::dollars(50'000));
    CHECK(w.company().cash() == cash - Money::dollars(50'000));
    const Train& a = w.railway().train(t);
    const Train& b = w.railway().train(copy.created_id);
    CHECK(b.loco == a.loco);
    CHECK(b.route == a.route);
    CHECK(b.rules == a.rules);
    CHECK(b.caboose);
    CHECK(b.priority == a.priority);

    const Money stock = w.company().rolling_stock_value();
    REQUIRE(w.execute(RetireTrain{.train = t}).ok);
    CHECK(w.railway().train(t).state == TrainState::Retired);
    CHECK(w.company().rolling_stock_value() == stock - Money::dollars(50'000));
    for (int d = 0; d < 30; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    CHECK(w.railway().train(t).state == TrainState::Retired); // never runs again
    CHECK(w.railway().train(t).distance_mm == 0);
    CHECK(w.execute(RetireTrain{.train = t}).error.find("no longer in service") != std::string::npos);
    CHECK_FALSE(w.execute(CopyTrain{.train = t}).ok);
}
