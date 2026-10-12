#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/date.hpp"
#include "railmaster/sim/locomotive.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/track.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace railmaster::sim {


constexpr std::size_t kMaxCarsPerTrain = 8;  // RT3 manual
constexpr std::int32_t kGaugeFull = 1000;

using StationId = std::uint32_t;
using TrainId = std::uint32_t;
using ServiceBuildingId = std::uint32_t;

enum class StationSize : std::uint8_t { Small, Medium, Large };

Money station_cost(StationSize size, const Balance& b = default_balance());

// Cargo gathered at a station, waiting for a train.
struct WaitingCargo {
    std::int32_t milli = 0;
    std::int64_t value = 0; // sum of pickup price x milli, for the average pickup price

    std::int32_t average_price() const { return milli > 0 ? static_cast<std::int32_t>(value / milli) : 0; }
};

// Express loads (passengers, mail, troops) waiting at a station for one destination.
struct ExpressWaiting {
    CargoId cargo = 0;
    std::uint32_t destination = 0; // StationId
    std::int32_t milli = 0;
};

struct Station {
    StationId id = 0;
    std::string name;
    NodeId node = 0;
    StationSize size = StationSize::Small;
    CompanyId owner = 0; // any company's trains may use it [D]
    std::vector<WaitingCargo> waiting{}; // indexed by CargoId; sized by the freight code
    std::vector<ExpressWaiting> express{}; // sorted by (cargo, destination)
    std::vector<std::int32_t> received_this_month{}; // express milli delivered here, by CargoId
    std::optional<std::size_t> town{}; // index into Economy::towns(); none in open country
    std::int32_t built_day = 0;
    // Passenger traffic this month, for the station-area buildings: loads
    // boarded, loads arriving, and loads waiting summed over the days.
    std::int64_t passengers_boarded_milli = 0;
    std::int64_t passengers_arrived_milli = 0;
    std::int64_t passengers_waiting_milli_days = 0;
    // Income from trains arriving here: this month, and the last 12 months
    // (newest last), for the profitability overlay (rt3-clone-spec §15.3).
    Money income_this_month{};
    std::vector<Money> income_months{};
    Money income_last_year() const;
};

// How a train or station is doing, for the profitability overlay
// (rt3-clone-spec §15.3: green, yellow, red) by Balance::overlays: over the
// months on record (up to 12), a loss is red; a return of at least the good
// share a year green (a train against its engine's price, a station its
// building cost); anything between yellow. Nothing on record yet is yellow.
enum class Rating : std::uint8_t { Loss, Low, Good };
Rating rate(Money earned, std::size_t months, Money price, const Balance& b);

// Buildings placed near stations (rt3-clone-spec §7.2): a post office keeps
// mail waiting longer; a hotel keeps passengers longer and earns from those
// waiting; a restaurant earns from every passenger passing; a tavern from
// those boarding. Any company may build them by anyone's station.
enum class StationBuildingType : std::uint8_t { PostOffice, Hotel, Restaurant, Tavern };
const char* station_building_name(StationBuildingType t);
Money station_building_cost(StationBuildingType t, const Balance& b = default_balance());

struct StationBuilding {
    std::uint32_t id = 0;
    StationBuildingType type = StationBuildingType::Restaurant;
    CompanyId owner = 0;
    MapPoint pos{};
};

// RT3 has two support buildings that sit on the track: the service tower
// (water and sand) and the maintenance facility (oil).
enum class ServiceType : std::uint8_t { ServiceTower, MaintenanceFacility };

Money service_building_cost(ServiceType type, const Balance& b = default_balance());

struct ServiceBuilding {
    ServiceBuildingId id = 0;
    ServiceType type = ServiceType::ServiceTower;
    NodeId node = 0;
    CompanyId owner = 0;
};

enum class TrainState : std::uint8_t {
    Dwelling,   // stopped at a station
    Moving,
    Servicing,  // stopped at a service building, part-way along its path
    BrokenDown, // stopped where it failed, part-way along its path
    NoRoute,    // next stop is unreachable; retries every tick
    Crashed,    // destroyed: no longer runs
    Retired,    // taken out of service by its owner: no longer runs
};

// What a train takes on at a stop (rt3-clone-spec §9.3 [D]). Auto: the most
// profitable loads for the stops ahead, of the filtered class, up to `max`
// cars. Custom: exactly the listed cargo, a car each. Either way the train
// waits at the stop until it has at least `min` loaded cars ("wait for a
// full load").
enum class CargoFilter : std::uint8_t { Any, Freight, Express };
struct ConsistRule {
    bool custom = false;
    CargoFilter filter = CargoFilter::Any;
    std::uint8_t min = 0;
    std::uint8_t max = 4;
    std::vector<CargoId> cars{}; // custom only
    std::size_t capacity() const { return custom ? cars.size() : max; }
    bool operator==(const ConsistRule&) const = default;
};
const char* cargo_filter_name(CargoFilter f);
// Caboose: halves the chance of a breakdown. Dining car: passenger fares on
// the train pay 20% more. Each takes a car slot [D].
constexpr std::int32_t kCabooseBreakdownPercent = 50;
constexpr std::int32_t kDinerPassengerPercent = 120;

