#include "railmaster/sim/freight.hpp"

#include <algorithm>

namespace railmaster::sim {

namespace {

struct Cell {
    std::int32_t x, y;
};

Cell station_cell(const Economy& eco, const Railway& rw, const Station& s) {
    const MapPoint p = rw.track().node(s.node).pos;
    return {eco.cell_x(p), eco.cell_y(p)};
}

// Calls f(x, y) for every cell in a station's catchment, row by row.
template <typename F>
void for_catchment(const Economy& eco, const Railway& rw, const Station& s, F&& f) {
    const Cell c = station_cell(eco, rw, s);
    const std::int32_t r = catchment_radius(s.size);
    for (std::int32_t y = std::max(0, c.y - r); y <= std::min(eco.height() - 1, c.y + r); ++y) {
        for (std::int32_t x = std::max(0, c.x - r); x <= std::min(eco.width() - 1, c.x + r); ++x) f(x, y);
    }
}

void size_pool(Station& s, const CargoRegistry& cargo) {
    if (s.waiting.size() != cargo.all().size()) s.waiting.resize(cargo.all().size());
}

// For each cargo, the best price at any *other* stop of any train calling
// at `station`: what the cargo could be sold for if loaded here.
std::vector<std::int32_t> best_onward_prices(const Economy& eco, const Railway& rw, const CargoRegistry& cargo,
                                             StationId station,
                                             const std::vector<std::vector<std::int32_t>>& best_at) {
    std::vector<std::int32_t> best(cargo.all().size(), 0);
    for (const Train& t : rw.trains()) {
        if (std::find(t.route.begin(), t.route.end(), station) == t.route.end()) continue;
        for (StationId other : t.route) {
            if (other == station) continue;
            for (const CargoType& c : cargo.all()) {
                if (!eco.active(c.id)) continue;
                best[c.id] = std::max(best[c.id], best_at[other][c.id]);
            }
        }
    }
    return best;
}

} // namespace

std::int32_t catchment_radius(StationSize size) {
    switch (size) {
    case StationSize::Small: return provisional::kCatchmentSmall;
    case StationSize::Medium: return provisional::kCatchmentMedium;
    case StationSize::Large: return provisional::kCatchmentLarge;
    }
    return provisional::kCatchmentSmall;
}

CatchmentPrices catchment_prices(const Economy& eco, const Railway& rw, const Station& s, CargoId c) {
    CatchmentPrices out;
    bool first = true;
    for_catchment(eco, rw, s, [&](std::int32_t x, std::int32_t y) {
        const std::int32_t p = eco.price(c, x, y);
        if (first || p > out.best) {
            out = {p, x, y};
            first = false;
        }
    });
    return out;
}

std::int32_t value_left_permille(const CargoType& c, std::int32_t days) {
    const std::int64_t lost = std::int64_t{std::max(0, days)} * c.decay_sensitivity *
                              provisional::kTransitDecayPerMillePerSensitivity;
    return static_cast<std::int32_t>(std::max<std::int64_t>(0, 1000 - lost));
}

void gather_at_stations(Railway& rw, Economy& eco, const CargoRegistry& cargo) {
    const std::size_t n_stations = rw.stations().size();
    if (n_stations == 0) return;

    // Best selling price for each cargo at each station, computed once.
    std::vector<std::vector<std::int32_t>> best_at(n_stations, std::vector<std::int32_t>(cargo.all().size(), 0));
    for (StationId s = 0; s < n_stations; ++s) {
        for (const CargoType& c : cargo.all()) {
            if (eco.active(c.id)) best_at[s][c.id] = catchment_prices(eco, rw, rw.station(s), c.id).best;
        }
    }

    for (StationId sid = 0; sid < n_stations; ++sid) {
        Station& st = rw.station_mut(sid);
        size_pool(st, cargo);
        const std::vector<std::int32_t> onward = best_onward_prices(eco, rw, cargo, sid, best_at);
        for (const CargoType& c : cargo.all()) {
            if (onward[c.id] == 0) continue; // no train here could sell it anywhere
            WaitingCargo& pool = st.waiting[c.id];
            const std::int32_t threshold = static_cast<std::int32_t>(
                c.base_price.whole_dollars() * provisional::kTransportCostPercent / 100);
            for_catchment(eco, rw, st, [&](std::int32_t x, std::int32_t y) {
                const std::int32_t room = provisional::kStationCapMilli - pool.milli;
                const std::int32_t stock = eco.stock_milli(c.id, x, y);
                const std::int32_t price = eco.price(c.id, x, y);
                if (room <= 0 || stock <= 0 || price + threshold >= onward[c.id]) return;
                const std::int32_t want = std::min(room, std::max(1, stock * provisional::kGatherPercentPerDay / 100));
                const std::int32_t got = eco.take_stock(c.id, x, y, want);
                pool.milli += got;
                pool.value += std::int64_t{price} * got;
            });
        }
    }
}

Money handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, TrainId train_id, StationId station_id,
                     std::int32_t today, std::uint64_t tick) {
    Station& st = rw.station_mut(station_id);
    size_pool(st, cargo);
    Money income;

    // Unload whatever sells here for more than it cost.
    for (Car& car : rw.train_mut(train_id).cars) {
        if (!car.cargo) continue;
        const CargoType& c = cargo.get(*car.cargo);
        const std::int32_t left = value_left_permille(c, today - car.loaded_day);
        const CatchmentPrices here = catchment_prices(eco, rw, st, c.id);
        if (car.loaded_at != station_id && here.best > car.pickup_price) {
            const std::int64_t gain = std::int64_t{here.best - car.pickup_price};
            income += Money::dollars(gain * left / 1000 * car.milli / kMilli);
            eco.add_stock(c.id, here.best_cx, here.best_cy, car.milli);
            car = Car{};
        } else if (left == 0) {
            car = Car{}; // spoiled on the way: worthless, dumped
        }
    }

    // Load: the waiting cargo worth most at the train's other stops, a full
    // carload per car, while any remains. Cargo nobody further on wants stays.
    const Train& t = rw.train(train_id);
    struct Candidate {
        CargoId cargo;
        std::int32_t gain;
    };
    std::vector<Candidate> candidates;
    for (const CargoType& c : cargo.all()) {
        const WaitingCargo& pool = st.waiting[c.id];
        if (pool.milli < kMilli) continue;
        std::int32_t onward = 0;
        for (StationId other : t.route) {
            if (other != station_id) onward = std::max(onward, catchment_prices(eco, rw, rw.station(other), c.id).best);
        }
        const std::int32_t gain = onward - pool.average_price();
        if (gain > 0) candidates.push_back({c.id, gain});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });

    Train& train = rw.train_mut(train_id);
    std::size_t next = 0;
    for (Car& car : train.cars) {
        if (car.cargo) continue;
        while (next < candidates.size() && st.waiting[candidates[next].cargo].milli < kMilli) ++next;
        if (next == candidates.size()) break;
        WaitingCargo& pool = st.waiting[candidates[next].cargo];
        const std::int32_t price = pool.average_price();
        pool.milli -= kMilli;
        pool.value -= std::int64_t{price} * kMilli;
        car = Car{candidates[next].cargo, kMilli, price, today, station_id};
    }

    if (income > Money{}) {
        train.revenue += income;
        train.last_income = income;
        train.last_income_tick = tick;
    }
    return income;
}

} // namespace railmaster::sim
