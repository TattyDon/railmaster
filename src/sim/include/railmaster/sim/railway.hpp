#pragma once

#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/locomotive.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/random.hpp"
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

// Servicing. Gauges run from 0 (empty) to 1000 (full).
constexpr std::int64_t kWaterRangeMm = 150'000'000;     // a steam engine empties its tender in 150 km
constexpr std::int64_t kSandRangeClimbMm = 600'000;      // sand runs out after 600 m of total climb
constexpr std::int64_t kOilRangeMm = 1'500'000'000;      // oil runs out after 1,500 km
constexpr std::int32_t kServiceThresholdPermille = 500;  // stop to service when a gauge is below half
constexpr std::int32_t kServiceTicks = 4;                // a quarter of a day per stop
constexpr std::int32_t kNoWaterSpeedPermille = 250;      // steam with no water: a quarter of top speed
constexpr std::int32_t kNoSandGradePermille = 400;       // no sand: 40% of normal climbing ability
constexpr std::int64_t kServiceTowerCost = 30'000;
constexpr std::int64_t kMaintenanceFacilityCost = 100'000;

// Breakdowns.
constexpr std::int64_t kMeanBreakdownDistanceMm = 2'000'000'000; // 2,000 km at reliability 100, full oil
constexpr std::int32_t kEmptyOilBreakdownMultiplier = 4;         // empty oil: 4x the breakdown rate
constexpr std::int32_t kBreakdownTicks = 32;                     // two days stopped

// Maintenance cost growth.
constexpr std::int32_t kMaintenanceAgeCapYears = 20;
constexpr std::int32_t kMaintenanceLowOilMultiplier = 2;
} // namespace provisional

constexpr std::size_t kMaxCarsPerTrain = 8;  // RT3 manual
constexpr std::int32_t kGaugeFull = 1000;

using StationId = std::uint32_t;
using TrainId = std::uint32_t;
using ServiceBuildingId = std::uint32_t;

enum class StationSize : std::uint8_t { Small, Medium, Large };

Money station_cost(StationSize size);

struct Station {
    StationId id = 0;
    std::string name;
    NodeId node = 0;
    StationSize size = StationSize::Small;
};

// RT3 has two support buildings that sit on the track: the service tower
// (water and sand) and the maintenance facility (oil).
enum class ServiceType : std::uint8_t { ServiceTower, MaintenanceFacility };

Money service_building_cost(ServiceType type);

struct ServiceBuilding {
    ServiceBuildingId id = 0;
    ServiceType type = ServiceType::ServiceTower;
    NodeId node = 0;
};

enum class TrainState : std::uint8_t {
    Dwelling,   // stopped at a station
    Moving,
    Servicing,  // stopped at a service building, part-way along its path
    BrokenDown, // stopped where it failed, part-way along its path
    NoRoute,    // next stop is unreachable; retries every tick
};

struct Train {
    TrainId id = 0;
    LocoTypeId loco = 0;
    std::vector<CargoId> cars;
    std::int32_t priority = 0; // higher wins meets on single track
    std::vector<StationId> route;
    std::size_t stop_index = 0; // the stop being travelled to, or dwelt at

    TrainState state = TrainState::Dwelling;
    NodeId at_node = 0; // valid when Dwelling or NoRoute
    std::vector<PathStep> path;
    std::size_t step = 0;
    std::int64_t offset_mm = 0; // distance travelled along path[step]
    std::int64_t speed_mm_per_tick = 0;
    std::int32_t wait_ticks_left = 0; // for Dwelling, Servicing and BrokenDown
    bool yielding = false; // stopped this tick to let a higher-priority train pass

    // Gauges, 0..kGaugeFull. Water only matters for steam engines.
    std::int32_t water = kGaugeFull;
    std::int32_t sand = kGaugeFull;
    std::int32_t oil = kGaugeFull;
    std::int64_t water_used_mm = 0; // distance run since the last fill, for gauge arithmetic
    std::int64_t sand_used_climb_mm = 0;
    std::int64_t oil_used_mm = 0;

    std::uint32_t stops_made = 0;
    std::uint32_t service_stops = 0;
    std::uint32_t breakdowns = 0;
};

// Game rules that change how trains are operated.
struct OperatingRules {
    // RT3's sandbox has "allow breakdown/crash for locomotives", off by
    // default; normal play has breakdowns. Crashes are not modelled yet.
    bool breakdowns = true;
};

// Convert a speed in miles per hour into distance per simulation tick.
std::int64_t mph_to_mm_per_tick(std::int64_t mph);

// Speed a locomotive can hold on a given grade with a given number of cars,
// before servicing effects.
std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, std::size_t cars, std::int32_t grade_bp);

// As above, including the effect of the train's water and sand gauges.
std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, const Train& train, std::int32_t grade_bp);

// Yearly maintenance for one locomotive: rises with age to 3x by year 20,
// and doubles while oil is below the service threshold.
Money annual_maintenance(const LocomotiveType& loco, std::int32_t age_years, std::int32_t oil);

// Track, stations, support buildings and trains, and the rules that move trains.
class Railway {
public:
    explicit Railway(std::uint64_t seed = 1) : rng_(seed, 0x7261696c) {}

    TrackNetwork& track() { return track_; }
    const TrackNetwork& track() const { return track_; }

    void set_rules(OperatingRules rules) { rules_ = rules; }
    const OperatingRules& rules() const { return rules_; }

    // Split a piece of track (see TrackNetwork::split_edge) and patch the
    // paths of trains using it, so they carry on undisturbed.
    NodeId split_edge(EdgeId e, MapPoint at);

    StationId add_station(std::string name, NodeId node, StationSize size);
    const Station& station(StationId id) const { return stations_.at(id); }
    const std::vector<Station>& stations() const { return stations_; }

    ServiceBuildingId add_service_building(ServiceType type, NodeId node);
    const std::vector<ServiceBuilding>& service_buildings() const { return service_buildings_; }

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
    void use_supplies(Train& t, const LocomotiveType& loco, std::int64_t distance_mm, std::int32_t grade_bp);
    bool roll_breakdown(const Train& t, const LocomotiveType& loco, std::int64_t distance_mm);
    // Refill at any support buildings at `node` the train needs. True if it stopped.
    bool service_at(Train& t, NodeId node, const LocomotiveType& loco);
    void arrive_at_stop(Train& t);

    TrackNetwork track_;
    std::vector<Station> stations_;
    std::vector<ServiceBuilding> service_buildings_;
    std::vector<Train> trains_;
    OperatingRules rules_;
    Random rng_;
};

} // namespace railmaster::sim