// One freight car and what it carries.
struct Car {
    std::optional<CargoId> cargo;  // empty car if unset
    std::int32_t milli = 0;        // amount, in thousandths of a carload
    std::int32_t pickup_price = 0; // dollars per carload where it was collected
    std::int32_t loaded_day = 0;   // Date::days_since_epoch() when loaded
    std::uint32_t loaded_at = 0;   // StationId where loaded; never sold back there
    std::optional<std::uint32_t> destination; // express only: the StationId it must reach
    std::int32_t expected_value = 0; // dollars this load should earn; decides meets at equal priority
    std::uint64_t loaded_tick = 0;   // World::total_ticks() when loaded: express pay goes by trip time
    // Express only: the hub where this load changes trains, when no train
    // runs from where it boarded to its destination (rt3-clone-spec §8.3).
    std::optional<std::uint32_t> via{};
};

struct Train {
    TrainId id = 0;
    CompanyId owner = 0;
    LocoTypeId loco = 0;
    std::vector<Car> cars;
    std::int32_t priority = 0; // higher wins meets on single track; then more valuable cargo [D]
    std::vector<StationId> route;
    std::vector<ConsistRule> rules; // one per route stop
    // One per route stop: 1 if the player made it a service stop, where the
    // train fills up at the station's service buildings even when not low.
    std::vector<std::uint8_t> service_stop;
    bool caboose = false;
    bool diner = false;
    bool holding = false; // waiting at a stop for its minimum load
    std::size_t stop_index = 0; // the stop being travelled to, or dwelt at

    TrainState state = TrainState::Dwelling;
    NodeId at_node = 0; // valid when Dwelling or NoRoute
    std::vector<PathStep> path;
    std::size_t step = 0;
    std::int64_t offset_mm = 0; // distance travelled along path[step]
    std::int64_t speed_mm_per_tick = 0;
    std::int32_t wait_ticks_left = 0; // for Dwelling, Servicing and BrokenDown
    bool yielding = false; // stopped this tick to let a higher-priority train pass
    // Track ahead was removed: at the end of the current piece, find a new
    // way to the next stop.
    bool replan = false;

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
    std::int64_t road_ticks = 0; // lifetime ticks running, servicing, broken down or held on the line

    std::int32_t built_day = 0;       // Date::days_since_epoch() when bought
    std::int64_t distance_mm = 0;     // lifetime distance run
    // Fuel burnt since the last bill: distance x weight moved (train and
    // engine) x fuel economy x the uphill surcharge, in kg-km.
    std::int64_t fuel_kg_km = 0;
    // Distance run on each company's track (indexed by CompanyId) since the
    // last stop, to share that stop's income with the track's owners.
    std::vector<std::int64_t> leg_mm_by_owner{};

    Money revenue;                    // lifetime earnings
    // Earnings less its running costs (maintenance, fuel, repairs): this
    // month, and the last 12 months (newest last), for the profitability
    // overlay (rt3-clone-spec §15.3).
    Money profit_this_month{};
    std::vector<Money> profit_months{};
    Money profit_last_year() const;
    Money last_income;                // from the most recent stop
    std::uint64_t last_income_tick = 0;

    // Still running: not wrecked or retired.
    bool in_service() const { return state != TrainState::Crashed && state != TrainState::Retired; }
    std::size_t loaded_cars() const;
    // Cars actually pulled: the loaded ones plus a caboose and dining car.
    // Empty cars are left behind, so a light train runs faster.
    std::size_t hauled_cars() const { return loaded_cars() + caboose + diner; }
    // Cargo cars the rule for route stop `i` may take, after special cars.
    std::size_t slots_at(std::size_t i) const;
};

// Where two lines cross without joining (rt3-clone-spec §11.2 [D]). At a
// level crossing trains take turns; an overpass lets both run on.
struct Crossing {
    EdgeId a = 0, b = 0;
    MapPoint at{};
    bool overpass = false;
};

// Game rules that change how trains are operated.
struct OperatingRules {
    // RT3's sandbox has "allow breakdown/crash for locomotives", off by
    // default; normal play has breakdowns. Crashes are not modelled yet.
    bool breakdowns = true;
};

// Convert a speed in miles per hour into distance per simulation tick.
std::int64_t mph_to_mm_per_tick(std::int64_t mph, const Balance& b = default_balance());

