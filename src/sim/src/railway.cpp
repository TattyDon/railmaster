#include "railmaster/sim/railway.hpp"

#include <algorithm>
#include <stdexcept>

namespace railmaster::sim {

namespace {

constexpr std::int64_t kMmPerMile = 1609344;

// a outranks b at a single-track meet: higher priority, then older train.
bool outranks(const Train& a, const Train& b) {
    if (a.priority != b.priority) return a.priority > b.priority;
    return a.id < b.id;
}

} // namespace

Money station_cost(StationSize size) {
    switch (size) {
    case StationSize::Small: return Money::dollars(50'000);
    case StationSize::Medium: return Money::dollars(100'000);
    case StationSize::Large: return Money::dollars(200'000);
    }
    throw std::invalid_argument("unknown station size");
}

std::int64_t mph_to_mm_per_tick(std::int64_t mph) {
    return mph * kMmPerMile * provisional::kTrainSecondsPerTick / 3600;
}

std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, std::size_t cars, std::int32_t grade_bp) {
    const std::int64_t top = mph_to_mm_per_tick(loco.top_speed_mph);
    if (grade_bp <= 0) return top;
    const std::int64_t load = 2 + static_cast<std::int64_t>(cars);
    const std::int64_t penalty = static_cast<std::int64_t>(grade_bp) * load * 25 / loco.grade_rating;
    const std::int64_t permille = std::max<std::int64_t>(provisional::kMinSpeedPermille, 1000 - penalty);
    return top * permille / 1000;
}

StationId Railway::add_station(std::string name, NodeId node, StationSize size) {
    if (node >= track_.nodes().size()) throw std::out_of_range("station node out of range");
    const auto id = static_cast<StationId>(stations_.size());
    stations_.push_back({id, std::move(name), node, size});
    return id;
}

TrainId Railway::add_train(LocoTypeId loco, std::vector<CargoId> cars, std::vector<StationId> route,
                           std::int32_t priority) {
    if (cars.size() > kMaxCarsPerTrain) throw std::invalid_argument("too many cars for one train");
    if (route.empty()) throw std::invalid_argument("train route must have at least one stop");
    for (StationId s : route) {
        if (s >= stations_.size()) throw std::out_of_range("route station out of range");
    }
    Train t;
    t.id = static_cast<TrainId>(trains_.size());
    t.loco = loco;
    t.cars = std::move(cars);
    t.priority = priority;
    t.route = std::move(route);
    t.state = TrainState::Dwelling;
    t.at_node = stations_[t.route.front()].node;
    t.dwell_ticks_left = 0; // depart on the first tick
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
        t.dwell_ticks_left = provisional::kStationDwellTicks;
        ++t.stops_made;
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
        if (o.id == t.id || o.state != TrainState::Moving) continue;
        const PathStep theirs = o.path[o.step];
        if (theirs.edge != mine.edge || theirs.forward == mine.forward) continue;
        if (!outranks(o, t)) continue;
        // Both measured from my starting end: they have not yet passed me
        // while their position is still ahead of mine.
        if (len - o.offset_mm >= t.offset_mm) return true;
    }
    return false;
}

void Railway::tick_train(Train& t, const LocomotiveRegistry& locos) {
    t.yielding = false;
    switch (t.state) {
    case TrainState::Dwelling:
        if (t.dwell_ticks_left > 0) {
            --t.dwell_ticks_left;
            return;
        }
        t.stop_index = (t.stop_index + 1) % t.route.size();
        plan_to_current_stop(t);
        return;
    case TrainState::NoRoute:
        plan_to_current_stop(t);
        return;
    case TrainState::Moving:
        break;
    }

    if (must_yield(t)) {
        t.yielding = true;
        t.speed_mm_per_tick = 0;
        return;
    }

    const LocomotiveType& loco = locos.get(t.loco);
    const std::int64_t accel = std::max<std::int64_t>(
        1, mph_to_mm_per_tick(loco.top_speed_mph) / provisional::kAccelTicksToTopSpeed);
    const std::int64_t target = target_speed_mm_per_tick(loco, t.cars.size(), track_.grade_bp(t.path[t.step]));
    t.speed_mm_per_tick = std::min(target, t.speed_mm_per_tick + accel);

    std::int64_t remaining = t.speed_mm_per_tick;
    while (remaining > 0) {
        const std::int64_t room = track_.edge(t.path[t.step].edge).length_mm - t.offset_mm;
        if (remaining < room) {
            t.offset_mm += remaining;
            return;
        }
        remaining -= room;
        if (t.step + 1 == t.path.size()) {
            t.at_node = track_.step_end(t.path[t.step]);
            t.path.clear();
            t.step = 0;
            t.offset_mm = 0;
            t.speed_mm_per_tick = 0;
            t.state = TrainState::Dwelling;
            t.dwell_ticks_left = provisional::kStationDwellTicks;
            ++t.stops_made;
            return;
        }
        ++t.step;
        t.offset_mm = 0;
    }
}

MapPoint Railway::train_position(TrainId id) const {
    const Train& t = trains_.at(id);
    if (t.state != TrainState::Moving) return track_.node(t.at_node).pos;
    const PathStep s = t.path[t.step];
    const MapPoint a = track_.node(track_.step_start(s)).pos;
    const MapPoint b = track_.node(track_.step_end(s)).pos;
    const std::int64_t len = track_.edge(s.edge).length_mm;
    return {a.x_mm + (b.x_mm - a.x_mm) * t.offset_mm / len, a.y_mm + (b.y_mm - a.y_mm) * t.offset_mm / len};
}

} // namespace railmaster::sim
