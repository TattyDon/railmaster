#include "railmaster/sim/freight.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

CargoRegistry test_cargo() {
    return CargoRegistry::from_json(R"({"cargo": [
        {"key": "coal", "name": "Coal", "base_price": 30},
        {"key": "passengers", "name": "Passengers", "class": "express", "decay_sensitivity": 9,
         "fare_per_km": 500, "generation": 4},
        {"key": "mail", "name": "Mail", "class": "express", "decay_sensitivity": 10,
         "fare_per_km": 700, "generation": 2, "demand_cap": true}
    ]})");
}

IndustryRegistry test_industries(const CargoRegistry& cargo) {
    return IndustryRegistry::from_json(R"({"industries": [
        {"key": "house", "name": "Houses", "kind": "house", "rate": 1,
         "inputs": [{"cargo": "passengers"}, {"cargo": "mail"}], "outputs": ["passengers", "mail"]}
    ]})",
                                       cargo);
}

MapPoint cell_centre(int cx, int cy) { return {cx * kKm + kKm / 2, cy * kKm + kKm / 2}; }

// Towns of `houses` houses at each given cell, each with a station, all on
// one straight line of track.
struct Towns {
    CargoRegistry cargo = test_cargo();
    IndustryRegistry ind = test_industries(cargo);
    Economy eco{60, 10, kKm, cargo};
    Railway rw;
    std::vector<StationId> st;

    Towns(std::vector<int> xs, int houses) {
        rw.set_rules({.breakdowns = false});
        NodeId prev = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            eco.add_site(ind, 0, xs[i], 5, houses);
            const NodeId n = rw.track().add_node(cell_centre(xs[i], 5), 0);
            if (i > 0) rw.track().add_edge(prev, n);
            st.push_back(rw.add_station("T" + std::to_string(i), n, StationSize::Small));
            prev = n;
        }
    }
    CargoId pax() const { return *cargo.find("passengers"); }
    CargoId mail() const { return *cargo.find("mail"); }
    void gather(int days) {
        for (int i = 0; i < days; ++i) gather_at_stations(rw, eco, cargo, ind, 1850);
    }
    std::int32_t waiting(StationId at, CargoId c, StationId dest) const {
        for (const ExpressWaiting& e : rw.station(at).express)
            if (e.cargo == c && e.destination == dest) return e.milli;
        return 0;
    }
    Money arrive(TrainId t, StationId s, std::int32_t day) {
        return handle_arrival(rw, eco, cargo, ind, t, s, day, static_cast<std::uint64_t>(day));
    }
};

} // namespace

TEST_CASE("passengers and mail appear only for places a train connects") {
    Towns w({5, 25, 45}, 20);
    w.gather(30);
    CHECK(w.rw.station(w.st[0]).express.empty()); // no trains yet

    w.rw.add_train(0, 4, {w.st[0], w.st[1]});
    w.gather(30);
    CHECK(w.waiting(w.st[0], w.pax(), w.st[1]) > 0);
    CHECK(w.waiting(w.st[1], w.pax(), w.st[0]) > 0);
    CHECK(w.waiting(w.st[0], w.mail(), w.st[1]) > 0);
    CHECK(w.waiting(w.st[0], w.pax(), w.st[2]) == 0); // no train serves the third town
}

TEST_CASE("more connections mean more passengers") {
    Towns one({5, 25, 45}, 20);
    one.rw.add_train(0, 4, {one.st[0], one.st[1]});
    Towns two({5, 25, 45}, 20);
    two.rw.add_train(0, 4, {two.st[0], two.st[1], two.st[2]});
    one.gather(10);
    two.gather(10);
    const auto total = [](const Towns& w, StationId at) {
        std::int64_t sum = 0;
        for (const ExpressWaiting& e : w.rw.station(at).express)
            if (e.cargo == w.pax()) sum += e.milli;
        return sum;
    };
    CHECK(total(two, two.st[0]) > total(one, one.st[0]) * 3 / 2);
}