// Ticks of train movement per game day (World::kTicksPerDay).
inline constexpr std::int32_t kTrainTicksPerDay = 16;

// Weight of one loaded car in `year`, in kg: the era's freight car, or 2/3
// of it for an express car, caboose or dining car (rt3-clone-spec §4.2 [D]).
// Weight goes by today's date, not the train's age [D].
std::int64_t car_weight_kg(bool express, std::int32_t year, const Balance& b = default_balance());
// The weight the speed chart assumes: reference_cars freight cars of the era.
std::int64_t reference_weight_kg(std::int32_t year, const Balance& b = default_balance());
// What a train pulls: its loaded cars, caboose and dining car (the engine's
// own weight is part of its figures).
std::int64_t train_weight_kg(const Train& t, std::int32_t year, const Balance& b = default_balance());

// Speed an engine can hold pulling `weight_kg` up a grade (downhill counts
// as flat), before curves, bridges and water: top speed x load factor x
// grade factor (rt3-clone-spec §10.2 [I]). Without sand it loses
// no_sand_grade_drop_percent as much on grades.
std::int64_t speed_for_weight_mm_per_tick(const LocomotiveType& loco, std::int64_t weight_kg,
                                          std::int64_t reference_kg, std::int32_t grade_bp,
                                          const Balance& b = default_balance(), bool sand = true);
// The Add Train speed-vs-grade chart [D]: `cars` loaded freight or express
// cars of `year`.
std::int64_t chart_speed_mm_per_tick(const LocomotiveType& loco, std::size_t cars, bool express, std::int32_t year,
                                     std::int32_t grade_bp, const Balance& b = default_balance());
// A train's own target speed on a grade: its weight, sand and water.
std::int64_t target_speed_mm_per_tick(const LocomotiveType& loco, const Train& train, std::int32_t year,
                                      std::int32_t grade_bp, const Balance& b = default_balance());
// Curve factor, in thousandths, for the turn from one piece of track to the
// next: the radius the turn implies over the comfort radius.
std::int32_t curve_speed_permille(MapPoint a, MapPoint joint, MapPoint b, const Balance& b_ = default_balance());

// Yearly maintenance for one locomotive: rises 4% of the new figure for
// each year of age, without limit, and by half again while out of oil
// (rt3-clone-spec §9.4 [I]).
Money annual_maintenance(const LocomotiveType& loco, std::int32_t age_years, std::int32_t oil,
                         const Balance& b = default_balance());

// Track, stations, support buildings and trains, and the rules that move trains.
class Railway {
public:
    explicit Railway(std::uint64_t seed = 1) : rng_(seed, 0x7261696c) {}

    TrackNetwork& track() { return track_; }
    const TrackNetwork& track() const { return track_; }

    void set_rules(OperatingRules rules) { rules_ = rules; }
    void set_balance(const Balance& b) { balance_ = b; }
    const Balance& balance() const { return balance_; }
    const OperatingRules& rules() const { return rules_; }

    // Split a piece of track (see TrackNetwork::split_edge) and patch the
    // paths of trains using it, so they carry on undisturbed.
    NodeId split_edge(EdgeId e, MapPoint at);
    // Take pieces of track out of the network. Trains whose way ahead ran
    // over them find another at the end of the piece they are on; callers
    // must not remove a piece a train is on (see train_on).
    void remove_edges(const std::vector<EdgeId>& edges);
    // Give a train new stops. Stops at stations the old route called at keep
    // their consist rules; new ones take the old first stop's. A train
    // heading for (or standing at) a station still on the route carries on
    // as before; otherwise it makes for the first stop, from the end of the
    // piece of track it is on.
    void set_route(TrainId id, std::vector<StationId> route, std::vector<std::uint8_t> service_stop);
    // Trains that ran onto each piece of track (by EdgeId): this month and
    // last month, for the traffic overlay (rt3-clone-spec §15.3).
    std::uint32_t traffic_last_month(EdgeId e) const { return e < traffic_last_.size() ? traffic_last_[e] : 0; }
    // Ticks trains spent held on each piece last month, waiting for another
    // to pass on single track: where a second track would help.
    std::uint32_t held_last_month(EdgeId e) const { return e < held_last_.size() ? held_last_[e] : 0; }
    // Single track where trains were held waiting for others at least
    // Balance::overlays.held_days_for_double last month.
    bool needs_double_track(EdgeId e) const;
    // At a month end: file this month's profits, income and traffic.
    void close_month();
    // Stop a train out on the line for `days`, as a breakdown (with the
    // repair bill); false if it is not on the line.
    bool break_down(TrainId id, std::int32_t days);
    void add_crossing(Crossing c) { crossings_.push_back(c); }
    const std::vector<Crossing>& crossings() const { return crossings_; }
    // The train standing or running on a piece of track, if any.
    std::optional<TrainId> train_on(EdgeId e) const;
    // The run of track a piece belongs to: it and its neighbours out to the
    // nearest junction, station, support building, end of line or change
    // of owner.
    std::vector<EdgeId> run_of(EdgeId e) const;

