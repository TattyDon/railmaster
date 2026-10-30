#pragma once

#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/railway.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace railmaster::sim {

// Freight by rail: stations gather cargo from the economy around them, and
// trains carry it to where it sells for more. Researched rules are in
// docs/spec/economy-cargo.md; the rest is documented in
// docs/spec/m2-economy-model.md.

// Catchment, gathering, decay and express rates come from the railway's
// Balance (stations, freight, express sections).

std::int32_t catchment_radius(StationSize size, const Balance& b = default_balance());

// What a train earned at one stop, in total and by cargo.
struct Earnings {
    Money total;
    std::vector<std::pair<CargoId, Money>> by_cargo;
    // Loads delivered, in thousandths, by cargo (paid or not), and the
    // station each was picked up at (for travellers who changed trains, the
    // last one; an id past the stations if it has since been taken down).
    struct Delivery {
        CargoId cargo = 0;
        std::int32_t milli = 0;
        StationId from = 0;
    };
    std::vector<Delivery> delivered;

    void add(CargoId c, Money m) {
        total += m;
        by_cargo.emplace_back(c, m);
    }
};

// The best (highest) and cheapest prices for a cargo within a station's catchment.
struct CatchmentPrices {
    std::int32_t best = 0;
    std::int32_t best_cx = 0, best_cy = 0;
};
CatchmentPrices catchment_prices(const Economy& eco, const Railway& rw, const Station& s, CargoId c);

// The best price for cargo `c` a station of `size` at `at` would reach: as
// catchment_prices gives a built one, for planning one (the AI's lines and
// the Guide's links).
std::int64_t best_price_within_reach(const Economy& eco, const Balance& b, MapPoint at, CargoId c,
                                     StationSize size = StationSize::Medium);

// Share of a load's value left after `days` in transit, in thousandths;
// 0 once it falls below the expiry level.
std::int32_t value_left_permille(const CargoType& c, std::int32_t days, const Balance& b = default_balance());

// Revenue modifiers at a destination [D, rt3-clone-spec §8.4].
enum class Difficulty : std::uint8_t { Easy, Medium, Hard, Expert };
std::int32_t difficulty_revenue_permille(Difficulty d); // 1200, 1000, 900, 800
// Station age: +15% when the town's first station is new, 0 at 4 years,
// -10% from 20 years; half the effect for a station in open country.
std::int32_t station_age_permille(std::int32_t days_since_first_station, bool open_country);

// How strongly the sites in a station's catchment produce (outputs) or
// attract (inputs) a cargo: the sum of their yearly rate x level, in
// thousandths of a carload.
std::int64_t catchment_rate(const Economy& eco, const Railway& rw, const IndustryRegistry& industries,
                            const Station& s, CargoId c, bool outputs);

// Express fare for one full load between two stations, before decay.
Money express_fare(const CargoType& c, const Railway& rw, StationId from, StationId to);
// The fare for `distance_mm` before speed and appeal: rate per km x the
// reference distance x (distance / reference)^0.9 (rt3-clone-spec §8.3 [I]).
Money express_base_fare(const CargoType& c, std::int64_t distance_mm, const Balance& b = default_balance());
// The era's reference express speed, in mph.
std::int32_t reference_speed_mph(std::int32_t year, const Balance& b = default_balance());
// How many more passengers and mail towns make than in 1830, in percent
// (rt3-clone-spec §8.3: spawning x an era factor [I]).
std::int32_t express_era_percent(std::int32_t year, const Balance& b = default_balance());
// Speed factor, in thousandths: the trip's time at the reference speed over
// its actual time, within the balance's bounds.
std::int32_t express_speed_permille(std::int64_t distance_mm, std::uint64_t ticks, std::int32_t year,
                                    const Balance& b = default_balance());

// Each day: every station draws in the freight around it that some train
// calling there could sell for more at another stop on its route, and the
// houses and barracks it reaches (shared with any other station that
// reaches them) generate express loads for the homes reached by the
// stations its trains connect it to (more connections, more traffic).
// Waiting express loads dwindle as passengers give up and mail goes stale.
void gather_at_stations(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        std::int32_t year, std::int32_t today);

// Monthly: homes whose travellers mostly gave up last month avoid the
// railway for a while; then reset the month's counts and the mail demand caps.
void start_new_month(Railway& rw, const Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                     std::int32_t today);

// For a station's panel: of the homes it reaches that make cargo `c`, how
// many avoid the railway now, and until when (the latest).
struct HomesAvoiding {
    std::int32_t avoiding = 0, homes = 0;
    std::int32_t until = 0; // a day, Date::days_since_epoch()
};
HomesAvoiding homes_avoiding(const Economy& eco, const Railway& rw, const IndustryRegistry& industries, StationId s,
                             CargoId c, std::int32_t today);

// A train has arrived at a station. Unload every car whose cargo sells here
// for more than it cost: RT3 unloads at the first such stop. Cargo is never
// sold back where it was loaded, which would pay out on small price
// differences within one catchment. Then fill empty cars with the waiting
// cargo worth most further along the route. Delivered cargo is added to the
// local economy. Returns the income.
// Express loads are only unloaded at their destination, earning the fare
// less decay (mail past the town's monthly demand earns nothing).
// `revenue_permille` scales all income here (difficulty x station age);
// express fares also go by trip speed, and passenger and mail fares by
// the engine's `appeal_percent`.
Earnings handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        TrainId train, StationId station, std::int32_t today, std::uint64_t tick,
                        std::int32_t revenue_permille = 1000, std::int32_t appeal_percent = 0);

} // namespace railmaster::sim
