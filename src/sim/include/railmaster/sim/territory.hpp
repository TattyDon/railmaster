#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/random.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace railmaster::sim {

// A named region of the map (rt3-clone-spec §3.3). Building there needs
// access rights, bought once per company [D]; a free territory is open to
// all. Modifiers [C]: a credit-rating shift for companies holding access, a
// station-cost and an overhead percentage, and borders closed to middlemen.
using TerritoryId = std::uint16_t;
constexpr TerritoryId kNoTerritory = 0xFFFF;

struct Territory {
    std::string name;
    Money access_cost{};
    std::int32_t credit_grades = 0;          // + is better: A- becomes A
    std::int32_t station_cost_percent = 100; // stations built here
    std::int32_t overhead_percent = 100;     // upkeep of track and stations here
    bool closed_border = false;              // middlemen and prices do not cross

    bool open() const { return access_cost <= Money{}; }
};

// Territories and which of them each map cell lies in.
struct TerritoryMap {
    std::vector<Territory> territories;
    std::int32_t width = 0, height = 0; // in map cells
    std::vector<TerritoryId> cells;     // row by row; kNoTerritory where none

    bool empty() const { return territories.empty(); }
    TerritoryId at(std::int32_t cx, std::int32_t cy) const;
};

// Split a map of width x height cells into `count` territories, each the
// cells nearest one of `count` random seed cells. The territory holding
// `home` (a cell) is free; the rest are priced and given modifiers from
// Balance::MapGeneration. Names are invented.
TerritoryMap generate_territories(std::int32_t width, std::int32_t height, std::int32_t count, std::int32_t home_x,
                                  std::int32_t home_y, Random& rng, const Balance::MapGeneration& b);

} // namespace railmaster::sim
