#include "railmaster/sim/freight.hpp"

#include "railmaster/sim/fixed_math.hpp"

#include <algorithm>
#include <limits>
#include <map>

namespace railmaster::sim {

namespace {

// Calls f(x, y) for every economy node in a station's catchment, row by
// row: those whose centre is within the radius of the station, then the
// nodes of industries whose footprint it reaches.
template <typename F>
void for_catchment(const Economy& eco, const Railway& rw, const Station& s, F&& f) {
    const MapPoint p = rw.track().node(s.node).pos;
    eco.for_reach(p, eco.cells_mm(catchment_radius(s.size, rw.balance())), f);
}

// The same nodes as a list, to walk many times (once for each cargo).
std::vector<std::pair<std::int32_t, std::int32_t>> catchment_nodes(const Economy& eco, const Railway& rw, const Station& s) {
    std::vector<std::pair<std::int32_t, std::int32_t>> out;
    for_catchment(eco, rw, s, [&](std::int32_t x, std::int32_t y) { out.emplace_back(x, y); });
    return out;
}

CatchmentPrices best_price(const Economy& eco, const std::vector<std::pair<std::int32_t, std::int32_t>>& nodes, CargoId c) {
    CatchmentPrices out;
    bool first = true;
    for (const auto& [x, y] : nodes) {
        const std::int32_t p = eco.price(c, x, y);
        if (first || p > out.best) {
            out = {p, x, y};
            first = false;
        }
    }
    return out;
}

void size_pool(Station& s, const CargoRegistry& cargo) {
    if (s.waiting.size() != cargo.all().size()) s.waiting.resize(cargo.all().size());
    if (s.received_this_month.size() != cargo.all().size()) s.received_this_month.resize(cargo.all().size());
}

ExpressWaiting& express_pool(Station& s, CargoId c, SiteId home) {
    const auto key = [](const ExpressWaiting& e) { return std::pair{e.cargo, e.home}; };
    auto it = std::lower_bound(s.express.begin(), s.express.end(), std::pair{c, home},
                               [&](const ExpressWaiting& e, const std::pair<CargoId, std::uint32_t>& k) {
                                   return key(e) < k;
                               });
    if (it == s.express.end() || key(*it) != std::pair{c, home}) it = s.express.insert(it, {c, home, 0});
    return *it;
}

// A home's yearly rate, in thousandths of a carload: what it makes (or
// takes) of each express cargo it deals in.
std::int64_t home_rate(const IndustryRegistry& industries, const Site& s) {
    return std::int64_t{industries.get(s.type).rate_milli} * s.level;
}

// The homes in reach of each station for an express cargo: the sites that
// make it (where travellers set out from) and those that take it (where
// they are going), houses and barracks (rt3-clone-spec §8.3 [D]).
struct Homes {
    std::vector<std::vector<SiteId>> makes, takes; // by StationId
};
Homes express_homes(const Economy& eco, const Railway& rw, const IndustryRegistry& industries, CargoId c) {
    const std::size_t n = rw.stations().size();
    Homes out{std::vector<std::vector<SiteId>>(n), std::vector<std::vector<SiteId>>(n)};
    std::vector<MapPoint> at(n);
    std::vector<std::int64_t> r_mm(n);
    for (StationId s = 0; s < n; ++s) {
        at[s] = rw.track().node(rw.station(s).node).pos;
        r_mm[s] = eco.cells_mm(catchment_radius(rw.station(s).size, rw.balance()));
    }
    for (const Site& site : eco.sites()) {
        const IndustryType& t = industries.get(site.type);
        const bool makes = std::find(t.outputs.begin(), t.outputs.end(), c) != t.outputs.end();
        const bool takes = std::any_of(t.inputs.begin(), t.inputs.end(), [&](const IndustryInput& in) { return in.cargo == c; });
        if (!makes && !takes) continue;
        for (StationId s = 0; s < n; ++s) {
            if (!eco.reaches(site, at[s], r_mm[s])) continue;
            if (makes) out.makes[s].push_back(site.id);
            if (takes) out.takes[s].push_back(site.id);
        }
    }
    return out;
}

// Stations that share a train route with each station.
std::vector<std::vector<StationId>> route_partners(const Railway& rw) {
    std::vector<std::vector<StationId>> partners(rw.stations().size());
    for (const Train& t : rw.trains()) {
        if (!t.in_service()) continue;
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



// How many trains it takes from every station to the nearest of `to` (0
// there; -1 where no run of trains reaches one): a breadth-first walk over
// stations that share a train.
std::vector<std::int32_t> legs_to_any(const std::vector<std::vector<StationId>>& partners, const std::vector<StationId>& to) {
    std::vector<std::int32_t> legs(partners.size(), -1);
    std::vector<StationId> frontier;
    for (StationId s : to) {
        if (s >= legs.size() || legs[s] == 0) continue;
        legs[s] = 0;
        frontier.push_back(s);
    }
    while (!frontier.empty()) {
        std::vector<StationId> next;
        for (StationId s : frontier) {
            for (StationId p : partners[s]) {
                if (legs[p] >= 0) continue;
                legs[p] = legs[s] + 1;
                next.push_back(p);
            }
        }
        frontier = std::move(next);
    }
    return legs;
}

// Where express from each station can go: every station within max_legs
// trains (rt3-clone-spec §8.3 [D]: travellers go to a hub to catch a train
// on; how many changes is [I]).
std::vector<std::vector<StationId>> express_reach(const std::vector<std::vector<StationId>>& partners, std::int32_t max_legs) {
    std::vector<std::vector<StationId>> reach(partners.size());
    for (std::size_t to = 0; to < partners.size(); ++to) {
        const std::vector<std::int32_t> legs = legs_to_any(partners, {static_cast<StationId>(to)});
        for (std::size_t s = 0; s < partners.size(); ++s) {
            if (s != to && legs[s] > 0 && legs[s] <= max_legs) reach[s].push_back(static_cast<StationId>(to));
        }
    }
    return reach;
}

// For each cargo, the best price at any *other* stop of any train calling
// at `station`: what the cargo could be sold for if loaded here.
std::vector<std::int32_t> best_onward_prices(const Economy& eco, const Railway& rw, const CargoRegistry& cargo,
                                             StationId station,
                                             const std::vector<std::vector<std::int32_t>>& best_at) {
    std::vector<std::int32_t> best(cargo.all().size(), 0);
    for (const Train& t : rw.trains()) {
        if (!t.in_service()) continue;
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

std::int32_t catchment_radius(StationSize size, const Balance& b) {
    switch (size) {
    case StationSize::Small: return b.stations.catchment_small;
    case StationSize::Medium: return b.stations.catchment_medium;
    case StationSize::Large: return b.stations.catchment_large;
    }
    return b.stations.catchment_small;
}

CatchmentPrices catchment_prices(const Economy& eco, const Railway& rw, const Station& s, CargoId c) {
    return best_price(eco, catchment_nodes(eco, rw, s), c);
}

std::int64_t catchment_rate(const Economy& eco, const Railway& rw, const IndustryRegistry& industries,
                            const Station& s, CargoId c, bool outputs) {
    const MapPoint p = rw.track().node(s.node).pos;
    const std::int64_t r_mm = eco.cells_mm(catchment_radius(s.size, rw.balance()));
    std::int64_t total = 0;
    for (const Site& site : eco.sites()) {
        if (!eco.reaches(site, p, r_mm)) continue;
        const IndustryType& t = industries.get(site.type);
        const bool has = outputs ? std::find(t.outputs.begin(), t.outputs.end(), c) != t.outputs.end()
                                 : std::any_of(t.inputs.begin(), t.inputs.end(),
                                               [&](const IndustryInput& in) { return in.cargo == c; });
        if (has) total += t.rate_milli * site.level;
    }
    return total;
}

HomesAvoiding homes_avoiding(const Economy& eco, const Railway& rw, const IndustryRegistry& industries, StationId s,
                             CargoId c, std::int32_t today) {
    HomesAvoiding out;
    const Station& st = rw.station(s);
    const MapPoint p = rw.track().node(st.node).pos;
    const std::int64_t r_mm = eco.cells_mm(catchment_radius(st.size, rw.balance()));
    for (const Site& site : eco.sites()) {
        const IndustryType& t = industries.get(site.type);
        if (std::find(t.outputs.begin(), t.outputs.end(), c) == t.outputs.end() || !eco.reaches(site, p, r_mm)) continue;
        ++out.homes;
        if (!rw.home_avoids(site.id, c, today)) continue;
        ++out.avoiding;
        out.until = std::max(out.until, rw.home_avoided_until().at(site.id)[c]);
    }
    return out;
}

Money express_base_fare(const CargoType& c, std::int64_t distance_mm, const Balance& b) {
    const std::int64_t ref_mm = std::int64_t{b.express.fare_reference_km} * 1'000'000;
    const std::int64_t scaled_mm = ref_mm * pow_ratio_permille(distance_mm, ref_mm, b.express.fare_distance_exponent_permille) / 1000;
    return Money::dollars(std::int64_t{c.fare_per_km} * (distance_mm > 0 ? scaled_mm : 0) / 1'000'000);
}

Money express_fare(const CargoType& c, const Railway& rw, StationId from, StationId to) {
    const std::int64_t mm = distance_mm(rw.track().node(rw.station(from).node).pos, rw.track().node(rw.station(to).node).pos);
    return express_base_fare(c, mm, rw.balance());
}

std::int32_t express_era_percent(std::int32_t year, const Balance& b) {
    const auto& r = b.express.era_volume_percent;
    if (year <= r.front().year) return r.front().percent;
    for (std::size_t i = 1; i < r.size(); ++i) {
        if (year > r[i].year) continue;
        return r[i - 1].percent + (r[i].percent - r[i - 1].percent) * (year - r[i - 1].year) / (r[i].year - r[i - 1].year);
    }
    return r.back().percent;
}

std::int32_t reference_speed_mph(std::int32_t year, const Balance& b) {
    const auto& r = b.express.reference_speeds;
    if (year <= r.front().year) return r.front().mph;
    for (std::size_t i = 1; i < r.size(); ++i) {
        if (year > r[i].year) continue;
        return r[i - 1].mph + (r[i].mph - r[i - 1].mph) * (year - r[i - 1].year) / (r[i].year - r[i - 1].year);
    }
    return r.back().mph;
}

std::int32_t express_speed_permille(std::int64_t distance_mm, std::uint64_t ticks, std::int32_t year, const Balance& b) {
    const std::int64_t per_tick = std::max<std::int64_t>(1, mph_to_mm_per_tick(reference_speed_mph(year, b), b));
    const std::int64_t actual = std::max<std::int64_t>(1, static_cast<std::int64_t>(ticks));
    // Ideal ticks over actual ticks, in thousandths.
    const std::int64_t f = distance_mm * 1000 / per_tick / actual;
    return static_cast<std::int32_t>(
        std::clamp<std::int64_t>(f, b.express.speed_factor_min_permille, b.express.speed_factor_max_permille));
}

void start_new_month(Railway& rw, const Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                     std::int32_t today) {
    const Balance::Express& b = rw.balance().express;
    // What each station's travellers lost waiting last month is laid at the
    // doors of the homes that made them, in proportion to what each made
    // there. A home that lost too many avoids the railway for a while
    // (rt3-clone-spec §8.3 [D]), whichever station they tried.
    for (const CargoType& c : cargo.all()) {
        if (c.cargo_class != CargoClass::Express) continue;
        const auto made_at = [&](StationId s) {
            const Station& st = rw.station(s);
            return c.id < st.express_made_month.size() ? std::int64_t{st.express_made_month[c.id]} : 0;
        };
        bool any = false;
        for (StationId s = 0; s < rw.stations().size(); ++s) any |= made_at(s) > 0;
        if (!any) continue;
        const Homes homes = express_homes(eco, rw, industries, c.id);
        std::map<SiteId, std::int32_t> sharing;
        for (StationId s = 0; s < rw.stations().size(); ++s)
            if (made_at(s) > 0)
                for (SiteId h : homes.makes[s]) ++sharing[h];
        std::map<SiteId, std::pair<std::int64_t, std::int64_t>> tally; // made, lost
        for (StationId s = 0; s < rw.stations().size(); ++s) {
            const std::int64_t made = made_at(s);
            if (made <= 0) continue;
            const Station& st = rw.station(s);
            const std::int64_t lost = c.id < st.express_lost_month.size() ? st.express_lost_month[c.id] : 0;
            std::int64_t total = 0;
            for (SiteId h : homes.makes[s]) total += home_rate(industries, eco.sites()[h]) / sharing[h];
            if (total <= 0) continue;
            for (SiteId h : homes.makes[s]) {
                const std::int64_t w = home_rate(industries, eco.sites()[h]) / sharing[h];
                tally[h].first += made * w / total;
                tally[h].second += lost * w / total;
            }
        }
        for (const auto& [h, ml] : tally) {
            if (ml.first < kMilli || ml.second * 100 <= ml.first * b.avoid_lost_percent) continue;
            std::vector<std::int32_t>& until = rw.home_avoided_until_mut()[h];
            if (until.size() <= c.id) until.resize(std::size_t{c.id} + 1, 0);
            until[c.id] = std::max(until[c.id], today + b.avoid_days);
        }
    }
    // Homes whose time is up are forgotten.
    std::erase_if(rw.home_avoided_until_mut(), [&](const auto& kv) {
        return std::all_of(kv.second.begin(), kv.second.end(), [&](std::int32_t d) { return d <= today; });
    });
    for (StationId s = 0; s < rw.stations().size(); ++s) {
        Station& st = rw.station_mut(s);
        std::fill(st.received_this_month.begin(), st.received_this_month.end(), 0);
        std::fill(st.express_made_month.begin(), st.express_made_month.end(), 0);
        std::fill(st.express_lost_month.begin(), st.express_lost_month.end(), 0);
    }
}

namespace {

// exp(-k x) for x = 0..kDecaySteps, in thousandths, built by repeated
// integer multiplication of a Q30 factor so every platform gets the same
// table. One table per decay rate in use, cached.
constexpr std::size_t kDecaySteps = 3000;

const std::vector<std::int32_t>& decay_table(std::int64_t step_q30) {
    static std::map<std::int64_t, std::vector<std::int32_t>> tables;
    auto [it, fresh] = tables.try_emplace(step_q30);
    if (fresh) {
        std::vector<std::int32_t>& t = it->second;
        t.resize(kDecaySteps + 1);
        std::int64_t v = std::int64_t{1} << 30;
        for (std::size_t i = 0; i <= kDecaySteps; ++i) {
            t[i] = static_cast<std::int32_t>(v * 1000 >> 30);
            v = v * step_q30 >> 30;
        }
    }
    return it->second;
}

} // namespace

std::int32_t value_left_permille(const CargoType& c, std::int32_t days, const Balance& b) {
    const std::int64_t x = std::int64_t{std::max(0, days)} * c.decay_sensitivity;
    if (x > static_cast<std::int64_t>(kDecaySteps)) return 0;
    const std::int32_t v = decay_table(b.decay_step_q30())[static_cast<std::size_t>(x)];
    return v < b.freight.expired_permille ? 0 : v;
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
                        std::int32_t year, std::int32_t today) {
    const std::size_t n_stations = rw.stations().size();
    if (n_stations == 0) return;

    // Each station's catchment, and the best selling price for each cargo
    // there, computed once.
    std::vector<std::vector<std::pair<std::int32_t, std::int32_t>>> catchment(n_stations);
    std::vector<std::vector<std::int32_t>> best_at(n_stations, std::vector<std::int32_t>(cargo.all().size(), 0));
    for (StationId s = 0; s < n_stations; ++s) {
        catchment[s] = catchment_nodes(eco, rw, rw.station(s));
        for (const CargoType& c : cargo.all()) {
            if (eco.active(c.id)) best_at[s][c.id] = best_price(eco, catchment[s], c.id).best;
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
                c.base_price.whole_dollars() * rw.balance().economy.transport_cost_percent / 100);
            // A station has one price per cargo, the best in its catchment, used
            // both to buy here and to sell here. Pricing pickups by the cheapest
            // cell instead would pay out on the spread within a town.
            const std::int32_t price = best_at[sid][c.id];
            if (price + threshold >= onward[c.id]) continue;
            for (const auto& [x, y] : catchment[sid]) {
                const std::int32_t room = rw.balance().stations.cap_milli - pool.milli;
                const std::int32_t stock = eco.stock_milli(c.id, x, y);
                if (room <= 0 || stock <= 0) continue;
                const std::int32_t want =
                    std::min(room, std::max(1, stock * rw.balance().stations.gather_percent_per_day / 100));
                const std::int32_t got = eco.take_stock(c.id, x, y, want);
                pool.milli += got;
                pool.value += std::int64_t{price} * got;
            }
        }
    }

    // Express: waiting loads dwindle, then houses and barracks add new ones
    // for every station a train connects them to.
    for (StationId sid = 0; sid < n_stations; ++sid) {
        // A post office keeps mail, and a hotel passengers, waiting longer [D].
        const bool post_office = rw.near_building(sid, StationBuildingType::PostOffice);
        const bool hotel = rw.near_building(sid, StationBuildingType::Hotel);
        Station& st = rw.station_mut(sid);
        for (ExpressWaiting& e : st.express) {
            const CargoType& c = cargo.get(e.cargo);
            std::int32_t per_mille = c.decay_sensitivity * rw.balance().express.wait_loss_per_mille_per_sensitivity;
            if ((c.key == "mail" && post_office) || (c.key == "passengers" && hotel)) {
                per_mille = per_mille * rw.balance().stations.wait_loss_percent / 100;
            }
            if (e.milli > 0) {
                // Rounded up or down by a fixed pattern over the days, so a
                // small pool loses its share on average rather than nothing
                // (or a whole thousandth) every day.
                const auto dither = static_cast<std::int32_t>((std::uint32_t(today) * 7919u + e.home * 104729u + e.cargo * 31u) % 1000u);
                const std::int32_t gone = std::min(e.milli, (e.milli * per_mille + dither) / 1000);
                e.milli -= gone;
                if (st.express_lost_month.size() <= e.cargo) st.express_lost_month.resize(std::size_t{e.cargo} + 1, 0);
                st.express_lost_month[e.cargo] += gone;
            }
            if (c.key == "passengers") st.passengers_waiting_milli_days += e.milli;
        }
    }
    const auto reach = express_reach(route_partners(rw), rw.balance().express.max_legs);
    const std::int32_t era_percent = express_era_percent(year, rw.balance());
    const auto& xb = rw.balance().express;
    const std::int64_t min_mm = std::int64_t{std::max(1, xb.gravity_min_km)} * 1'000'000;
    const std::int64_t half = std::int64_t{xb.attraction_half} * kMilli;
    std::vector<char> seen(eco.sites().size());
    for (const CargoType& c : cargo.all()) {
        if (c.cargo_class != CargoClass::Express || c.generation <= 0 || year < c.available_year) continue;
        const Homes homes = express_homes(eco, rw, industries, c.id);
        // Travellers set out from homes, each using a station that reaches
        // their home (rt3-clone-spec §7.1 [D]); a home in reach of several
        // shares its travellers among those with somewhere to go.
        std::map<SiteId, std::int32_t> sharing;
        for (StationId s = 0; s < n_stations; ++s)
            if (!reach[s].empty())
                for (SiteId h : homes.makes[s]) ++sharing[h];
        for (StationId sid = 0; sid < n_stations; ++sid) {
            if (reach[sid].empty()) continue;
            std::int64_t produced = 0; // already in thousandths
            for (SiteId h : homes.makes[sid]) {
                std::int64_t r = home_rate(industries, eco.sites()[h]) / sharing[h];
                // A home that gave up on the railway makes fewer travellers for a while.
                if (rw.home_avoids(h, c.id, today)) r = r * xb.avoid_generation_percent / 100;
                produced += r;
            }
            if (produced == 0) continue;
            const std::int64_t daily = produced * c.generation * c.demand_percent / 100 * era_percent / 100 / 365;
            // Where they go: the homes in reach of the stations a train
            // connects this one to, each once, and none this one reaches
            // itself. How many in all: more for every station with homes to
            // go to, each saturating [I]; shared among the homes by gravity,
            // pull over distance^1.5 (rt3-clone-spec §8.3 [I]).
            std::fill(seen.begin(), seen.end(), 0);
            for (SiteId h : homes.takes[sid]) seen[h] = 1;
            const MapPoint from = rw.track().node(rw.station(sid).node).pos;
            std::int64_t total = 0, weights = 0;
            std::vector<std::pair<SiteId, std::int64_t>> dest;
            for (StationId r : reach[sid]) {
                std::int64_t pull = 0;
                for (SiteId h : homes.takes[r]) {
                    if (seen[h]) continue;
                    seen[h] = 1;
                    const Site& site = eco.sites()[h];
                    const std::int64_t a = home_rate(industries, site);
                    if (a == 0) continue;
                    pull += a;
                    const std::int64_t mm = std::max(min_mm, distance_mm(from, eco.node_centre(site.cx, site.cy)));
                    const std::int64_t w =
                        a * 1000 / std::max<std::int64_t>(1000, pow_ratio_permille(mm, min_mm, xb.gravity_exponent_permille));
                    dest.emplace_back(h, w);
                    weights += w;
                }
                if (pull > 0) total += daily * pull / (pull + half);
            }
            if (weights == 0) continue;
            Station& origin = rw.station_mut(sid);
            if (origin.express_made_month.size() <= c.id) origin.express_made_month.resize(std::size_t{c.id} + 1, 0);
            if (origin.express_lost_month.size() <= c.id) origin.express_lost_month.resize(std::size_t{c.id} + 1, 0);
            // The day's travellers, a thousandth at a time, each to the home
            // whose share of the weights it falls in, starting at a point
            // that moves from day to day: every home gets its share exactly
            // on average, however small, and none is lost to rounding.
            std::vector<std::int32_t> adds(dest.size(), 0);
            const auto phase = static_cast<std::int64_t>((std::uint32_t(today) * 7919u + sid * 104729u + c.id * 31u) % 1000u);
            std::size_t i = 0;
            std::int64_t below = dest.empty() ? 0 : dest[0].second; // weights up to and including home i
            for (std::int64_t k = 0; k < total; ++k) {
                const std::int64_t point = (k * 1000 + phase) * weights / (total * 1000);
                while (i + 1 < dest.size() && point >= below) below += dest[++i].second;
                ++adds[i];
            }
            for (std::size_t j = 0; j < dest.size(); ++j) {
                const std::int32_t add = adds[j];
                if (add == 0) continue;
                ExpressWaiting& pool = express_pool(origin, c.id, dest[j].first);
                const std::int32_t kept = std::min(xb.cap_milli, pool.milli + add) - pool.milli;
                pool.milli += kept;
                origin.express_made_month[c.id] += add;
                origin.express_lost_month[c.id] += add - kept; // no room to wait
            }
        }
    }
}

Earnings handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        TrainId train_id, StationId station_id, std::int32_t today, std::uint64_t tick,
                        std::int32_t revenue_permille, std::int32_t appeal_percent) {
    const std::int32_t year = Date::from_days(today).year();
    Station& st = rw.station_mut(station_id);
    size_pool(st, cargo);
    Earnings income;

    // Unload express loads that have arrived, and freight that sells here
    // for more than it cost.
    for (Car& car : rw.train_mut(train_id).cars) {
        if (!car.cargo) continue;
        const CargoType& c = cargo.get(*car.cargo);
        const std::int32_t left = value_left_permille(c, today - car.loaded_day, rw.balance());
        if (car.destination) {
            if (car.via && *car.via == station_id && *car.destination != station_id) {
                // A change of trains: paid for this leg, then they wait here
                // with the others for their destination [I].
                const std::int64_t mm = distance_mm(rw.track().node(rw.station(car.loaded_at).node).pos,
                                                    rw.track().node(st.node).pos);
                const std::int32_t speed =
                    express_speed_permille(mm, tick - std::min(tick, car.loaded_tick), year, rw.balance());
                const std::int32_t appeal = c.key == "passengers" || c.key == "mail" ? 100 + appeal_percent : 100;
                const std::int32_t diner = c.key == "passengers" && rw.train(train_id).diner ? kDinerPassengerPercent : 100;
                income.add(c.id, express_base_fare(c, mm, rw.balance())
                                     .scaled(std::int64_t{left} * car.milli, 1000LL * kMilli)
                                     .scaled(revenue_permille, 1000)
                                     .scaled(diner, 100)
                                     .scaled(appeal, 100)
                                     .scaled(speed, 1000));
                if (left > 0 && car.home) {
                    std::int32_t& pool = express_pool(st, c.id, *car.home).milli;
                    pool = std::min(rw.balance().express.cap_milli, pool + car.milli);
                }
                car = Car{};
                continue;
            }
            if (*car.destination == station_id) {
                std::int32_t& received = st.received_this_month[c.id];
                const std::int64_t cap = catchment_rate(eco, rw, industries, st, c.id, false) / 12 *
                                         rw.balance().express.mail_cap_months;
                if (!c.demand_cap || received < cap) {
                    // A dining car makes passengers pay more [D], and a
                    // handsome engine passengers and mail [D]; faster trips
                    // pay more [D].
                    const std::int32_t diner = c.key == "passengers" && rw.train(train_id).diner ? kDinerPassengerPercent : 100;
                    const std::int32_t appeal = c.key == "passengers" || c.key == "mail" ? 100 + appeal_percent : 100;
                    const std::int64_t mm = distance_mm(rw.track().node(rw.station(car.loaded_at).node).pos,
                                                        rw.track().node(st.node).pos);
                    const std::int32_t speed = express_speed_permille(mm, tick - std::min(tick, car.loaded_tick), year,
                                                                      rw.balance());
                    income.add(c.id, express_base_fare(c, mm, rw.balance())
                                         .scaled(std::int64_t{left} * car.milli, 1000LL * kMilli)
                                         .scaled(revenue_permille, 1000)
                                         .scaled(diner, 100)
                                         .scaled(appeal, 100)
                                         .scaled(speed, 1000));
                }
                received += car.milli;
                income.delivered.push_back({c.id, car.milli, car.loaded_at});
                if (c.key == "passengers") st.passengers_arrived_milli += car.milli;
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
            income.delivered.push_back({c.id, car.milli, car.loaded_at});
            car = Car{};
        } else if (left == 0) {
            car = Car{}; // expired on the way: worthless, dumped
        }
    }

    // Load, by the consist rule for this stop (rt3-clone-spec §9.3): the
    // waiting cargo of the allowed kinds worth most at the train's other
    // stops, a full carload per car, up to the rule's cars. Cargo nobody
    // further on wants stays. Express cars may leave part-full, from the
    // minimum load [C: load fraction 0.5-1.0], since passengers and mail will
    // not wait for ever.
    const std::int32_t express_min = rw.balance().express.min_load_milli;
    const Train& t = rw.train(train_id);
    const std::size_t stop = t.stop_index;
    const ConsistRule rule = stop < t.rules.size() ? t.rules[stop] : ConsistRule{};
    const auto allowed = [&](const CargoType& c) {
        if (rule.custom) return std::find(rule.cars.begin(), rule.cars.end(), c.id) != rule.cars.end();
        if (rule.filter == CargoFilter::Freight) return c.cargo_class == CargoClass::Freight;
        if (rule.filter == CargoFilter::Express) return c.cargo_class == CargoClass::Express;
        return true;
    };
    struct Candidate {
        CargoId cargo;
        std::optional<StationId> destination;
        std::int64_t gain;
        std::optional<StationId> via{};
        std::vector<SiteId> homes{}; // express: the homes of those going this way, who share cars
    };
    const auto partners = route_partners(rw);
    // For each home travellers here are going to: the stations that reach
    // it (any of them will do), and how many trains it takes from every
    // station to the nearest of those.
    struct Ways {
        std::vector<StationId> stations;
        std::vector<std::int32_t> legs;
    };
    std::map<SiteId, Ways> ways_cache;
    const auto ways_to = [&](SiteId home) -> const Ways& {
        auto it = ways_cache.find(home);
        if (it != ways_cache.end()) return it->second;
        Ways w;
        const Site& site = eco.sites()[home];
        for (StationId s = 0; s < rw.stations().size(); ++s) {
            const Station& other = rw.station(s);
            if (eco.reaches(site, rw.track().node(other.node).pos, eco.cells_mm(catchment_radius(other.size, rw.balance())))) {
                w.stations.push_back(s);
            }
        }
        w.legs = legs_to_any(partners, w.stations);
        return ways_cache.emplace(home, std::move(w)).first->second;
    };
    std::vector<Candidate> candidates;
    for (const CargoType& c : cargo.all()) {
        const WaitingCargo& pool = st.waiting[c.id];
        if (pool.milli < kMilli || !allowed(c)) continue;
        std::int32_t onward = 0;
        for (StationId other : t.route) {
            if (other != station_id) onward = std::max(onward, catchment_prices(eco, rw, rw.station(other), c.id).best);
        }
        const std::int32_t gain = onward - pool.average_price();
        if (gain > 0) candidates.push_back({c.id, std::nullopt, gain});
    }
    for (const ExpressWaiting& e : st.express) {
        if (e.milli <= 0 || !allowed(cargo.get(e.cargo)) || e.home >= eco.sites().size()) continue;
        const Ways& ways = ways_to(e.home);
        const Site& home = eco.sites()[e.home];
        const MapPoint home_at = eco.node_centre(home.cx, home.cy);
        // Straight there: the stop of this train that reaches their home,
        // the one nearest it if several do.
        std::optional<StationId> direct;
        std::int64_t nearest = 0;
        for (StationId h : t.route) {
            if (h == station_id || std::find(ways.stations.begin(), ways.stations.end(), h) == ways.stations.end()) continue;
            const std::int64_t d = distance_mm(rw.track().node(rw.station(h).node).pos, home_at);
            if (!direct || d < nearest) {
                direct = h;
                nearest = d;
            }
        }
        // Travellers for different homes getting off at the same stop (or
        // changing at the same hub for the same station) share cars.
        const auto board = [&](StationId to, std::optional<StationId> via, std::int64_t gain) {
            for (Candidate& k : candidates) {
                if (k.homes.empty() || k.cargo != e.cargo || k.destination != to || k.via != via) continue;
                k.homes.push_back(e.home);
                return;
            }
            candidates.push_back({e.cargo, to, gain, via, {e.home}});
        };
        if (direct) {
            board(*direct, std::nullopt, express_fare(cargo.get(e.cargo), rw, station_id, *direct).whole_dollars());
            continue;
        }
        // No direct train: ride to the stop of this train from which a
        // station by their home is fewest trains away (and nearer in trains
        // than from here), the one nearest the home among equals.
        const std::vector<std::int32_t>& legs = ways.legs;
        std::optional<StationId> hub;
        std::int64_t best = 0;
        std::int32_t best_legs = 0;
        const std::int32_t from_here = legs[station_id] < 0 ? std::numeric_limits<std::int32_t>::max() : legs[station_id];
        for (StationId h : t.route) {
            if (h == station_id || legs[h] < 1 || legs[h] >= from_here) continue;
            const std::int64_t d = distance_mm(rw.track().node(rw.station(h).node).pos, home_at);
            if (!hub || legs[h] < best_legs || (legs[h] == best_legs && d < best)) {
                hub = h;
                best = d;
                best_legs = legs[h];
            }
        }
        if (hub) {
            // Bound for the station by their home nearest it, by way of the hub.
            StationId to = ways.stations.front();
            std::int64_t closest = -1;
            for (StationId w : ways.stations) {
                const std::int64_t d = distance_mm(rw.track().node(rw.station(w).node).pos, home_at);
                if (closest < 0 || d < closest) {
                    to = w;
                    closest = d;
                }
            }
            board(to, hub, express_fare(cargo.get(e.cargo), rw, station_id, *hub).whole_dollars());
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.gain > b.gain; });

    Train& train = rw.train_mut(train_id);
    const auto waiting_milli = [&](const Candidate& c) {
        if (c.homes.empty()) return st.waiting[c.cargo].milli;
        std::int32_t sum = 0;
        for (SiteId h : c.homes) sum += express_pool(st, c.cargo, h).milli;
        return sum;
    };
    // A custom consist takes each listed car once (a cargo listed twice, twice).
    std::vector<std::int32_t> wanted(cargo.all().size(), 0);
    for (CargoId c : rule.cars) ++wanted[c];
    const auto enough = [&](const Candidate& c) {
        return waiting_milli(c) >= (c.destination ? express_min : kMilli) && (!rule.custom || wanted[c.cargo] > 0);
    };
    // Loaded cars first; the train has as many slots as the rule allows,
    // after any caboose and dining car (more only if through loads need them).
    std::stable_partition(train.cars.begin(), train.cars.end(), [](const Car& c) { return c.cargo.has_value(); });
    for (const Car& car : train.cars)
        if (car.cargo && rule.custom && wanted[*car.cargo] > 0) --wanted[*car.cargo]; // already aboard
    const std::size_t loaded = train.loaded_cars();
    train.cars.resize(std::max(train.slots_at(stop), loaded));
    std::size_t next = 0;
    for (Car& car : train.cars) {
        if (car.cargo) continue;
        while (next < candidates.size() && !enough(candidates[next])) ++next;
        if (next == candidates.size()) break;
        const Candidate& pick = candidates[next];
        --wanted[pick.cargo];
        if (pick.destination) {
            // Up to a carload from the homes going this way, the most first;
            // the car is reckoned to be going to the home most of them are.
            std::vector<SiteId> homes = pick.homes;
            std::stable_sort(homes.begin(), homes.end(), [&](SiteId a, SiteId b) {
                return express_pool(st, pick.cargo, a).milli > express_pool(st, pick.cargo, b).milli;
            });
            std::int32_t load = 0;
            for (SiteId h : homes) {
                std::int32_t& pool = express_pool(st, pick.cargo, h).milli;
                const std::int32_t take = std::min(kMilli - load, pool);
                pool -= take;
                load += take;
                if (load == kMilli) break;
            }
            if (cargo.get(pick.cargo).key == "passengers") st.passengers_boarded_milli += load;
            car = Car{pick.cargo, load, 0, today, station_id, pick.destination, static_cast<std::int32_t>(pick.gain), tick,
                      pick.via, homes.front()};
        } else {
            WaitingCargo& pool = st.waiting[pick.cargo];
            const std::int32_t price = pool.average_price();
            pool.milli -= kMilli;
            pool.value -= std::int64_t{price} * kMilli;
            car = Car{pick.cargo, kMilli, price, today, station_id, std::nullopt, static_cast<std::int32_t>(pick.gain), tick};
        }
    }

    // Short of the rule's minimum: wait here for more ("wait for a full load").
    train.holding = train.loaded_cars() < rule.min;

    if (income.total > Money{}) {
        train.revenue += income.total;
        train.last_income = income.total;
        train.last_income_tick = tick;
    }
    return income;
}

std::int64_t best_price_within_reach(const Economy& eco, const Balance& b, MapPoint at, CargoId c, StationSize size) {
    std::int64_t best = 0;
    eco.for_nodes_within(at, eco.cells_mm(catchment_radius(size, b)),
                         [&](std::int32_t x, std::int32_t y) { best = std::max<std::int64_t>(best, eco.price(c, x, y)); });
    return best;
}

} // namespace railmaster::sim


