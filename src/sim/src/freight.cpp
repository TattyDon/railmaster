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
    if (s.received_this_month.size() != cargo.all().size()) s.received_this_month.resize(cargo.all().size());
}

ExpressWaiting& express_pool(Station& s, CargoId c, StationId dest) {
    const auto key = [](const ExpressWaiting& e) { return std::pair{e.cargo, e.destination}; };
    auto it = std::lower_bound(s.express.begin(), s.express.end(), std::pair{c, dest},
                               [&](const ExpressWaiting& e, const std::pair<CargoId, std::uint32_t>& k) {
                                   return key(e) < k;
                               });
    if (it == s.express.end() || key(*it) != std::pair{c, dest}) it = s.express.insert(it, {c, dest, 0});
    return *it;
}

// Stations that share a train route with each station.
std::vector<std::vector<StationId>> route_partners(const Railway& rw) {
    std::vector<std::vector<StationId>> partners(rw.stations().size());
    for (const Train& t : rw.trains()) {
        if (t.state == TrainState::Crashed) continue;
        for (StationId a : t.route)
            for (StationId b : t.route)
                if (a != b) partners[a].push_back(b);
    }
    for (auto& p : partners) {
        std::sort(p.begin(), p.end());
        p.erase(std::unique(p.begin(), p.end()), p.end());
    }
    return partners;
}

bool on_route(const Train& t, StationId s) { return std::find(t.route.begin(), t.route.end(), s) != t.route.end(); }

