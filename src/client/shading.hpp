#pragma once

#include "camera.hpp"

namespace railmaster::client::shading {

// The shader pipeline for the 3D view: per-pixel sunlight with a sky and
// ground ambient, shadows from a shadow map cast by the sun, the distance
// haze, and a gradient sky. It runs on the OpenGL 2.1 context with GLSL
// 1.20, so the drawing code keeps sending vertices, normals, colours and
// texture coordinates as before; functions past OpenGL 1.1 are loaded at
// run time. Where shaders or framebuffers are missing (or with
// --no-shaders) the client keeps the fixed-function lighting.

// After the GL context exists: load functions, compile the programs, make
// the shadow map. Returns whether the pipeline is in use.
bool init(bool wanted);
bool active();

// The 1x1 white texture bound when nothing is textured, so the shader can
// always multiply by a texture.
unsigned white_texture();

// Shadows: between these, draw what casts shadows (depth only). The light
// looks down along the sun at the camera's target, over an area that grows
// as the camera pulls back. end_shadow_pass restores the camera.
void begin_shadow_pass(Camera& cam);
void end_shadow_pass(Camera& cam);

// The time of day, 0 to 1 round the clock: 0 day, 0.25 dusk, 0.5 night,
// 0.75 dawn, easing between. Sets the sun (or moon), its colour, the
// ambient light and the sky, for the shaders and the fixed-function path.
void set_time_of_day(float phase);
// The season, each 0 to 1: snow lying on open ground and leaves in winter,
// leaves turning in autumn. Only green surfaces change.
void set_season(float snow, float autumn);
// Around drawing trees, so their leaves turn in autumn (grass only fades).
void set_foliage(bool on);
// Ambient occlusion: a luminance texture over the map (1 open sky, darker
// where trees and buildings stand round), `inv_w` and `inv_h` one over the
// map's size in tiles. The ground takes it in full (set_ground around the
// terrain), the trees' leaves in part (set_foliage).
void set_occlusion(unsigned texture, float inv_w, float inv_h);
// What stands round the town building about to be drawn, as 12 floats:
// across + (top z, gap), across - (top z, gap), along + (top z, gap),
// along - (top z, gap), then its axis along (x, y), the z of its ground
// and of its eaves.
// A gap of 0: nothing near on that side. nullptr: not a town building.
// Recorded in display lists, so it may be set while compiling one.
void set_neighbours(const float* near);
void set_ground(bool on);
// The land's height (tiles) at each map corner, as a 16-bit luminance
// texture `corners_w` x `corners_h` spanning z_min to z_min + z_range: the
// occlusion then darkens the foot of buildings, trees and trains too,
// fading out over their first few feet above the ground.
void set_heights(unsigned texture, int corners_w, int corners_h, float z_min, float z_range);
// Seconds of real time, for what moves while paused too (the water).
void set_clock(float seconds);
// Clouds' shadows drifting over the land.
void set_clouds(bool on);
// The cloud cover the shader draws at `seconds` of the clock, 0.22 to 0.5;
// it rains (or snows) when it is thickest.
float cloud_cover(float seconds);
// How hard it rains or snows, 0 to 1: the sunlight dulls.
void set_rain(float amount);
// How dark it is: 0 in daylight, 1 at night (lights come on).
float night();
// The light for fixed-function drawing: towards the sun, its colour, ambient.
void light_params(float* towards_sun, float* diffuse, float* ambient);

// The sky, behind everything: deep blue overhead to haze at the horizon.
// Leaves the camera's world matrices in place.
void draw_sky(Camera& cam);
// The colour far land fades to (the sky at the horizon).
void horizon_rgb(float& r, float& g, float& b);

// The world pass: the program on, its uniforms set; set_lit switches
// between lit solids and unlit overlays (begin_lit/end_lit in render.cpp).
void begin_world(const Camera& cam);
void set_lit(bool lit);
void end_world();

} // namespace railmaster::client::shading
