#pragma once

#include "camera.hpp"

#include "railmaster/sim/company.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/railway.hpp"
#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/territory.hpp"
#include "railmaster/sim/track_builder.hpp"
#include "railmaster/sim/world.hpp"

#include <optional>
#include <vector>

namespace railmaster::client {


// The ground: in 3D a lit mesh with trees, rebuilt only when the terrain
// changes; in 2D flat squares.
void draw_terrain(const sim::Terrain& t, const Camera& cam);
// `player` is the human's company, if any: its track keeps the structure
// colours, and rivals are drawn in theirs.
void draw_railway(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player);
// The analysis overlays (rt3-clone-spec §15.3), drawn over the railway.
enum class MapOverlay { None, Holdings, Profit, Grade, Traffic, Cargo };
// Holdings: every company's track, stations and industries in its colour.
// Profit: trains and stations green, yellow or red by sim::rate. Grade:
// track green on the level to red at the grade limit. Traffic: track by
// last month's trains, brighter the busier, red where single track held
// trains waiting at meets (it needs a second track).
void draw_analysis_overlay(const sim::World& w, MapOverlay mode, const Camera& cam);
// Each company's colour: the player's is pale; rivals get distinct hues.
void owner_rgb(sim::CompanyId owner, std::optional<sim::CompanyId> player, float& r, float& g, float& b);

// Industries and houses: in 3D buildings (houses cream with red roofs, raw
// producers brown, processors purple, consumers grey), in 2D squares. With `cargo` set, producers of it
// are ringed red and consumers green.
void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Camera& cam,
                std::optional<sim::CargoId> cargo);
// RT3-style cargo map: red where the cargo is cheap, green where it is dear.
void draw_price_overlay(const sim::Economy& eco, const sim::CargoType& cargo, const Camera& cam);
// Town names, in screen space.
// Town names with their star rating ("NORTHWICK **").
void draw_town_names(const sim::Economy& eco, const Camera& cam, const sim::Balance::Towns& towns);
// Territory borders along map-cell edges (closed borders in red), in map
// coordinates; then, in screen coordinates, each territory's name at its
// centre with its access price (none if open, "YOURS" with access).
void draw_territory_borders(const sim::TerritoryMap& map, const Camera& cam);
void draw_territory_names(const sim::TerritoryMap& map, const Camera& cam, const sim::Company* company);

// A planned run of track, coloured by structure; red if it cannot be built.
void draw_plan(sim::MapPoint start, const sim::TrackPlan& plan, const Camera& cam);
void draw_line(sim::MapPoint a, sim::MapPoint b, const Camera& cam, float r, float g, float bl, float alpha);
// A square marker `half_px` pixels from centre to edge, whatever the zoom.
void draw_marker(sim::MapPoint p, const Camera& cam, float half_px, float r, float g, float b);

} // namespace railmaster::client
