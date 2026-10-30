#include "railmaster/sim/freight.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <utility>

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
        for (int i = 0; i < days; ++i) gather_at_stations(rw, eco, cargo, ind, 1850, i);
    }
    // Travellers waiting at `at` for the home in the town of station `dest`
    // (each town is one house site, numbered as its station).
    std::int32_t waiting(StationId at, CargoId c, StationId dest) const {
        for (const ExpressWaiting& e : rw.station(at).express)
            if (e.cargo == c && e.home == dest) return e.milli;
        return 0;
    }
    Money arrive(TrainId t, StationId s, std::int32_t day, std::int32_t appeal = 0) {
        return handle_arrival(rw, eco, cargo, ind, t, s, day, static_cast<std::uint64_t>(day) * kTrainTicksPerDay, 1000,
                              appeal)
            .total;
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
    // About 0.9% a day for passengers: half the on-train rate [C].
    CHECK(w.waiting(w.st[0], w.pax(), w.st[1]) < before * 95 / 100);
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

    // At the destination: paid by distance (40 km), less two days' decay,
    // x 1.5 for a trip far quicker than the era's reference speed.
    const Money income = w.arrive(t, w.st[2], 102);
    const CargoType& pax = w.cargo.get(w.pax());
    const Money fare = express_fare(pax, w.rw, w.st[0], w.st[2]);
    const std::int32_t left = value_left_permille(pax, 2);
    CHECK(income >= fare.scaled(left, 1000).scaled(1500, 1000) * 2 - Money::dollars(1));
    // ...and the cars are refilled with passengers heading back.
    REQUIRE(train.cars[0].cargo.has_value());
    CHECK(train.cars[0].loaded_at == w.st[2]);
    CHECK(train.cars[0].destination != w.st[2]);
}

TEST_CASE("express fares grow with distance^0.9, the plain rate at 50 km (§8.3 [I])") {
    Towns w({5, 15, 45, 55}, 20);
    const CargoType& pax = w.cargo.get(w.pax());
    CHECK(express_fare(pax, w.rw, w.st[0], w.st[3]) == Money::dollars(500 * 50));
    // 10 km pays as 50 x 0.2^0.9 = 11.7 km; 40 km as 40.9 km.
    const Money ten = express_fare(pax, w.rw, w.st[0], w.st[1]);
    const Money forty = express_fare(pax, w.rw, w.st[0], w.st[2]);
    CHECK(ten > Money::dollars(500 * 116 / 10));
    CHECK(ten < Money::dollars(500 * 119 / 10));
    CHECK(forty > Money::dollars(500 * 405 / 10));
    CHECK(forty < Money::dollars(500 * 413 / 10));
    CHECK(express_base_fare(pax, 0) == Money{});
}

TEST_CASE("express pays by trip speed against the era's reference, within 0.25-1.5 (§8.3 [I])") {
    CHECK(reference_speed_mph(1800) == 14);
    CHECK(reference_speed_mph(1850) == 21); // between 14 (1830) and 29 (1870)
    CHECK(reference_speed_mph(2100) == 126);
    const std::int64_t per_tick = mph_to_mm_per_tick(reference_speed_mph(1850));
    const std::int64_t mm = per_tick * 100; // 100 ticks at the reference speed
    CHECK(express_speed_permille(mm, 100, 1850) == 1000);
    CHECK(express_speed_permille(mm, 200, 1850) == 500);
    CHECK(express_speed_permille(mm, 80, 1850) == 1250);
    CHECK(express_speed_permille(mm, 10, 1850) == 1500);   // capped
    CHECK(express_speed_permille(mm, 1000, 1850) == 250);  // floored
    // The same trip is slow by the standards of a later era.
    CHECK(express_speed_permille(mm, 100, 1950) < 1000);
}

