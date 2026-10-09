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

// Each day: every station draws in the freight around it that some train
// calling there could sell for more at another stop on its route.
void gather_at_stations(Railway& rw, Economy& eco, const CargoRegistry& cargo);

// A train has arrived at a station. Unload every car whose cargo sells here
// for more than it cost: RT3 unloads at the first such stop. Cargo is never
// sold back where it was loaded, which would pay out on small price
// differences within one catchment. Then fill empty cars with the waiting
// cargo worth most further along the route. Delivered cargo is added to the
// local economy. Returns the income.
Money handle_arrival(Railway& rw, Economy& eco, const CargoRegistry& cargo, TrainId train, StationId station,
                     std::int32_t today, std::uint64_t tick);

} // namespace railmaster::sim
