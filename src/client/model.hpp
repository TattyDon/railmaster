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

struct Model {
    unsigned list = 0;           // display list: the model in map axes, in its own units
    float min[3] = {0, 0, 0};    // bounds, map axes
    float max[3] = {0, 0, 0};
    std::size_t triangles = 0;
};

// After the GL context exists: read the manifest under `data_dir` and load
// its models. Problems are reported on stderr and leave the role empty.
void init(const std::string& data_dir);

// One of the models for a role, chosen by `seed`; nullptr if it has none.
const Model* pick(const std::string& role, unsigned seed);
bool has(const std::string& role);

// Draw a model standing at (x, y, z) in tiles, turned so its front faces
// (fx, fy), scaled so its footprint is `width` tiles across (x) and, when
// `depth` > 0, `depth` tiles deep (y); height scales with the width. Not
// inside glBegin/glEnd.
void draw(const Model& m, float x, float y, float z, float fx, float fy, float width, float depth = 0.0f);

} // namespace railmaster::client::models