TEST_CASE("a handsome engine raises passenger fares, an ugly one lowers them (§9.1 [D])") {
    const auto income_with = [](std::int32_t appeal) {
        Towns w({5, 25}, 40);
        const TrainId t = w.rw.add_train(0, 1, {w.st[0], w.st[1]});
        w.gather(60);
        w.arrive(t, w.st[0], 100, appeal);
        REQUIRE(w.rw.train(t).cars[0].destination.has_value()); // passengers or mail
        return w.arrive(t, w.st[1], 110, appeal);
    };
    const Money plain = income_with(0);
    REQUIRE(plain > Money{});
    const auto near = [](Money a, Money b) { return a - b <= Money::dollars(1) && b - a <= Money::dollars(1); };
    CHECK(near(income_with(30), plain.scaled(130, 100)));
    CHECK(near(income_with(-15), plain.scaled(85, 100)));
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
    start_new_month(w.rw, w.eco, w.cargo, w.ind, 30);
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

TEST_CASE("travellers change trains at a hub, and each train is paid for its leg (§8.3 [D/I])") {
    Towns w({5, 25, 45}, 40);
    const TrainId first = w.rw.add_train(0, 1, {w.st[0], w.st[1]});
    const TrainId second = w.rw.add_train(0, 1, {w.st[1], w.st[2]});
    w.gather(60);
    // No train runs from the first town to the third, but one change gets there.
    REQUIRE(w.waiting(w.st[0], w.pax(), w.st[2]) >= kMilli);
    // Keep only passengers for the third town at the first, so they board.
    for (ExpressWaiting& e : w.rw.station_mut(w.st[0]).express)
        if (e.home != w.st[2] || e.cargo != w.pax()) e.milli = 0;
    w.arrive(first, w.st[0], 100);
    const Car& car = w.rw.train(first).cars[0];
    REQUIRE(car.destination == w.st[2]);
    CHECK(car.via == w.st[1]);
    // At the hub: paid for 20 km, and they wait there for the onward train.
    const std::int32_t before = w.waiting(w.st[1], w.pax(), w.st[2]);
    const Money leg1 = w.arrive(first, w.st[1], 101);
    CHECK(leg1 > Money{});
    CHECK(w.waiting(w.st[1], w.pax(), w.st[2]) > before);
    // The second train takes them on.
    for (ExpressWaiting& e : w.rw.station_mut(w.st[1]).express)
        if (e.home != w.st[2] || e.cargo != w.pax()) e.milli = 0;
    w.arrive(second, w.st[1], 102);
    REQUIRE(w.rw.train(second).cars[0].destination == w.st[2]);
    CHECK_FALSE(w.rw.train(second).cars[0].via.has_value());
    CHECK(w.arrive(second, w.st[2], 103) > Money{});
}

TEST_CASE("joining networks makes more passengers: destinations one change away count") {
    Towns two({5, 25, 45}, 20);
    two.rw.add_train(0, 4, {two.st[0], two.st[1]});
    Towns joined({5, 25, 45}, 20);
    joined.rw.add_train(0, 4, {joined.st[0], joined.st[1]});
    joined.rw.add_train(0, 4, {joined.st[1], joined.st[2]});
    two.gather(10);
    joined.gather(10);
    CHECK(two.waiting(two.st[0], two.pax(), two.st[2]) == 0);
    CHECK(joined.waiting(joined.st[0], joined.pax(), joined.st[2]) > 0);
}

TEST_CASE("travellers reach places two changes of trains away, changing where they get nearer in trains") {
    // Four towns, three trains end to end: 0-1, 1-2, 2-3.
    Towns w({5, 20, 35, 50}, 20);
    w.rw.add_train(0, 4, {w.st[0], w.st[1]});
    w.rw.add_train(0, 4, {w.st[1], w.st[2]});
    w.rw.add_train(0, 4, {w.st[2], w.st[3]});
    w.gather(30);
    // Three trains from 0 to 3: within the default reach.
    CHECK(w.waiting(w.st[0], w.pax(), w.st[3]) > 0);
    // Loaded at 0 for 3, they ride the first train to 1, the hub nearer in trains.
    const TrainId first = 0;
    for (auto& e : w.rw.station_mut(w.st[0]).express) {
        if (e.home != w.st[3]) e.milli = 0; // only the far travellers wait
    }
    w.arrive(first, w.st[0], 31);
    bool bound_for_3 = false;
    for (const Car& car : w.rw.train(first).cars) {
        if (car.destination && *car.destination == w.st[3]) {
            bound_for_3 = true;
            CHECK(car.via == std::optional<std::uint32_t>(w.st[1]));
        }
    }
    CHECK(bound_for_3);

    // With journeys of at most two trains, 3 is out of reach from 0.
    Balance b = default_balance();
    b.express.max_legs = 2;
    Towns near({5, 20, 35, 50}, 20);
    near.rw.set_balance(b);
    near.rw.add_train(0, 4, {near.st[0], near.st[1]});
    near.rw.add_train(0, 4, {near.st[1], near.st[2]});
    near.rw.add_train(0, 4, {near.st[2], near.st[3]});
    near.gather(30);
    CHECK(near.waiting(near.st[0], near.pax(), near.st[2]) > 0);
    CHECK(near.waiting(near.st[0], near.pax(), near.st[3]) == 0);
}

TEST_CASE("when most travellers give up waiting, their homes avoid the railway for six months") {
    Towns w({5, 25}, 30);
    w.rw.add_train(0, 4, {w.st[0], w.st[1]}); // a train that never comes: it stays where it is
    const auto avoided = [&] {
        const auto& m = w.rw.home_avoided_until();
        const auto it = m.find(0); // the first town's houses
        return it != m.end() && w.pax() < it->second.size() ? it->second[w.pax()] : 0;
    };
    // The first month the waiting room is still filling: few have given up yet.
    int day = 0;
    for (; day < 30; ++day) gather_at_stations(w.rw, w.eco, w.cargo, w.ind, 1850, day);
    start_new_month(w.rw, w.eco, w.cargo, w.ind, day);
    CHECK(avoided() == 0);
    // Once it is full, nearly everyone made gives up: the town avoids the railway.
    for (int month = 0; month < 6 && avoided() == 0; ++month) {
        for (int i = 0; i < 30; ++i, ++day) gather_at_stations(w.rw, w.eco, w.cargo, w.ind, 1850, day);
        start_new_month(w.rw, w.eco, w.cargo, w.ind, day);
    }
    const std::int32_t until = avoided();
    REQUIRE(until > 0);
    CHECK(until == day + default_balance().express.avoid_days);
    // While avoided, the town makes fewer travellers.
    const auto made_on = [&](std::int32_t when) {
        for (auto& e : w.rw.station_mut(w.st[0]).express) e.milli = 0;
        gather_at_stations(w.rw, w.eco, w.cargo, w.ind, 1850, when);
        return w.waiting(w.st[0], w.pax(), w.st[1]);
    };
    const std::int32_t avoided_day = made_on(day + 1);
    const std::int32_t normal_day = made_on(until + 1);
    CHECK(avoided_day > 0);
    CHECK(avoided_day * 100 <= normal_day * (default_balance().express.avoid_generation_percent + 5));
}

TEST_CASE("travellers go more to near places than far ones: pull over a power of distance (§8.3 [I])") {
    const auto& xb = default_balance().express;
    const double e = xb.gravity_exponent_permille / 1000.0, floor_km = xb.gravity_min_km;
    // The ratio of loads to the near town and the far one, both as big.
    const auto expected = [&](double near_km, double far_km) {
        return std::pow(std::max(far_km, floor_km) / std::max(near_km, floor_km), e);
    };
    for (const auto& [near_x, far_x] : {std::pair{0, 45}, std::pair{11, 45}}) {
        // From the town at 15 km: one town at near_x, another at far_x, equal in size.
        Towns w({near_x, 15, far_x}, 20);
        w.rw.add_train(0, 4, {w.st[0], w.st[1], w.st[2]});
        w.gather(5);
        const double near = w.waiting(w.st[1], w.pax(), w.st[0]), far = w.waiting(w.st[1], w.pax(), w.st[2]);
        REQUIRE(far > 0);
        const double want = expected(15.0 - near_x, far_x - 15.0);
        CHECK(near / far > want * 0.93);
        CHECK(near / far < want * 1.07);
    }
}

TEST_CASE("a home in reach of two stations shares its travellers between them, not twice over (§7.1 [D])") {
    const auto made_at_first_town = [](bool second_station) {
        Towns w({5, 25}, 20);
        std::vector<StationId> from{w.st[0]};
        if (second_station) {
            // Another station in the first town, beside the first, on its own line to the second town.
            const NodeId n = w.rw.track().add_node(cell_centre(5, 6), 0);
            w.rw.track().add_edge(w.rw.station(w.st[0]).node, n);
            from.push_back(w.rw.add_station("T0b", n, StationSize::Small));
            w.rw.add_train(0, 4, {from.back(), w.st[1]});
        }
        w.rw.add_train(0, 4, {w.st[0], w.st[1]});
        w.gather(5);
        std::int64_t sum = 0;
        for (StationId s : from)
            for (const ExpressWaiting& e : w.rw.station(s).express)
                if (e.cargo == w.pax()) sum += e.milli;
        return sum;
    };
    const std::int64_t one = made_at_first_town(false), two = made_at_first_town(true);
    REQUIRE(one > 0);
    CHECK(two * 100 >= one * 98);
    CHECK(two * 100 <= one * 102);
}

TEST_CASE("travellers get off at any station by their home (§8.3 [D])") {
    Towns w({5, 25, 45}, 40);
    // A second station in the third town, which the train runs to instead.
    const NodeId n = w.rw.track().add_node(cell_centre(45, 6), 0);
    w.rw.track().add_edge(w.rw.station(w.st[2]).node, n);
    const StationId other = w.rw.add_station("T2b", n, StationSize::Small);
    const TrainId t = w.rw.add_train(0, 2, {w.st[0], other});
    w.gather(60);
    // Bound for the houses of the third town, though no train calls at its first station.
    REQUIRE(w.waiting(w.st[0], w.pax(), w.st[2]) >= kMilli);
    for (ExpressWaiting& e : w.rw.station_mut(w.st[0]).express)
        if (e.home != w.st[2] || e.cargo != w.pax()) e.milli = 0;
    w.arrive(t, w.st[0], 100);
    const Car& car = w.rw.train(t).cars[0];
    REQUIRE(car.destination == other);
    CHECK(car.home == std::optional<std::uint32_t>(w.st[2]));
    CHECK(w.arrive(t, other, 102) > Money{});
}

TEST_CASE("a home that gave up avoids the railway at any station, old or new") {
    Towns w({5, 25}, 30);
    w.rw.add_train(0, 4, {w.st[0], w.st[1]}); // never comes
    int day = 0;
    for (int month = 0; month < 6; ++month) {
        for (int i = 0; i < 30; ++i, ++day) gather_at_stations(w.rw, w.eco, w.cargo, w.ind, 1850, day);
        start_new_month(w.rw, w.eco, w.cargo, w.ind, day);
    }
    REQUIRE(w.rw.home_avoids(0, w.pax(), day));
    // A new station beside the old one, with a train of its own: the
    // first town's houses still avoid the railway, wherever they set out.
    const auto made_by_new_station = [&](Towns& at, std::int32_t when) {
        const NodeId n = at.rw.track().add_node(cell_centre(5, 6), 0);
        at.rw.track().add_edge(at.rw.station(at.st[0]).node, n);
        const StationId fresh = at.rw.add_station("T0b", n, StationSize::Small);
        at.rw.add_train(0, 4, {fresh, at.st[1]});
        at.rw.train_mut(0).route = {at.st[0]}; // the old line closes
        gather_at_stations(at.rw, at.eco, at.cargo, at.ind, 1850, when);
        return at.waiting(fresh, at.pax(), at.st[1]);
    };
    Towns keen({5, 25}, 30);
    keen.rw.add_train(0, 4, {keen.st[0], keen.st[1]});
    const std::int32_t sulking = made_by_new_station(w, day);
    const std::int32_t willing = made_by_new_station(keen, day);
    REQUIRE(willing > 0);
    CHECK(sulking * 100 <= willing * (default_balance().express.avoid_generation_percent + 5));
}
