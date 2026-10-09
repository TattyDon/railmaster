#pragma once

#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/railway.hpp"

#include <cstdint>

namespace railmaster::sim {

// Freight by rail: stations gather cargo from the economy around them, and
// trains carry it to where it sells for more. Researched rules are in
// docs/spec/economy-cargo.md; the rest is documented in
// docs/spec/m2-economy-model.md.

namespace provisional {
// Catchment radius in economy cells (Chebyshev distance), by station size.
constexpr std::int32_t kCatchmentSmall = 1;  // 3 x 3 cells
constexpr std::int32_t kCatchmentMedium = 2; // 5 x 5
constexpr std::int32_t kCatchmentLarge = 3;  // 7 x 7
constexpr std::int32_t kGatherPercentPerDay = 20;   // share of catchment stock that moves to the station
constexpr std::int32_t kStationCapMilli = 20'000;  // 20 carloads of each cargo waiting
// Value lost in transit, per day per point of decay sensitivity (1-10), in
// tenths of a percent: coal loses 0.5% a day, milk 5% a day.
constexpr std::int32_t kTransitDecayPerMillePerSensitivity = 5;
// Express: a destination's pull saturates; one with this much attraction
// (e.g. 20 houses) draws half the traffic it could.
constexpr std::int32_t kExpressAttractionHalf = 20;
constexpr std::int32_t kExpressCapMilli = 20'000; // 20 loads per destination waiting
// Waiting express loads give up, per day per point of decay sensitivity,
// in tenths of a percent: passengers (9) lose 4.5% a day.
constexpr std::int32_t kExpressWaitLossPerMillePerSensitivity = 5;
constexpr std::int32_t kMailCapMonths = 2; // a town pays for two months' worth of mail a month
} // namespace provisional

std::int32_t catchment_radius(StationSize size);

// The best (highest) and cheapest prices for a cargo within a station's catchment.
struct CatchmentPrices {
    std::int32_t best = 0;
    std::int32_t best_cx = 0, best_cy = 0;
};
CatchmentPrices catchment_prices(const Economy& eco, const Railway& rw, const Station& s, CargoId c);

// Share of a load's value left after `days` in transit, in thousandths.
std::int32_t value_left_permille(const CargoType& c, std::int32_t days);

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
Money handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, const IndustryRegistry& industries,
                     TrainId train, StationId station, std::int32_t today, std::uint64_t tick);

} // namespace railmaster::sim
