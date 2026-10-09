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
// attract (inputs) a cargo: the sum of their yearly rate x level.
std::int64_t catchment_rate(const Economy& eco, const Railway& rw, const IndustryRegistry& industries,
                            const Station& s, CargoId c, bool outputs);

// Express fare for one full load between two stations, before decay.
Money express_fare(const CargoType& c, const Railway& rw, StationId from, StationId to);

// Each day: every station draws in the freight around it that some train
// calling there could sell for more at another stop on its route, and its
// houses and barracks generate express loads for each station that shares
// a train route with it (more connections, more traffic). Waiting express
// loads dwindle as passengers give up and mail goes stale.
void gather_at_stations(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        std::int32_t year);

// Monthly: reset the mail demand caps.
void start_new_month(Railway& rw);

// A train has arrived at a station. Unload every car whose cargo sells here
// for more than it cost: RT3 unloads at the first such stop. Cargo is never
// sold back where it was loaded, which would pay out on small price
// differences within one catchment. Then fill empty cars with the waiting
// cargo worth most further along the route. Delivered cargo is added to the
// local economy. Returns the income.
// Express loads are only unloaded at their destination, earning the fare
// less decay (mail past the town's monthly demand earns nothing).
// `revenue_permille` scales all income here (difficulty x station age).
Earnings handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                        TrainId train, StationId station, std::int32_t today, std::uint64_t tick,
                        std::int32_t revenue_permille = 1000);

} // namespace railmaster::sim
