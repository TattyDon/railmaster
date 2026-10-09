#pragma once

#include "railmaster/sim/world.hpp"

#include <string>

namespace railmaster::sim {

// A starting network for the player's company, built with the same commands
// the tools use: a triangle of track joining the first town and its two
// nearest neighbours, a station and service tower in each, a maintenance
// facility, and two trains running round it in opposite directions. The
// client opens with it, and the calibration tool measures it. Throws
// std::runtime_error if the map or the money will not allow it; returns a
// line describing what was built.
std::string build_demo_network(World& world);

} // namespace railmaster::sim