// For each cargo, the best price at any *other* stop of any train calling
// at `station`: what the cargo could be sold for if loaded here.
std::vector<std::int32_t> best_onward_prices(const Economy& eco, const Railway& rw, const CargoRegistry& cargo,
                                             StationId station,
                                             const std::vector<std::vector<std::int32_t>>& best_at) {
    std::vector<std::int32_t> best(cargo.all().size(), 0);
    for (const Train& t : rw.trains()) {
        if (t.state == TrainState::Crashed) continue;
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

std::int64_t catchment_rate(const Economy& eco, const Railway& rw, const IndustryRegistry& industries,
                            const Station& s, CargoId c, bool outputs) {
    const MapPoint p = rw.track().node(s.node).pos;
    const std::int32_t cx = eco.cell_x(p), cy = eco.cell_y(p), r = catchment_radius(s.size);
    std::int64_t total = 0;
    for (const Site& site : eco.sites()) {
        if (std::abs(site.cx - cx) > r || std::abs(site.cy - cy) > r) continue;
        const IndustryType& t = industries.get(site.type);
        const bool has = outputs ? std::find(t.outputs.begin(), t.outputs.end(), c) != t.outputs.end()
                                 : std::any_of(t.inputs.begin(), t.inputs.end(),
                                               [&](const IndustryInput& in) { return in.cargo == c; });
        if (has) total += std::int64_t{t.rate_per_year} * site.level;
    }
    return total;
}

Money express_fare(const CargoType& c, const Railway& rw, StationId from, StationId to) {
    const std::int64_t mm = distance_mm(rw.track().node(rw.station(from).node).pos, rw.track().node(rw.station(to).node).pos);
    return Money::dollars(std::int64_t{c.fare_per_km} * mm / 1'000'000);
}

void start_new_month(Railway& rw) {
    for (StationId s = 0; s < rw.stations().size(); ++s) {
        auto& r = rw.station_mut(s).received_this_month;
        std::fill(r.begin(), r.end(), 0);
    }
}

namespace {

// exp(-0.0023 x) for x = 0..kDecaySteps, as Q30 fixed point, built by
// repeated integer multiplication so every platform gets the same table.
constexpr std::int64_t kDecayStepQ30 = 1'071'275'056; // round(exp(-0.0023) * 2^30)
constexpr std::size_t kDecaySteps = 3000;

const std::vector<std::int32_t>& decay_table() {
    static const std::vector<std::int32_t> table = [] {
        std::vector<std::int32_t> t(kDecaySteps + 1);
        std::int64_t v = std::int64_t{1} << 30;
        for (std::size_t i = 0; i <= kDecaySteps; ++i) {
            t[i] = static_cast<std::int32_t>(v * 1000 >> 30);
            v = v * kDecayStepQ30 >> 30;
        }
        return t;
    }();
    return table;
}

} // namespace

std::int32_t value_left_permille(const CargoType& c, std::int32_t days) {
    const std::int64_t x = std::int64_t{std::max(0, days)} * c.decay_sensitivity;
    if (x > static_cast<std::int64_t>(kDecaySteps)) return 0;
    const std::int32_t v = decay_table()[static_cast<std::size_t>(x)];
    return v < provisional::kExpiredPermille ? 0 : v;
}

std::int32_t difficulty_revenue_permille(Difficulty d) {
    switch (d) {
    case Difficulty::Easy: return 1200;
    case Difficulty::Medium: return 1000;
    case Difficulty::Hard: return 900;
    case Difficulty::Expert: return 800;
    }
    return 1000;
}

std::int32_t station_age_permille(std::int32_t days, bool open_country) {
    constexpr std::int64_t kYear = 365;
    const std::int64_t d = std::max(0, days);
    std::int64_t bonus; // permille above or below 1000
    if (d <= 4 * kYear) bonus = 150 - 150 * d / (4 * kYear);
    else if (d <= 20 * kYear) bonus = -100 * (d - 4 * kYear) / (16 * kYear);
    else bonus = -100;
    if (open_country) bonus /= 2;
    return static_cast<std::int32_t>(1000 + bonus);
}

void gather_at_stations(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        std::int32_t year) {
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
            // A station has one price per cargo, the best in its catchment, used
            // both to buy here and to sell here. Pricing pickups by the cheapest
            // cell instead would pay out on the spread within a town.
            const std::int32_t price = best_at[sid][c.id];
            if (price + threshold >= onward[c.id]) continue;
            for_catchment(eco, rw, st, [&](std::int32_t x, std::int32_t y) {
                const std::int32_t room = provisional::kStationCapMilli - pool.milli;
                const std::int32_t stock = eco.stock_milli(c.id, x, y);
                if (room <= 0 || stock <= 0) return;
                const std::int32_t want = std::min(room, std::max(1, stock * provisional::kGatherPercentPerDay / 100));
                const std::int32_t got = eco.take_stock(c.id, x, y, want);
                pool.milli += got;
                pool.value += std::int64_t{price} * got;
            });
        }
    }

    // Express: waiting loads dwindle, then houses and barracks add new ones
    // for every station a train connects them to.
    for (StationId sid = 0; sid < n_stations; ++sid) {
        for (ExpressWaiting& e : rw.station_mut(sid).express) {
            const std::int32_t per_mille =
                cargo.get(e.cargo).decay_sensitivity * provisional::kExpressWaitLossPerMillePerSensitivity;
            if (e.milli > 0) e.milli -= std::max(1, e.milli * per_mille / 1000);
        }
    }
    const auto partners = route_partners(rw);
    for (const CargoType& c : cargo.all()) {
        if (c.cargo_class != CargoClass::Express || c.generation <= 0 || year < c.available_year) continue;
        std::vector<std::int64_t> attraction(n_stations);
        for (StationId s = 0; s < n_stations; ++s) {
            attraction[s] = catchment_rate(eco, rw, industries, rw.station(s), c.id, false);
        }
        for (StationId sid = 0; sid < n_stations; ++sid) {
            const std::int64_t produced = catchment_rate(eco, rw, industries, rw.station(sid), c.id, true);
            if (produced == 0) continue;
            const std::int64_t daily = produced * c.generation * kMilli / 365;
            for (StationId dest : partners[sid]) {
                const std::int64_t a = attraction[dest];
                if (a == 0) continue;
                ExpressWaiting& pool = express_pool(rw.station_mut(sid), c.id, dest);
                const auto add = static_cast<std::int32_t>(daily * a / (a + provisional::kExpressAttractionHalf));
                pool.milli = std::min(provisional::kExpressCapMilli, pool.milli + add);
            }
        }
    }
}

