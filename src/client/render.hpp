#pragma once

#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/railway.hpp"
#include "railmaster/sim/terrain.hpp"
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
void draw_railway(const sim::Railway& rw, const Camera& cam);

// Industries and houses as small squares: raw producers brown, processors
// purple, consumers grey, houses cream. With `cargo` set, producers of it
// are ringed red and consumers green.
void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Camera& cam,
                std::optional<sim::CargoId> cargo);
// RT3-style cargo map: red where the cargo is cheap, green where it is dear.
void draw_price_overlay(const sim::Economy& eco, const sim::CargoType& cargo);
// Town names, in screen space.
void draw_town_names(const sim::Economy& eco, const Camera& cam);

// A planned run of track, coloured by structure; red if it cannot be built.
void draw_plan(sim::MapPoint start, const sim::TrackPlan& plan, const Camera& cam);
void draw_line(sim::MapPoint a, sim::MapPoint b, const Camera& cam, float r, float g, float bl, float alpha);
// A square marker `half_px` pixels from centre to edge, whatever the zoom.
void draw_marker(sim::MapPoint p, const Camera& cam, float half_px, float r, float g, float b);

} // namespace railmaster::client