    StationId add_station(std::string name, NodeId node, StationSize size, CompanyId owner = 0);
    const Station& station(StationId id) const { return stations_.at(id); }
    // Why an electric engine could not run this route, if it could not:
    // every leg between consecutive stops, the last back to the first, must
    // be possible on electrified track (rt3-clone-spec §10.2 [D]).
    std::optional<std::string> electric_route_problem(const std::vector<StationId>& route) const;
    const std::vector<Station>& stations() const { return stations_; }

    ServiceBuildingId add_service_building(ServiceType type, NodeId node, CompanyId owner = 0);

    std::uint32_t add_station_building(StationBuildingType type, MapPoint pos, CompanyId owner);
    const std::vector<StationBuilding>& station_buildings() const { return station_buildings_; }
    // Is there a building of this type within range of the station?
    bool near_building(StationId s, StationBuildingType type) const;
    const std::vector<ServiceBuilding>& service_buildings() const { return service_buildings_; }

    // The train starts stopped at the first station of its route.
    // Throws if there are more than kMaxCarsPerTrain cars or the route is empty.
    TrainId add_train(LocoTypeId loco, std::size_t car_count, std::vector<StationId> route,
                      std::int32_t priority = 0, CompanyId owner = 0);
    Train& train_mut(TrainId id) { return trains_.at(id); }
    Station& station_mut(StationId id) { return stations_.at(id); }

    // Hand everything `from` owns (track, stations, buildings, trains) to `to`.
    void transfer_owner(CompanyId from, CompanyId to);

    // Trains that reached a station since the last call, in arrival order,
    // with the station. The world handles cargo for each.
    std::vector<std::pair<TrainId, StationId>> take_arrivals();
    // Trains destroyed in crashes since the last call.
    std::vector<TrainId> take_crashes();
    // Trains that broke down since the last call, to charge their repairs.
    std::vector<TrainId> take_breakdowns();
    // The current date, for train ages (crash risk).
    void set_today(std::int32_t day) {
        today_ = day;
        year_ = Date::from_days(day).year();
    }
    const Train& train(TrainId id) const { return trains_.at(id); }
    const std::vector<Train>& trains() const { return trains_; }

    // Advance every train by one tick, in id order.
    void tick(const LocomotiveRegistry& locos);

    // Where a train is now, interpolated along its current piece of track.
    MapPoint train_position(TrainId id) const;

    // Saving and loading (save.cpp) reads and writes every member.
    template <class Ar>
    friend void io(Ar& ar, Railway& v);

private:
    void tick_train(Train& t, const LocomotiveRegistry& locos);
    // Electric engines keep to electrified track [D].
    void plan_to_current_stop(Train& t, bool electric);
    bool must_yield(const Train& t) const;
    void use_supplies(Train& t, const LocomotiveType& loco, std::int64_t distance_mm, std::int32_t grade_bp);
    // The nearest train ahead going the same way on single track, within
    // follow_distance along t's path: how far ahead it is and its speed.
    struct Leader {
        std::int64_t distance_mm;
        std::int64_t speed_mm_per_tick;
    };
    std::optional<Leader> leader_ahead(const Train& t) const;
    bool roll_breakdown(const Train& t, const LocomotiveType& loco, std::int64_t distance_mm);
    bool roll_crash(const Train& t, const LocomotiveType& loco);
    // Refill at any support buildings at `node` the train needs. True if it stopped.
    bool service_at(Train& t, NodeId node, const LocomotiveType& loco, bool always = false);
    void arrive_at_stop(Train& t);

    TrackNetwork track_;
    std::vector<Station> stations_;
    std::vector<ServiceBuilding> service_buildings_;
    std::vector<StationBuilding> station_buildings_;
    std::vector<Train> trains_;
    std::vector<std::pair<TrainId, StationId>> arrivals_;
    std::vector<TrainId> crashes_;
    std::vector<TrainId> breakdowns_;
    std::vector<std::uint32_t> traffic_, traffic_last_; // by EdgeId
    std::vector<std::uint32_t> held_, held_last_;       // by EdgeId
    void count_traffic(EdgeId e);
    std::vector<Crossing> crossings_;
    // How far t may go before the first level crossing it must wait at, if any.
    std::optional<std::int64_t> crossing_stop(const Train& t) const;
    std::int32_t today_ = 0;
    std::int32_t year_ = Date::kEpochYear;
    OperatingRules rules_;
    Balance balance_ = default_balance();
    Random rng_;
};

} // namespace railmaster::sim
