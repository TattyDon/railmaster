#pragma once

#include <string>
#include <vector>

namespace railmaster::client::models {

// Art from glTF files (rt3-clone-spec §15.6 views; docs/ROADMAP.md M4):
// data/models/models.json names, for each role the client draws (a town
// house, a tree, ...), the .glb or .gltf files that can play it. Where a
// role has models they replace the shapes drawn in code; where it has none
// (or the files are missing) the code keeps drawing its own. Models are
// read once, their textures uploaded, and each compiled to a display list.
//
// glTF is y-up and right-handed; models are turned into map axes (x east,
// y south, z up) as they load, so a model's front (glTF +z) faces map +y.

struct Model;
// A part of a prefab that turns (windmill sails, a water wheel): left out
// of the prefab's display list and drawn each frame by draw_spinners,
// turning about its own middle.
struct Spinner {
    const Model* part = nullptr;
    float at[3] = {0, 0, 0};
    float turn = 0;
    float scale[3] = {1, 1, 1};
    int axis = 1;    // 0 x, 1 y, 2 z: the piece's own axis it turns about
    float rpm = 6;   // turns a minute
};

struct Model {
    unsigned list = 0;           // display list: the model in map axes, in its own units
    float min[3] = {0, 0, 0};    // bounds, map axes
    float max[3] = {0, 0, 0};
    std::size_t triangles = 0;
    std::vector<Spinner> spinners{};
    // Where its roof starts (its eaves), in its own units above min[2]; 0:
    // no roof found. The lowest sloping upward face above 45% of its height.
    float eave = 0;
    // The lowest sloping upward face at all (absolute, own units), for a
    // prefab finding its eaves among its pieces; above max[2]: none.
    float roof_low = 1e30f;
};

// After the GL context exists: read the manifest under `data_dir` and load
// its models. Problems are reported on stderr and leave the role empty.
void init(const std::string& data_dir);

// One of the models for a role, chosen by `seed`; nullptr if it has none.
const Model* pick(const std::string& role, unsigned seed);
bool has(const std::string& role);

// Draw a model standing at (x, y, z) in tiles, turned so its front faces
// (fx, fy), scaled so its footprint is `width` tiles across (x) and, when
// `depth` > 0, `depth` tiles deep (y); height scales with the width. With
// `width` 0 it keeps its proportions, fitted to `depth`. Not inside
// glBegin/glEnd.
void draw(const Model& m, float x, float y, float z, float fx, float fy, float width, float depth = 0.0f);
// The same, scaled evenly so it stands `height` tiles tall.
void draw_to_height(const Model& m, float x, float y, float z, float fx, float fy, float height);
// A model's turning parts, placed as draw() or draw_to_height() places the
// rest, turned for `seconds` of running. Not inside glBegin/glEnd.
void draw_spinners(const Model& m, float x, float y, float z, float fx, float fy, float width, float seconds);
void draw_spinners_to_height(const Model& m, float x, float y, float z, float fx, float fy, float height, float seconds);

} // namespace railmaster::client::models
