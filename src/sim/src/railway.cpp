#include "railmaster/sim/railway.hpp"

#include "railmaster/sim/fixed_math.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace railmaster::sim {

namespace {

constexpr std::int64_t kMmPerMile = 1609344;

std::int64_t cargo_value(const Train& t) {
    std::int64_t v = 0;
    for (const Car& c : t.cars)
        if (c.cargo) v += c.expected_value;
    return v;
}

// a outranks b at a single-track meet: higher priority; at equal priority the
// more valuable cargo [D]; then, our tie-break, the older train.
bool outranks(const Train& a, const Train& b) {
    if (a.priority != b.priority) return a.priority > b.priority;
    const std::int64_t va = cargo_value(a), vb = cargo_value(b);
    if (va != vb) return va > vb;
    return a.id < b.id;
}

std::int32_t bridge_speed_permille(const TrackEdge& e, const Balance& b) {
    if (e.kind != TrackKind::Bridge) return 1000;
    return e.bridge == BridgeType::Wood ? b.trains.wood_bridge_speed_permille : b.trains.other_bridge_speed_permille;
}

// A train is physically on the track (rather than at a station node) in these states.
bool on_path(TrainState s) {
    return s == TrainState::Moving || s == TrainState::Servicing || s == TrainState::BrokenDown;
}

std::int32_t gauge_after(std::int64_t used, std::int64_t range) {
    return static_cast<std::int32_t>(std::max<std::int64_t>(0, kGaugeFull - used * kGaugeFull / range));
}

bool needs(const Train& t, ServiceType type, const LocomotiveType& loco, const Balance& b) {
    const std::int32_t low = b.servicing.service_threshold_permille;
    if (type == ServiceType::MaintenanceFacility) return t.oil < low;
    const bool water_low = loco.fuel == Fuel::Steam && t.water < low;
    return water_low || t.sand < low;
}

void refill(Train& t, ServiceType type) {
    if (type == ServiceType::MaintenanceFacility) {
        t.oil = kGaugeFull;
        t.oil_used_mm = 0;
    } else {
        t.water = kGaugeFull;
        t.water_used_mm = 0;
        t.sand = kGaugeFull;
        t.sand_used_climb_mm = 0;
    }
}

} // namespace

Money station_cost(StationSize size, const Balance& b) {
    switch (size) {
    case StationSize::Small: return Money::dollars(b.stations.small_cost);
    case StationSize::Medium: return Money::dollars(b.stations.medium_cost);
    case StationSize::Large: return Money::dollars(b.stations.large_cost);
    }
    throw std::invalid_argument("unknown station size");
}

Money service_building_cost(ServiceType type, const Balance& b) {
    switch (type) {
    case ServiceType::ServiceTower: return Money::dollars(b.servicing.service_tower_cost);
    case ServiceType::MaintenanceFacility: return Money::dollars(b.servicing.maintenance_facility_cost);
    }
    throw std::invalid_argument("unknown service building type");
}

std::int64_t mph_to_mm_per_tick(std::int64_t mph, const Balance& b) {
    return mph * kMmPerMile * b.trains.seconds_per_tick / 3600;
}

std::int64_t car_weight_kg(bool express, std::int32_t year, const Balance& b) {
    const auto& w = b.trains.car_weights;
    std::int64_t tons = w.front().freight_tons;
    for (const Balance::CarWeight& era : w)
        if (year >= era.from_year) tons = era.freight_tons;
    const std::int64_t kg = tons * 1000;
    return express ? kg * b.trains.express_permille_of_freight / 1000 : kg;
}

std::int64_t reference_weight_kg(std::int32_t year, const Balance& b) {
    return std::int64_t{b.trains.reference_cars} * car_weight_kg(false, year, b);
}

std::int64_t train_weight_kg(const Train& t, std::int32_t year, const Balance& b) {
    const std::int64_t freight = car_weight_kg(false, year, b), light = car_weight_kg(true, year, b);
    std::int64_t w = 0;
    for (const Car& c : t.cars)
        if (c.cargo) w += c.destination ? light : freight;
    if (t.caboose) w += light;
    if (t.diner) w += light;
    return w;
}

