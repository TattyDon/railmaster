#pragma once

#include "railmaster/sim/company.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/railway.hpp"
#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/territory.hpp"
#include "railmaster/sim/track_builder.hpp"

#include <optional>
#include <vector>

namespace railmaster::client {

// Top-down view of the map. World drawing happens in tile units after
// apply(); UI drawing happens in window pixels after apply_screen().
struct Camera {
    float pan_x = 0.0f; // tile at the window's left edge
    float pan_y = 0.0f; // tile at the window's top edge
    float zoom = 6.0f;  // pixels per tile
    float mm_per_tile = 1'000'000.0f;
    int width_px = 1280;
    int height_px = 800;

    sim::MapPoint to_map(float sx, float sy) const;
    void to_screen(sim::MapPoint p, float& sx, float& sy) const;
    float pixels_to_mm(float px) const { return px / zoom * mm_per_tile; }
    // Zoom by `factor`, keeping the map point under (sx, sy) fixed.
    void zoom_at(float sx, float sy, float factor);
    void apply() const;
    void apply_screen() const;
};

void draw_terrain(const sim::Terrain& t);
// `player` is the human's company, if any: its track keeps the structure
// colours, and rivals are drawn in theirs.
void draw_railway(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player);
// Each company's colour: the player's is pale; rivals get distinct hues.
void owner_rgb(sim::CompanyId owner, std::optional<sim::CompanyId> player, float& r, float& g, float& b);

// Industries and houses as small squares: raw producers brown, processors
// purple, consumers grey, houses cream. With `cargo` set, producers of it
// are ringed red and consumers green.
void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Camera& cam,
                std::optional<sim::CargoId> cargo);
// RT3-style cargo map: red where the cargo is cheap, green where it is dear.
void draw_price_overlay(const sim::Economy& eco, const sim::CargoType& cargo);
// Town names, in screen space.
// Town names with their star rating ("NORTHWICK **").
void draw_town_names(const sim::Economy& eco, const Camera& cam, const sim::Balance::Towns& towns);
// Territory borders along map-cell edges (closed borders in red), in map
// coordinates; then, in screen coordinates, each territory's name at its
// centre with its access price (none if open, "YOURS" with access).
void draw_territory_borders(const sim::TerritoryMap& map);
void draw_territory_names(const sim::TerritoryMap& map, const Camera& cam, const sim::Company* company);

// A planned run of track, coloured by structure; red if it cannot be built.
void draw_plan(sim::MapPoint start, const sim::TrackPlan& plan, const Camera& cam);
void draw_line(sim::MapPoint a, sim::MapPoint b, const Camera& cam, float r, float g, float bl, float alpha);
// A square marker `half_px` pixels from centre to edge, whatever the zoom.
void draw_marker(sim::MapPoint p, const Camera& cam, float half_px, float r, float g, float b);

} // namespace railmaster::client
