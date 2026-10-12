#pragma once

#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/track.hpp"

#include <array>

namespace railmaster::client {

// The view of the map. World drawing happens in map tiles (x east, y south)
// after apply(), with heights in tiles too (see z_of); UI drawing happens in
// window pixels after apply_screen().
//
// In 3D (the default) the camera orbits a point on the ground: `yaw` turns
// it round, `pitch` tilts it (low looks across the land, high looks down)
// and `distance` zooms (rt3-clone-spec §15.6: rotate, tilt, zoom). The
// mouse picks the map point its ray meets on the terrain. In 2D it is the
// old top-down view, kept as a fallback (V switches).
struct Camera {
    float pan_x = 0.0f; // 2D: tile at the window's left edge
    float pan_y = 0.0f; // 2D: tile at the window's top edge
    float zoom = 6.0f;  // pixels per tile (in 3D: at the point looked at)
    float mm_per_tile = 1'000'000.0f;
    int width_px = 1280;
    int height_px = 800;

    bool three_d = true;
    float target_x = 128.0f, target_y = 128.0f; // tiles: the point on the ground looked at
    float distance = 28.0f;                      // tiles from it
    float yaw = 0.0f;                            // radians; 0 looks north
    float pitch = 0.85f;                         // radians above the horizon
    float fov_y = 0.75f;                         // radians
    // Heights are drawn this many times their true scale, so hills read
    // at a map's distance.
    float height_exaggeration = 4.0f;
    const sim::Terrain* terrain = nullptr;

    // Height of the ground at a point, in tiles (0 in 2D or with no terrain).
    float ground_z(float tx, float ty) const;
    // A height in millimetres (rails, nodes) in tiles.
    float z_of(std::int64_t z_mm) const;

    sim::MapPoint to_map(float sx, float sy) const;
    // Where a point on the ground (or at `z_mm` when given) appears on screen;
    // false (and far off screen) when it is behind the camera.
    bool to_screen(sim::MapPoint p, float& sx, float& sy) const;
    bool to_screen_at(sim::MapPoint p, std::int64_t z_mm, float& sx, float& sy) const;
    float pixels_to_mm(float px) const { return px / zoom * mm_per_tile; }
    // Zoom by `factor`, keeping the map point under (sx, sy) fixed (2D) or
    // moving towards the point looked at (3D).
    void zoom_at(float sx, float sy, float factor);
    // Look at a map point.
    void center_on(sim::MapPoint p);
    // Move the view by (dx, dy) screen-ish units: right and down on screen.
    void pan(float dx, float dy);
    void orbit(float dyaw, float dpitch);
    void apply();
    void apply_screen() const;

private:
    std::array<float, 16> view_proj_{}; // column-major, map tiles to clip space
    void project(float x, float y, float z, float& sx, float& sy, float& w) const;
};

} // namespace railmaster::client