std::int64_t speed_for_weight_mm_per_tick(const LocomotiveType& loco, std::int64_t weight_kg, std::int64_t reference_kg,
                                          std::int32_t grade_bp, const Balance& b, bool sand) {
    const Balance::Trains& tr = b.trains;
    const std::int64_t ref = std::max<std::int64_t>(1, reference_kg);
    const std::int64_t over = std::max<std::int64_t>(0, weight_kg - std::int64_t{loco.free_weight_tons} * 1000);
    const std::int64_t load = std::clamp<std::int64_t>(1000 - tr.load_slowdown_permille * over / ref,
                                                       tr.min_load_speed_permille, 1000);
    std::int64_t grade = 1000;
    if (grade_bp > 0) {
        // grade% x drop per 1% x weight / reference, in thousandths.
        const std::int64_t mult = sand ? 100 : b.servicing.no_sand_grade_drop_percent;
        const std::int64_t drop = std::int64_t{grade_bp} * loco.grade_drop_permille * mult / 100 * weight_kg / (100 * ref);
        grade = std::clamp<std::int64_t>(1000 - drop, tr.min_grade_speed_permille, 1000);
    }
    return mph_to_mm_per_tick(loco.top_speed_mph, b) * load / 1000 * grade / 1000;
}

std::int64_t chart_speed_mm_per_tick(const LocomotiveType& loco, std::size_t cars, bool express, std::int32_t year,
                                     std::int32_t grade_bp, const Balance& b) {
    const std::int64_t w = static_cast<std::int64_t>(cars) * car_weight_kg(express, year, b);
    return speed_for_weight_mm_per_tick(loco, w, reference_weight_kg(year, b), grade_bp, b);
}

std::int32_t curve_speed_permille(MapPoint a, MapPoint joint, MapPoint c, const Balance& b) {
    const std::int64_t x1 = joint.x_mm - a.x_mm, y1 = joint.y_mm - a.y_mm;
    const std::int64_t x2 = c.x_mm - joint.x_mm, y2 = c.y_mm - joint.y_mm;
    const std::int64_t l1 = isqrt(x1 * x1 + y1 * y1), l2 = isqrt(x2 * x2 + y2 * y2);
    const std::int32_t floor = b.trains.min_curve_speed_permille;
    if (l1 == 0 || l2 == 0) return 1000;
    // Work in metres so the products stay well inside 64 bits.
    const std::int64_t mx1 = x1 / 1000, my1 = y1 / 1000, mx2 = x2 / 1000, my2 = y2 / 1000;
    const std::int64_t m1 = std::max<std::int64_t>(1, l1 / 1000), m2 = std::max<std::int64_t>(1, l2 / 1000);
    if (mx1 * mx2 + my1 * my2 <= 0) return floor; // a turn of 90 degrees or more
    const std::int64_t cross = std::abs(mx1 * my2 - my1 * mx2);
    // sin(turn) in millionths; the turn's radius is about piece / turn.
    const std::int64_t sin_ppm = cross * 1'000'000 / (m1 * m2);
    if (sin_ppm == 0) return 1000;
    const std::int64_t radius_mm = std::min(l1, l2) / 1000 * 1'000'000 / sin_ppm * 1000;
    const std::int64_t f = radius_mm * 1000 / std::max<std::int64_t>(1, b.trains.curve_comfort_radius_mm);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(f, floor, 1000));
}

const char* cargo_filter_name(CargoFilter f) {
    switch (f) {
    case CargoFilter::Any: return "Any Cargo";
    case CargoFilter::Freight: return "Any Freight";
    case CargoFilter::Express: return "Any Express";
    }
    return "?";
}

std::size_t Train::loaded_cars() const {
    return static_cast<std::size_t>(std::count_if(cars.begin(), cars.end(), [](const Car& c) { return c.cargo.has_value(); }));
}

std::size_t Train::slots_at(std::size_t i) const {
    const std::size_t room = kMaxCarsPerTrain - std::size_t{caboose} - std::size_t{diner};
    return i < rules.size() ? std::min(rules[i].capacity(), room) : room;
}

std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, const Train& train, std::int32_t year,
                                      std::int32_t grade_bp, const Balance& b) {
    std::int64_t v = speed_for_weight_mm_per_tick(loco, train_weight_kg(train, year, b), reference_weight_kg(year, b),
                                                  grade_bp, b, train.sand > 0);
    if (loco.fuel == Fuel::Steam && train.water == 0) v = v * b.servicing.no_water_speed_permille / 1000;
    return v;
}

Money annual_maintenance(const LocomotiveType& loco, std::int32_t age_years, std::int32_t oil, const Balance& b) {
    const std::int64_t age = std::max(0, age_years);
    Money m = loco.maintenance_per_year.scaled(100 + b.breakdowns.maintenance_age_percent_per_year * age, 100);
    if (oil <= 0) m = m.scaled(b.breakdowns.maintenance_no_oil_percent, 100);
    return m;
}

