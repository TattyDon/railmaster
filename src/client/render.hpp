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
void draw_railway(const sim::World& w, const Camera& cam);
// Smoke and steam from the chimneys drawn this frame (steam engines on the
// move, works and power stations); `seconds` animates it. Call it after the
// railway and sites, last in the world pass.
void draw_smoke(const Camera& cam, float seconds);
// Forget the chimneys seen so far this frame (after the shadow pass, which
// draws the same things).
void clear_smoke();
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
// are ringed red and consumers green. In 3D, towns are laid out along
// streets (paved as the years pass), clear of the railway's track and
// stations.
void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const sim::Railway& rw,
                std::int32_t year, const Camera& cam, std::optional<sim::CargoId> cargo);
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

// The radar (rt3-clone-spec §15.1): the whole map small, in a square of
// `size` pixels at (x0, y0) on screen, with towns, your stations and where
// the view is looking. radar_point gives the map point under a click on it.
void draw_radar(const sim::World& w, const Camera& cam, float x0, float y0, float size);
std::optional<sim::MapPoint> radar_point(const sim::World& w, const Camera& cam, float x0, float y0, float size, float sx, float sy);

} // namespace railmaster::client
