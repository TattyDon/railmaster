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
// Seconds of real time, for what moves while paused too (the water).
void set_clock(float seconds);
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