NodeId Railway::split_edge(EdgeId e, MapPoint at) {
    EdgeId second = 0;
    const NodeId mid = track_.split_edge(e, at, &second);
    const std::int64_t len_first = track_.edge(e).length_mm;       // a -> mid
    const std::int64_t len_second = track_.edge(second).length_mm; // mid -> b

    for (Train& t : trains_) {
        if (t.path.empty()) continue;
        std::vector<PathStep> path;
        path.reserve(t.path.size() + 1);
        std::size_t new_step = t.step;
        std::int64_t new_offset = t.offset_mm;
        for (std::size_t i = 0; i < t.path.size(); ++i) {
            const PathStep s = t.path[i];
            const bool current = i == t.step;
            if (current) new_step = path.size();
            if (s.edge != e) {
                path.push_back(s);
                continue;
            }
            // Travelled forward the halves come a->mid then mid->b; backward, the reverse.
            const PathStep first_half = s.forward ? PathStep{e, true} : PathStep{second, false};
            const PathStep second_half = s.forward ? PathStep{second, true} : PathStep{e, false};
            const std::int64_t first_len = s.forward ? len_first : len_second;
            path.push_back(first_half);
            path.push_back(second_half);
            if (current && t.offset_mm >= first_len) {
                new_step = path.size() - 1;
                const std::int64_t second_len = s.forward ? len_second : len_first;
                new_offset = std::min(t.offset_mm - first_len, second_len - 1);
            } else if (current) {
                new_offset = t.offset_mm;
            }
        }
        t.path = std::move(path);
        t.step = new_step;
        t.offset_mm = new_offset;
    }
    // Crossings on the far half now belong to the new piece.
    const MapPoint a = track_.node(track_.edge(e).a).pos;
    for (Crossing& c : crossings_) {
        for (EdgeId* side : {&c.a, &c.b})
            if (*side == e && distance_mm(a, c.at) > len_first) *side = second;
    }
    return mid;
}

const char* station_building_name(StationBuildingType t) {
    switch (t) {
    case StationBuildingType::PostOffice: return "Post Office";
    case StationBuildingType::Hotel: return "Hotel";
    case StationBuildingType::Restaurant: return "Restaurant";
    case StationBuildingType::Tavern: return "Tavern";
    }
    return "?";
}

Money station_building_cost(StationBuildingType t, const Balance& b) {
    switch (t) {
    case StationBuildingType::PostOffice: return Money::dollars(b.stations.post_office_cost);
    case StationBuildingType::Hotel: return Money::dollars(b.stations.hotel_cost);
    case StationBuildingType::Restaurant: return Money::dollars(b.stations.restaurant_cost);
    case StationBuildingType::Tavern: return Money::dollars(b.stations.tavern_cost);
    }
    return Money{};
}

std::uint32_t Railway::add_station_building(StationBuildingType type, MapPoint pos, CompanyId owner) {
    const auto id = static_cast<std::uint32_t>(station_buildings_.size());
    station_buildings_.push_back({id, type, owner, pos});
    return id;
}

bool Railway::near_building(StationId s, StationBuildingType type) const {
    const MapPoint at = track_.node(stations_.at(s).node).pos;
    for (const StationBuilding& b : station_buildings_) {
        if (b.type == type && distance_mm(b.pos, at) <= balance_.stations.building_range_mm) return true;
    }
    return false;
}

void Railway::transfer_owner(CompanyId from, CompanyId to) {
    track_.transfer_owner(from, to);
    for (Station& s : stations_)
        if (s.owner == from) s.owner = to;
    for (ServiceBuilding& b : service_buildings_)
        if (b.owner == from) b.owner = to;
    for (StationBuilding& b : station_buildings_)
        if (b.owner == from) b.owner = to;
    for (Train& t : trains_) {
        if (t.owner == from) t.owner = to;
        // Distance run so far on the old company's track now counts as the new one's.
        if (t.leg_mm_by_owner.size() > from) {
            const std::int64_t mm = t.leg_mm_by_owner[from];
            t.leg_mm_by_owner[from] = 0;
            if (t.leg_mm_by_owner.size() <= to) t.leg_mm_by_owner.resize(std::size_t{to} + 1, 0);
            t.leg_mm_by_owner[to] += mm;
        }
    }
}

StationId Railway::add_station(std::string name, NodeId node, StationSize size, CompanyId owner) {
    if (node >= track_.nodes().size()) throw std::out_of_range("station node out of range");
    const auto id = static_cast<StationId>(stations_.size());
    stations_.push_back({id, std::move(name), node, size, owner});
    return id;
}

ServiceBuildingId Railway::add_service_building(ServiceType type, NodeId node, CompanyId owner) {
    if (node >= track_.nodes().size()) throw std::out_of_range("service building node out of range");
    const auto id = static_cast<ServiceBuildingId>(service_buildings_.size());
    service_buildings_.push_back({id, type, node, owner});
    return id;
}

