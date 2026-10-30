#pragma once

#include "railmaster/sim/balance.hpp"

#include <cstddef>
#include <cstdint>

namespace railmaster::sim {

class Random;

// The business cycle [C] (rt3-clone-spec §5.5). It moves at random, a step
// at a time, and is checked a few times a year. Its effects (production,
// costs, the prime rate, share prices) are in Balance::EconomicStates.
enum class EconomicState : std::uint8_t { Depression, Recession, Normal, Prosperity, Boom };

const char* economic_state_name(EconomicState s);

inline std::size_t index_of(EconomicState s) { return static_cast<std::size_t>(s); }

// One check: stay, move a step toward Normal, or a step away from it [I].
// From Normal, the move is up or down with equal odds; at either end, a move
// away becomes a stay.
EconomicState next_economic_state(EconomicState s, Random& rng, const Balance::EconomicStates& b);

} // namespace railmaster::sim
