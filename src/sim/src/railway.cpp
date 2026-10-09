#include "railmaster/sim/railway.hpp"

#include <algorithm>
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

std::int64_t speed_for(std::int64_t top, std::int64_t grade_rating, std::size_t cars, std::int32_t grade_bp,
                       const Balance& b) {
    if (grade_bp <= 0) return top;
    const std::int64_t load = 2 + static_cast<std::int64_t>(cars);
    const std::int64_t penalty = static_cast<std::int64_t>(grade_bp) * load * b.trains.grade_penalty / grade_rating;
    const std::int64_t permille = std::max<std::int64_t>(b.trains.min_speed_permille, 1000 - penalty);
    return top * permille / 1000;
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

std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, std::size_t cars, std::int32_t grade_bp,
                                      const Balance& b) {
    return speed_for(mph_to_mm_per_tick(loco.top_speed_mph, b), loco.grade_rating, cars, grade_bp, b);
}

std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, const Train& train, std::int32_t grade_bp,
                                      const Balance& b) {
    std::int64_t top = mph_to_mm_per_tick(loco.top_speed_mph, b);
    if (loco.fuel == Fuel::Steam && train.water == 0) top = top * b.servicing.no_water_speed_permille / 1000;
    std::int64_t rating = loco.grade_rating;
    if (train.sand == 0) rating = std::max<std::int64_t>(1, rating * b.servicing.no_sand_grade_permille / 1000);
    return speed_for(top, rating, train.cars.size(), grade_bp, b);
}

Money annual_maintenance(const LocomotiveType& loco, std::int32_t age_years, std::int32_t oil, const Balance& b) {
    const std::int32_t cap = b.breakdowns.maintenance_age_cap_years;
    const std::int64_t age = std::clamp(age_years, 0, cap);
    Money m = loco.maintenance_per_year.scaled(cap + 2 * age, cap);
    if (oil < b.servicing.service_threshold_permille) m = m * b.breakdowns.maintenance_low_oil_multiplier;
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
    t.state = TrainState::Dwelling;
    t.at_node = stations_[t.route.front()].node;
    t.wait_ticks_left = 0; // depart on the first tick
    trains_.push_back(std::move(t));
    return trains_.back().id;
}

void Railway::tick(const LocomotiveRegistry& locos) {
    for (Train& t : trains_) tick_train(t, locos);
}

void Railway::plan_to_current_stop(Train& t) {
    const NodeId target = stations_[t.route[t.stop_index]].node;
    auto path = track_.shortest_path(t.at_node, target);
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
    return static_cast<std::int64_t>(rng_.below(static_cast<std::uint32_t>(kBillion))) < ppb;
}

bool Railway::service_at(Train& t, NodeId node, const LocomotiveType& loco) {
    bool stopped = false;
    for (const ServiceBuilding& b : service_buildings_) {
        if (b.node != node || !needs(t, b.type, loco, balance_)) continue;
        refill(t, b.type);
        stopped = true;
    }
    if (stopped) ++t.service_stops;
    return stopped;
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

std::vector<TrainId> Railway::take_crashes() {
    std::vector<TrainId> out;
    out.swap(crashes_);
    return out;
}

bool Railway::roll_crash(const Train& t, const LocomotiveType& loco) {
    if (!rules_.breakdowns) return false;
    const std::int64_t age_years = std::max(0, today_ - t.built_day) / 365;
    std::int64_t ppb = balance_.breakdowns.crash_ppb_per_tick * 100 / loco.reliability;
    ppb = ppb * (20 + age_years) / 20;
    if (t.oil == 0) ppb *= 3;
    return static_cast<std::int64_t>(rng_.below(1'000'000'000u)) < ppb;
}

std::vector<std::pair<TrainId, StationId>> Railway::take_arrivals() {
    std::vector<std::pair<TrainId, StationId>> out;
    out.swap(arrivals_);
    return out;
}

void Railway::tick_train(Train& t, const LocomotiveRegistry& locos) {
    t.yielding = false;
    const LocomotiveType& loco = locos.get(t.loco);
    switch (t.state) {
    case TrainState::Dwelling:
        if (t.wait_ticks_left > 0) {
            --t.wait_ticks_left;
            return;
        }
        t.stop_index = (t.stop_index + 1) % t.route.size();
        plan_to_current_stop(t);
        return;
    case TrainState::NoRoute:
        plan_to_current_stop(t);
        return;
    case TrainState::Servicing:
    case TrainState::BrokenDown:
        if (--t.wait_ticks_left > 0) return;
        t.state = TrainState::Moving;
        t.speed_mm_per_tick = 0;
        return;
    case TrainState::Crashed:
        return;
    case TrainState::Moving:
        break;
    }

    if (must_yield(t)) {
        t.yielding = true;
        t.speed_mm_per_tick = 0;
        return;
    }

    const std::int64_t accel = std::max<std::int64_t>(
        1, mph_to_mm_per_tick(loco.top_speed_mph, balance_) / balance_.trains.accel_ticks_to_top_speed);
    const PathStep here = t.path[t.step];
    const std::int64_t target = target_speed_mm_per_tick(loco, t, track_.grade_bp(here), balance_) *
                                bridge_speed_permille(track_.edge(here.edge), balance_) / 1000;
    t.speed_mm_per_tick = std::min(target, t.speed_mm_per_tick + accel);

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
        if (t.step + 1 == t.path.size()) {
            arrive_at_stop(t);
            service_at(t, node, loco); // serviced during the station stop, no extra time
            return;
        }
        ++t.step;
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
        t.wait_ticks_left = balance_.breakdowns.breakdown_ticks;
        t.speed_mm_per_tick = 0;
        ++t.breakdowns;
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