std::optional<std::string> Railway::electric_route_problem(const std::vector<StationId>& route) const {
    for (std::size_t i = 0; i < route.size(); ++i) {
        const Station& from = stations_.at(route[i]);
        const Station& to = stations_.at(route[(i + 1) % route.size()]);
        if (!track_.shortest_path(from.node, to.node, true)) {
            return "electric engines need electrified track all the way from " + from.name + " to " + to.name;
        }
    }
    return std::nullopt;
}

TrainId Railway::add_train(LocoTypeId loco, std::size_t car_count, std::vector<StationId> route,
                           std::int32_t priority, CompanyId owner) {
    if (car_count > kMaxCarsPerTrain) throw std::invalid_argument("too many cars for one train");
    if (route.empty()) throw std::invalid_argument("train route must have at least one stop");
    for (StationId s : route) {
        if (s >= stations_.size()) throw std::out_of_range("route station out of range");
    }
    Train t;
    t.id = static_cast<TrainId>(trains_.size());
    t.owner = owner;
    t.loco = loco;
    t.cars.resize(car_count);
    t.priority = priority;
    t.route = std::move(route);
    t.rules.assign(t.route.size(), ConsistRule{.max = static_cast<std::uint8_t>(car_count)});
    t.service_stop.assign(t.route.size(), 0);
    t.state = TrainState::Dwelling;
    t.at_node = stations_[t.route.front()].node;
    t.wait_ticks_left = 0; // depart on the first tick
    trains_.push_back(std::move(t));
    return trains_.back().id;
}

void Railway::tick(const LocomotiveRegistry& locos) {
    for (Train& t : trains_) tick_train(t, locos);
}

void Railway::plan_to_current_stop(Train& t, bool electric) {
    const NodeId target = stations_[t.route[t.stop_index]].node;
    auto path = track_.shortest_path(t.at_node, target, electric);
    if (!path) {
        t.state = TrainState::NoRoute;
        return;
    }
    if (path->empty()) { // already there
        t.state = TrainState::Dwelling;
        t.wait_ticks_left = balance_.trains.station_dwell_ticks;
        ++t.stops_made;
        arrivals_.emplace_back(t.id, t.route[t.stop_index]);
        return;
    }
    t.path = std::move(*path);
    count_traffic(t.path.front().edge);
    t.step = 0;
    t.offset_mm = 0;
    t.speed_mm_per_tick = 0;
    t.state = TrainState::Moving;
}

bool Railway::must_yield(const Train& t) const {
    const PathStep mine = t.path[t.step];
    if (track_.edge(mine.edge).double_track) return false;
    const std::int64_t len = track_.edge(mine.edge).length_mm;
    for (const Train& o : trains_) {
        if (o.id == t.id || !on_path(o.state)) continue;
        const PathStep theirs = o.path[o.step];
        if (theirs.edge != mine.edge || theirs.forward == mine.forward) continue;
        // A train on another company's track yields to the owner's trains,
        // whatever its priority [M]; otherwise priority and cargo decide.
        const CompanyId owner = track_.edge(mine.edge).owner;
        const bool o_home = o.owner == owner, t_home = t.owner == owner;
        if (o_home != t_home) {
            if (!o_home) continue;
        } else if (!outranks(o, t)) {
            continue;
        }
        // Both measured from my starting end: they have not yet passed me
        // while their position is still ahead of mine.
        if (len - o.offset_mm >= t.offset_mm) return true;
    }
    return false;
}

std::optional<Railway::Leader> Railway::leader_ahead(const Train& t) const {
    const std::int64_t reach = balance_.trains.follow_distance_mm;
    std::optional<Leader> best;
    std::int64_t before = -t.offset_mm; // distance from t to the start of step i
    for (std::size_t i = t.step; i < t.path.size() && before <= reach; ++i) {
        const PathStep s = t.path[i];
        const TrackEdge& e = track_.edge(s.edge);
        if (!e.double_track) { // on double track trains overtake [D]
            for (const Train& o : trains_) {
                // A ghosting train blocks nobody (rt3-clone-spec §10.3 [D]).
                if (o.id == t.id || !on_path(o.state) || o.yielding) continue;
                const PathStep theirs = o.path[o.step];
                if (theirs != s) continue;
                if (i == t.step && o.offset_mm <= t.offset_mm) continue; // behind, or level
                const std::int64_t d = before + o.offset_mm;
                if (d <= reach && (!best || d < best->distance_mm)) {
                    best = Leader{d, o.state == TrainState::Moving ? o.speed_mm_per_tick : 0};
                }
            }
            if (best) return best;
        }
        before += e.length_mm;
    }
    return best;
}