Earnings handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        TrainId train_id, StationId station_id, std::int32_t today, std::uint64_t tick,
                        std::int32_t revenue_permille) {
    Station& st = rw.station_mut(station_id);
    size_pool(st, cargo);
    Earnings income;

    // Unload express loads that have arrived, and freight that sells here
    // for more than it cost.
    for (Car& car : rw.train_mut(train_id).cars) {
        if (!car.cargo) continue;
        const CargoType& c = cargo.get(*car.cargo);
        const std::int32_t left = value_left_permille(c, today - car.loaded_day);
        if (car.destination) {
            if (*car.destination == station_id) {
                std::int32_t& received = st.received_this_month[c.id];
                const std::int64_t cap = catchment_rate(eco, rw, industries, st, c.id, false) * kMilli / 12 *
                                         provisional::kMailCapMonths;
                if (!c.demand_cap || received < cap) {
                    income.add(c.id, express_fare(c, rw, car.loaded_at, station_id)
                                         .scaled(std::int64_t{left} * car.milli, 1000LL * kMilli)
                                         .scaled(revenue_permille, 1000));
                }
                received += car.milli;
                car = Car{};
            } else if (left == 0) {
                car = Car{}; // gave up: worthless
            }
            continue;
        }
        const CatchmentPrices here = catchment_prices(eco, rw, st, c.id);
        if (car.loaded_at != station_id && here.best > car.pickup_price) {
            const std::int64_t gain = std::int64_t{here.best - car.pickup_price};
            income.add(c.id,
                       Money::dollars(gain * left / 1000 * car.milli / kMilli).scaled(revenue_permille, 1000));
            eco.add_stock(c.id, here.best_cx, here.best_cy, car.milli);
            car = Car{};
        } else if (left == 0) {
            car = Car{}; // expired on the way: worthless, dumped
        }
    }

    // Load: the waiting cargo worth most at the train's other stops, a full
    // carload per car, while any remains. Cargo nobody further on wants stays.
    const Train& t = rw.train(train_id);
    struct Candidate {
        CargoId cargo;
        std::optional<StationId> destination;
        std::int64_t gain;
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
        if (gain > 0) candidates.push_back({c.id, std::nullopt, gain});
    }
    for (const ExpressWaiting& e : st.express) {
        if (e.milli < kMilli || !on_route(t, e.destination)) continue;
        candidates.push_back({e.cargo, e.destination,
                              express_fare(cargo.get(e.cargo), rw, station_id, e.destination).whole_dollars()});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });

    Train& train = rw.train_mut(train_id);
    const auto waiting_milli = [&](const Candidate& c) {
        return c.destination ? express_pool(st, c.cargo, *c.destination).milli : st.waiting[c.cargo].milli;
    };
    std::size_t next = 0;
    for (Car& car : train.cars) {
        if (car.cargo) continue;
        while (next < candidates.size() && waiting_milli(candidates[next]) < kMilli) ++next;
        if (next == candidates.size()) break;
        const Candidate& pick = candidates[next];
        if (pick.destination) {
            express_pool(st, pick.cargo, *pick.destination).milli -= kMilli;
            car = Car{pick.cargo, kMilli, 0, today, station_id, pick.destination, static_cast<std::int32_t>(pick.gain)};
        } else {
            WaitingCargo& pool = st.waiting[pick.cargo];
            const std::int32_t price = pool.average_price();
            pool.milli -= kMilli;
            pool.value -= std::int64_t{price} * kMilli;
            car = Car{pick.cargo, kMilli, price, today, station_id, std::nullopt, static_cast<std::int32_t>(pick.gain)};
        }
    }

    if (income.total > Money{}) {
        train.revenue += income.total;
        train.last_income = income.total;
        train.last_income_tick = tick;
    }
    return income;
}

} // namespace railmaster::sim
