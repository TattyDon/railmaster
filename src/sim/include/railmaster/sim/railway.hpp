#pragma once

#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/locomotive.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/track.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace railmaster::sim {

// Stand-ins for rules the research has not pinned down. Every value here is
// documented in docs/spec/m1-provisional-models.md and is expected to change.
namespace provisional {
constexpr std::int64_t kTrainSecondsPerTick = 40;
constexpr std::int32_t kAccelTicksToTopSpeed = 8;
constexpr std::int32_t kStationDwellTicks = 8;
constexpr std::int32_t kMinSpeedPermille = 100;
} // namespace provisional

constexpr std::size_t kMaxCarsPerTrain = 8; // RT3 manual

using StationId = std::uint32_t;
using TrainId = std::uint32_t;

enum class StationSize : std::uint8_t { Small, Medium, Large };

Money station_cost(StationSize size);

struct Station {
    StationId id = 0;
    std::string name;
    NodeId node = 0;
    StationSize size = StationSize::Small;
};

enum class TrainState : std::uint8_t {
    Dwelling, // stopped at a station
    Moving,
    NoRoute,  // next stop is unreachable; retries every tick
};

struct Train {
    TrainId id = 0;
    LocoTypeId loco = 0;
    std::vector<CargoId> cars;
    std::int32_t priority = 0; // higher wins meets on single track
    std::vector<StationId> route;
    std::size_t stop_index = 0; // the stop being travelled to, or dwelt at

    TrainState state = TrainState::Dwelling;
    NodeId at_node = 0; // valid unless Moving
    std::vector<PathStep> path;
    std::size_t step = 0;
    std::int64_t offset_mm = 0; // distance travelled along path[step]
    std::int64_t speed_mm_per_tick = 0;
    std::int32_t dwell_ticks_left = 0;
    bool yielding = false; // stopped this tick to let a higher-priority train pass
    std::uint32_t stops_made = 0;
};

// Convert a speed in miles per hour into distance per simulation tick.
std::int64_t mph_to_mm_per_tick(std::int64_t mph);

// Speed a locomotive can hold on a given grade with a given number of cars.
std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, std::size_t cars, std::int32_t grade_bp);

// Track, stations and trains, and the rules that move trains.
class Railway {
public:
    TrackNetwork& track() { return track_; }
    const TrackNetwork& track() const { return track_; }

    StationId add_station(std::string name, NodeId node, StationSize size);
    const Station& station(StationId id) const { return stations_.at(id); }
    const std::vector<Station>& stations() const { return stations_; }

    // The train starts stopped at the first station of its route.
    // Throws if there are more than kMaxCarsPerTrain cars or the route is empty.
    TrainId add_train(LocoTypeId loco, std::vector<CargoId> cars, std::vector<StationId> route,
                      std::int32_t priority = 0);
    const Train& train(TrainId id) const { return trains_.at(id); }
    const std::vector<Train>& trains() const { return trains_; }

    // Advance every train by one tick, in id order.
    void tick(const LocomotiveRegistry& locos);

    // Where a train is now, interpolated along its current piece of track.
    MapPoint train_position(TrainId id) const;

private:
    void tick_train(Train& t, const LocomotiveRegistry& locos);
    void plan_to_current_stop(Train& t);
    bool must_yield(const Train& t) const;

    TrackNetwork track_;
    std::vector<Station> stations_;
    std::vector<Train> trains_;
};

} // namespace railmaster::sim