void Railway::use_supplies(Train& t, const LocomotiveType& loco, std::int64_t distance_mm, std::int32_t grade_bp) {
    if (loco.fuel == Fuel::Steam) {
        t.water_used_mm += distance_mm;
        t.water = gauge_after(t.water_used_mm, balance_.servicing.water_range_mm);
    }
    if (grade_bp > 0) {
        t.sand_used_climb_mm += distance_mm * grade_bp / 10000;
        t.sand = gauge_after(t.sand_used_climb_mm, balance_.servicing.sand_range_climb_mm);
    }
    t.oil_used_mm += distance_mm;
    t.oil = gauge_after(t.oil_used_mm, balance_.servicing.oil_range_mm);
    // Fuel: distance x everything moved x economy, more uphill [D/I].
    const std::int64_t kg = train_weight_kg(t, year_, balance_) + std::int64_t{loco.weight_tons} * 1000;
    const std::int64_t uphill = 10000 + std::max(0, grade_bp) * balance_.finance.fuel_uphill_percent_per_1pct;
    // kg x mm / 1e6 = kg-km; kept per tick, so round to the nearest.
    t.fuel_kg_km += (distance_mm * kg / 1000 * loco.fuel_economy_percent / 100 * uphill / 10000 + 500) / 1000;
}

bool Railway::roll_breakdown(const Train& t, const LocomotiveType& loco, std::int64_t distance_mm) {
    if (!rules_.breakdowns || distance_mm <= 0) return false;
    constexpr std::int64_t kBillion = 1'000'000'000;
    // Chance in parts per billion: distance over the mean distance between
    // failures, raised as oil runs low and scaled by reliability.
    const std::int64_t oil_factor_permille =
        1000 + (balance_.breakdowns.empty_oil_multiplier - 1) * (kGaugeFull - t.oil);
    std::int64_t ppb = distance_mm * kBillion / balance_.breakdowns.mean_distance_mm;
    ppb = ppb * oil_factor_permille / 1000;
    ppb = ppb * 100 / loco.reliability;
    // Older engines fail more: x (1 + age / breakdown_age_years) [I].
    const std::int64_t age_years = std::max(0, today_ - t.built_day) / 365;
    const std::int64_t span = std::max(1, balance_.breakdowns.breakdown_age_years);
    ppb = ppb * (span + age_years) / span;
    if (t.caboose) ppb = ppb * kCabooseBreakdownPercent / 100;
    return static_cast<std::int64_t>(rng_.below(static_cast<std::uint32_t>(kBillion))) < ppb;
}

bool Railway::service_at(Train& t, NodeId node, const LocomotiveType& loco, bool always) {
    bool stopped = false;
    for (const ServiceBuilding& b : service_buildings_) {
        if (b.node != node || (!always && !needs(t, b.type, loco, balance_))) continue;
        refill(t, b.type);
        stopped = true;
    }
    if (stopped) ++t.service_stops;
    return stopped;
}

void Railway::remove_edges(const std::vector<EdgeId>& edges) {
    for (EdgeId e : edges) track_.remove_edge(e);
    std::erase_if(crossings_, [&](const Crossing& c) { return track_.edge(c.a).removed || track_.edge(c.b).removed; });
    for (Train& t : trains_) {
        if (!on_path(t.state)) continue;
        for (std::size_t i = t.step + 1; i < t.path.size(); ++i) {
            if (!track_.edge(t.path[i].edge).removed) continue;
            t.path.resize(t.step + 1);
            t.replan = true;
            break;
        }
    }
}

std::optional<TrainId> Railway::train_on(EdgeId e) const {
    for (const Train& t : trains_)
        if (on_path(t.state) && t.path[t.step].edge == e) return t.id;
    return std::nullopt;
}

std::vector<EdgeId> Railway::run_of(EdgeId start) const {
    const TrackEdge& first = track_.edge(start);
    if (first.removed) return {};
    const auto stop_at = [&](NodeId n) {
        if (track_.edges_at(n).size() != 2) return true; // a junction or the end of the line
        for (const Station& st : stations_)
            if (st.node == n) return true;
        for (const ServiceBuilding& b : service_buildings_)
            if (b.node == n) return true;
        return false;
    };
    std::vector<EdgeId> run{start};
    for (NodeId n : {first.a, first.b}) {
        EdgeId here = start;
        while (!stop_at(n)) {
            const auto& adj = track_.edges_at(n);
            const EdgeId next = adj[0] == here ? adj[1] : adj[0];
            if (track_.edge(next).owner != first.owner || next == start) break;
            run.push_back(next);
            here = next;
            n = track_.edge(next).other(n);
        }
    }
    std::sort(run.begin(), run.end());
    return run;
}