TEST_CASE("waiting passengers give up over time") {
    Towns w({5, 25}, 20);
    const TrainId t = w.rw.add_train(0, 4, {w.st[0], w.st[1]});
    w.gather(30);
    const std::int32_t before = w.waiting(w.st[0], w.pax(), w.st[1]);
    REQUIRE(before > 0);
    w.rw.train_mut(t).route = {w.st[0]}; // no longer connected: nothing new arrives
    w.gather(10);
    CHECK(w.waiting(w.st[0], w.pax(), w.st[1]) < before * 7 / 10); // ~4.5% a day for passengers
}

TEST_CASE("express loads go only where the train goes, and only off at their destination") {
    Towns w({5, 25, 45}, 40);
    const TrainId t = w.rw.add_train(0, 2, {w.st[0], w.st[1], w.st[2]});
    w.gather(60);
    REQUIRE(w.waiting(w.st[0], w.pax(), w.st[2]) >= kMilli);

    w.arrive(t, w.st[0], 100);
    const Train& train = w.rw.train(t);
    // The most valuable loads are for the farthest stop: both cars go there.
    REQUIRE(train.cars[0].destination.has_value());
    CHECK(*train.cars[0].destination == w.st[2]);
    CHECK(*train.cars[1].destination == w.st[2]);

    // Passing through the middle town: they stay on board.
    w.arrive(t, w.st[1], 101);
    CHECK(train.cars[0].destination == w.st[2]);

    // At the destination: paid by distance (40 km), less two days' decay.
    const Money income = w.arrive(t, w.st[2], 102);
    const Money fare = Money::dollars(500 * 40);
    const CargoType& pax = w.cargo.get(w.pax());
    const std::int32_t left = value_left_permille(pax, 2);
    CHECK(income >= fare.scaled(left, 1000) * 2 - Money::dollars(1));
    // ...and the cars are refilled with passengers heading back.
    REQUIRE(train.cars[0].cargo.has_value());
    CHECK(train.cars[0].loaded_at == w.st[2]);
    CHECK(train.cars[0].destination != w.st[2]);
}

TEST_CASE("express fares grow with distance") {
    Towns w({5, 15, 45}, 20);
    const CargoType& pax = w.cargo.get(w.pax());
    CHECK(express_fare(pax, w.rw, w.st[0], w.st[1]) == Money::dollars(500 * 10));
    CHECK(express_fare(pax, w.rw, w.st[0], w.st[2]) == Money::dollars(500 * 40));
}

TEST_CASE("a town stops paying for mail beyond its monthly demand") {
    Towns w({5, 25}, 6); // 6 houses: mail demand 6 a year, so 1 load a month, capped at 2
    const TrainId t = w.rw.add_train(0, 1, {w.st[0], w.st[1]});
    auto deliver_mail = [&](std::int32_t day) {
        w.rw.train_mut(t).cars[0] = Car{w.mail(), kMilli, 0, day, w.st[0], w.st[1]};
        return w.arrive(t, w.st[1], day);
    };
    CHECK(deliver_mail(10) > Money{});
    CHECK(deliver_mail(11) == Money{}); // 1 load received >= cap of 6 * 1000 / 12 * 2 = 1000
    start_new_month(w.rw);
    CHECK(deliver_mail(40) > Money{});

    // Passengers have no such cap.
    for (int i = 0; i < 5; ++i) {
        w.rw.train_mut(t).cars[0] = Car{w.pax(), kMilli, 0, 50, w.st[0], w.st[1]};
        CHECK(w.arrive(t, w.st[1], 50) > Money{});
    }
}

TEST_CASE("express loads that never arrive go stale and are dropped") {
    Towns w({5, 25, 45}, 20);
    const TrainId t = w.rw.add_train(0, 1, {w.st[0], w.st[1]});
    w.rw.train_mut(t).cars[0] = Car{w.pax(), kMilli, 0, 0, w.st[0], w.st[2]}; // st[2] not on the route
    w.arrive(t, w.st[1], 5);
    CHECK(w.rw.train(t).cars[0].cargo.has_value()); // still hoping
    w.arrive(t, w.st[1], 200);
    CHECK_FALSE(w.rw.train(t).cars[0].cargo.has_value());
}
