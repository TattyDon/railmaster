#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/freight.hpp"
#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

CargoRegistry no_cargo() { return CargoRegistry::from_json(R"({"cargo": []})"); }

constexpr std::int64_t kCellMm = 805'000; // 0.5 mile
constexpr std::int64_t kNodeMm = 2 * kCellMm;

// Nodes in a station's catchment, for a station at `p`.
int catchment_nodes(const Economy& eco, MapPoint p, StationSize size) {
    int n = 0;
    eco.for_nodes_within(p, eco.cells_mm(catchment_radius(size)), [&](std::int32_t, std::int32_t) { ++n; });
    return n;
}

} // namespace

TEST_CASE("map presets and about 15,000 economy nodes [I, D]") {
    CHECK(map_dimensions(MapSize::Small).width == 256);
    CHECK(map_dimensions(MapSize::Medium).width == 384);
    CHECK(map_dimensions(MapSize::Medium).height == 512);
    CHECK(map_dimensions(MapSize::Large).height == 1024);
    CHECK(default_cells_per_node(256, 256) == 2); // 128 x 128 = 16,384 nodes
    CHECK(default_cells_per_node(384, 512) == 4); // 96 x 128 = 12,288
    CHECK(default_cells_per_node(768, 1024) == 7); // 110 x 147 = 16,170
    CHECK(default_cells_per_node(64, 64) == 1);    // small test maps: a node per cell
    WorldConfig cfg;
    CHECK(cfg.tile_size_m == 805);
    CHECK(cfg.width_tiles == 256);
}

TEST_CASE("catchment: 2, 3 and 4 map cells, counted in the nodes whose centres fall inside [I]") {
    const CargoRegistry cargo = no_cargo();
    const Economy eco(128, 128, kNodeMm, cargo, default_balance(), 2);
    CHECK(eco.map_cell_mm() == kCellMm);
    CHECK(eco.cells_mm(2) == kNodeMm);
    const MapPoint centre = eco.node_centre(20, 20);
    const MapPoint corner = {20 * kNodeMm, 20 * kNodeMm};
    // Small: 1 mile either way. 3 x 3 nodes from a node's centre, 2 x 2 from its corner.
    CHECK(catchment_nodes(eco, centre, StationSize::Small) == 9);
    CHECK(catchment_nodes(eco, corner, StationSize::Small) == 4);
    // Medium: 1.5 miles.
    CHECK(catchment_nodes(eco, centre, StationSize::Medium) == 9);
    CHECK(catchment_nodes(eco, corner, StationSize::Medium) == 16);
    // Large: 2 miles.
    CHECK(catchment_nodes(eco, centre, StationSize::Large) == 25);
    CHECK(catchment_nodes(eco, corner, StationSize::Large) == 16);
    // Near the edge, only what is on the map.
    CHECK(catchment_nodes(eco, eco.node_centre(0, 0), StationSize::Small) == 4);
}

TEST_CASE("an economy node takes its water and relief from the map cells it covers") {
    const CargoRegistry cargo = no_cargo();
    Economy eco(4, 4, kNodeMm, cargo, default_balance(), 2);
    Terrain t(8, 8, 805);
    // Node (0, 0): three of its four cells are water. Node (1, 0): one is.
    t.set_ground(0, 0, GroundType::Water);
    t.set_ground(1, 0, GroundType::Water);
    t.set_ground(0, 1, GroundType::Water);
    t.set_ground(2, 0, GroundType::Water);
    eco.set_terrain(t);
    CHECK(eco.water(0, 0));
    CHECK_FALSE(eco.water(1, 0));
    const Balance::Economy& b = default_balance().economy;
    CHECK(eco.conductance(0, 0) == b.water_conductance_permille);
    CHECK(eco.conductance(1, 0) == b.coast_conductance_permille); // some water: coast
    CHECK(eco.conductance(3, 3) == 1000);                           // dry and flat
    // A terrain grid that does not fit the nodes is refused.
    CHECK_THROWS(eco.set_terrain(Terrain(20, 20, 805)));
}