void Railway::arrive_at_stop(Train& t) {
    t.at_node = track_.step_end(t.path[t.step]);
    t.path.clear();
    t.step = 0;
    t.offset_mm = 0;
    t.speed_mm_per_tick = 0;
    t.state = TrainState::Dwelling;
    t.wait_ticks_left = balance_.trains.station_dwell_ticks;
    ++t.stops_made;
    arrivals_.emplace_back(t.id, t.route[t.stop_index]);
}

std::optional<std::int64_t> Railway::crossing_stop(const Train& t) const {
    if (crossings_.empty()) return std::nullopt;
    const std::int64_t clear = balance_.trains.crossing_clear_mm;
    const std::int64_t reach = t.speed_mm_per_tick + clear + 1;
    std::int64_t before = -t.offset_mm; // from t to the start of step i
    for (std::size_t i = t.step; i < t.path.size() && before <= reach; ++i) {
        const PathStep s = t.path[i];
        const TrackEdge& e = track_.edge(s.edge);
        for (const Crossing& c : crossings_) {
            if (c.overpass || (c.a != s.edge && c.b != s.edge)) continue;
            const std::int64_t along = distance_mm(track_.node(e.a).pos, c.at);
            const std::int64_t d = before + (s.forward ? along : e.length_mm - along); // to the crossing
            if (d <= clear || d - clear > reach) continue; // inside it already, or far off
            const EdgeId other = c.a == s.edge ? c.b : c.a;
            const std::int64_t other_along = distance_mm(track_.node(track_.edge(other).a).pos, c.at);
            for (const Train& o : trains_) {
                // A train ghosting through a meet blocks nobody [D].
                if (o.id == t.id || !on_path(o.state) || o.yielding) continue;
                // How far o has left to the crossing, negative once past. A
                // crossing at a node is recorded on one of the two pieces
                // that meet there, so look a piece either side of o.
                std::optional<std::int64_t> left;
                const std::int64_t here_len = track_.edge(o.path[o.step].edge).length_mm;
                for (std::size_t j = o.step == 0 ? 0 : o.step - 1; j <= o.step + 1 && j < o.path.size(); ++j) {
                    const PathStep os = o.path[j];
                    if (os.edge != other) continue;
                    const std::int64_t len = track_.edge(other).length_mm;
                    const std::int64_t into = os.forward ? other_along : len - other_along; // from the step's start
                    if (j == o.step) left = into - o.offset_mm;
                    else if (j > o.step) left = here_len - o.offset_mm + into;
                    else left = -(o.offset_mm + len - into);
                    break;
                }
                if (!left) continue;
                const std::int64_t ahead = *left;
                // A tick is 40 s of travel: a train that passed within its
                // last tick's run is still using the crossing.
                const bool inside = ahead <= clear && ahead >= -(clear + o.speed_mm_per_tick);
                // Coming up to it: the higher-ranked train goes first.
                const bool arriving = ahead > clear && ahead - clear <= o.speed_mm_per_tick + 1 && outranks(o, t);
                if (inside || arriving) return d - clear;
            }
        }
        before += e.length_mm;
    }
    return std::nullopt;
}

std::vector<TrainId> Railway::take_breakdowns() { return std::exchange(breakdowns_, {}); }

std::vector<TrainId> Railway::take_crashes() {
    std::vector<TrainId> out;
    out.swap(crashes_);
    return out;
}

