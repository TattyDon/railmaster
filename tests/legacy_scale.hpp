#pragma once

#include "railmaster/sim/world.hpp"

// Most tests were written at the first map scale: 1 km map cells, each its
// own economy node. Pinning a fixture to it keeps hand-placed positions
// (cell_centre, kKm) meaning what they did. The shipped scale is 0.5-mile
// cells in nodes of 2 x 2 (rt3-clone-spec §3.1, §5.2).
namespace railmaster::sim::testing {

inline void legacy_scale(WorldConfig& cfg) {
    cfg.tile_size_m = 1000;
    cfg.cells_per_node = 1;
}

} // namespace railmaster::sim::testing