bool Railway::roll_crash(const Train& t, const LocomotiveType& loco) {
    if (!rules_.breakdowns) return false;
    const std::int64_t age_years = std::max(0, today_ - t.built_day) / 365;
    std::int64_t ppb = balance_.breakdowns.crash_ppb_per_tick * 100 / loco.reliability;
    const std::int64_t span = std::max(1, balance_.breakdowns.crash_age_years);
    ppb = ppb * (span + age_years) / span;
    if (t.oil == 0) ppb *= 3;
    return static_cast<std::int64_t>(rng_.below(1'000'000'000u)) < ppb;
}

void Railway::set_route(TrainId id, std::vector<StationId> route, std::vector<std::uint8_t> service_stop) {
    if (route.empty()) throw std::invalid_argument("train route must have at least one stop");
    Train& t = trains_.at(id);
    service_stop.resize(route.size(), 0);
    std::vector<ConsistRule> rules;
    for (StationId s : route) {
        const auto old = std::find(t.route.begin(), t.route.end(), s);
        rules.push_back(old != t.route.end() ? t.rules[static_cast<std::size_t>(old - t.route.begin())] : t.rules.front());
    }
    const StationId current = t.route[t.stop_index];
    const auto kept = std::find(route.begin(), route.end(), current);
    t.route = std::move(route);
    t.rules = std::move(rules);
    t.service_stop = std::move(service_stop);
    if (kept != t.route.end()) {
        t.stop_index = static_cast<std::size_t>(kept - t.route.begin());
        return;
    }
    if (t.state == TrainState::Dwelling) {
        // Leaves for the first stop when it is done here.
        t.stop_index = t.route.size() - 1;
        t.holding = false;
        return;
    }
    t.stop_index = 0;
    if (on_path(t.state) && !t.path.empty()) {
        t.path.resize(t.step + 1);
        t.replan = true;
    }
}

bool Railway::break_down(TrainId id, std::int32_t days) {
    Train& t = trains_.at(id);
    if (t.state != TrainState::Moving || t.path.empty()) return false;
    t.state = TrainState::BrokenDown;
    t.wait_ticks_left = days * kTrainTicksPerDay;
    t.speed_mm_per_tick = 0;
    ++t.breakdowns;
    breakdowns_.push_back(t.id);
    return true;
}

namespace {
Money last_year(const std::vector<Money>& months) {
    Money sum;
    for (const Money& m : months) sum += m;
    return sum;
}
void file_month(Money& now, std::vector<Money>& months) {
    months.push_back(now);
    if (months.size() > 12) months.erase(months.begin());
    now = Money{};
}
} // namespace

Money Train::profit_last_year() const { return last_year(profit_months); }

Rating rate(Money earned, std::size_t months, Money price, const Balance& b) {
    if (months == 0) return Rating::Low;
    if (earned < Money{}) return Rating::Loss;
    // A good year's return, for the months on record.
    const Money good = price.scaled(std::int64_t{b.overlays.good_return_percent} * static_cast<std::int64_t>(months), 100 * 12);
    return earned >= good ? Rating::Good : Rating::Low;
}

bool Railway::needs_double_track(EdgeId e) const {
    return !track_.edge(e).double_track &&
           held_last_month(e) >= static_cast<std::uint32_t>(balance_.overlays.held_days_for_double * kTrainTicksPerDay);
}
Money Station::income_last_year() const { return last_year(income_months); }

void Railway::count_traffic(EdgeId e) {
    if (traffic_.size() <= e) traffic_.resize(std::size_t{e} + 1, 0);
    ++traffic_[e];
}

void Railway::close_month() {
    for (Train& t : trains_) {
        if (t.in_service()) file_month(t.profit_this_month, t.profit_months);
    }
    for (Station& s : stations_) file_month(s.income_this_month, s.income_months);
    traffic_last_ = std::move(traffic_);
    traffic_.assign(track_.edges().size(), 0);
    held_last_ = std::move(held_);
    held_.assign(track_.edges().size(), 0);
}

std::vector<std::pair<TrainId, StationId>> Railway::take_arrivals() {
    std::vector<std::pair<TrainId, StationId>> out;
    out.swap(arrivals_);
    return out;
}

void Railway::tick_train(Train& t, const LocomotiveRegistry& locos) {
    t.yielding = false;
    if (on_path(t.state)) ++t.road_ticks;
    const LocomotiveType& loco = locos.get(t.loco);
    switch (t.state) {
    case TrainState::Dwelling:
        if (t.wait_ticks_left > 0) {
            --t.wait_ticks_left;
            return;
        }
        if (t.holding) {
            // Short of its minimum load: wait another dwell and try again.
            t.wait_ticks_left = balance_.trains.station_dwell_ticks;
            arrivals_.emplace_back(t.id, t.route[t.stop_index]);
            return;
        }
        t.stop_index = (t.stop_index + 1) % t.route.size();
        plan_to_current_stop(t, loco.fuel == Fuel::Electric);
        return;
    case TrainState::NoRoute:
        plan_to_current_stop(t, loco.fuel == Fuel::Electric);
        return;
    case TrainState::Servicing:
    case TrainState::BrokenDown:
        if (--t.wait_ticks_left > 0) return;
        t.state = TrainState::Moving;
        t.speed_mm_per_tick = 0;
        return;
    case TrainState::Crashed:
    case TrainState::Retired:
        return;
    case TrainState::Moving:
        break;
    }

    if (must_yield(t)) {
        const EdgeId e = t.path[t.step].edge;
        if (held_.size() <= e) held_.resize(std::size_t{e} + 1, 0);
        ++held_[e];
        t.yielding = true;
        t.speed_mm_per_tick = 0;
        return;
    }

    const PathStep here = t.path[t.step];
    std::int64_t target = target_speed_mm_per_tick(loco, t, year_, track_.grade_bp(here), balance_) *
                          bridge_speed_permille(track_.edge(here.edge), balance_) / 1000;
    if (t.step > 0) {
        const PathStep before = t.path[t.step - 1];
        target = target *
                 curve_speed_permille(track_.node(track_.step_start(before)).pos, track_.node(track_.step_start(here)).pos,
                                      track_.node(track_.step_end(here)).pos, balance_) /
                 1000;
    }
    // A first-order lag: each tick closes 1/T of the gap to the target, T
    // being the engine's time constant (rt3-clone-spec §10.2 [I]).
    const std::int64_t lag_ticks =
        std::max<std::int64_t>(1, std::int64_t{loco.accel_milli_days} * kTrainTicksPerDay / 1000);
    // On single track there is no overtaking (rt3-clone-spec §10.3 [I]):
    // close on the train ahead, hold its speed, keep the gap.
    const std::optional<Leader> leader = leader_ahead(t);
    if (leader) target = std::min(target, leader->speed_mm_per_tick);
    const std::int64_t gap = target - t.speed_mm_per_tick;
    t.speed_mm_per_tick += gap >= 0 ? (gap + lag_ticks - 1) / lag_ticks : -((-gap + lag_ticks - 1) / lag_ticks);
    if (leader) {
        const std::int64_t room = std::max<std::int64_t>(0, leader->distance_mm - balance_.trains.follow_gap_mm);
        if (t.speed_mm_per_tick > room) t.speed_mm_per_tick = room;
    }

    // Level crossings: wait short of one another line's train is using.
    if (const auto stop = crossing_stop(t)) t.speed_mm_per_tick = std::min(t.speed_mm_per_tick, *stop);

    std::int64_t remaining = t.speed_mm_per_tick;
    std::int64_t travelled = 0;
    while (remaining > 0) {
        const PathStep step = t.path[t.step];
        const std::int64_t room = track_.edge(step.edge).length_mm - t.offset_mm;
        const std::int64_t moved = std::min(remaining, room);
        use_supplies(t, loco, moved, track_.grade_bp(step));
        t.distance_mm += moved;
        const CompanyId owner = track_.edge(step.edge).owner;
        if (t.leg_mm_by_owner.size() <= owner) t.leg_mm_by_owner.resize(std::size_t{owner} + 1, 0);
        t.leg_mm_by_owner[owner] += moved;
        travelled += moved;
        remaining -= moved;
        if (moved < room) {
            t.offset_mm += moved;
            break;
        }
        const NodeId node = track_.step_end(step);
        if (t.step + 1 == t.path.size() && t.replan) {
            // The track ahead was taken up: find another way from here.
            t.replan = false;
            t.at_node = node;
            plan_to_current_stop(t, loco.fuel == Fuel::Electric);
            return;
        }
        if (t.step + 1 == t.path.size()) {
            arrive_at_stop(t);
            // Serviced during the station stop, no extra time; a service
            // stop fills up whatever the gauges say and takes the time.
            const bool service_stop = t.stop_index < t.service_stop.size() && t.service_stop[t.stop_index] != 0;
            if (service_at(t, node, loco, service_stop) && service_stop) {
                t.wait_ticks_left += balance_.servicing.service_ticks;
            }
            return;
        }
        ++t.step;
        count_traffic(t.path[t.step].edge);
        t.offset_mm = 0;
        if (service_at(t, node, loco)) {
            t.state = TrainState::Servicing;
            t.wait_ticks_left = balance_.servicing.service_ticks;
            t.speed_mm_per_tick = 0;
            return;
        }
    }

    if (roll_breakdown(t, loco, travelled)) {
        t.state = TrainState::BrokenDown;
        const Balance::Breakdowns& b = balance_.breakdowns;
        t.wait_ticks_left = rng_.between(b.breakdown_min_days, b.breakdown_max_days) * kTrainTicksPerDay;
        t.speed_mm_per_tick = 0;
        ++t.breakdowns;
        breakdowns_.push_back(t.id);
    } else if (travelled > 0 && roll_crash(t, loco)) {
        t.state = TrainState::Crashed;
        t.at_node = track_.step_start(t.path[t.step]); // wreck shown at the last node passed
        t.path.clear();
        t.speed_mm_per_tick = 0;
        for (Car& car : t.cars) car = Car{};
        crashes_.push_back(t.id);
    }
}

MapPoint Railway::train_position(TrainId id) const {
    const Train& t = trains_.at(id);
    if (!on_path(t.state)) return track_.node(t.at_node).pos;
    const PathStep s = t.path[t.step];
    const MapPoint a = track_.node(track_.step_start(s)).pos;
    const MapPoint b = track_.node(track_.step_end(s)).pos;
    const std::int64_t len = track_.edge(s.edge).length_mm;
    return {a.x_mm + (b.x_mm - a.x_mm) * t.offset_mm / len, a.y_mm + (b.y_mm - a.y_mm) * t.offset_mm / len};
}

} // namespace railmaster::sim
