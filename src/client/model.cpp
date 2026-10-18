#include "model.hpp"

#include "shading.hpp"

#include <SDL_opengl.h>
#include <cgltf.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <array>
#include <map>
#include <optional>

namespace railmaster::client::models {

namespace {

std::map<std::string, std::vector<Model>> g_roles;
std::map<std::string, GLuint> g_textures; // by file path, shared between models
// Colours set by material name in the manifest ("material_colors"), as
// screen colours: to fit a pack's palette to the game's.
std::map<std::string, std::array<float, 3>> g_material_colors;

GLuint upload(unsigned char* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // Low-poly art often colours faces from a palette texture: linear
    // sampling without mipmaps keeps each face its own colour at a distance.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, shading::white_texture());
    return tex;
}

// A texture file, read once.
GLuint texture_file(const std::string& path) {
    if (const auto it = g_textures.find(path); it != g_textures.end()) return it->second;
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!pixels) {
        std::fprintf(stderr, "models: cannot read texture %s\n", path.c_str());
        return g_textures[path] = 0;
    }
    const GLuint tex = upload(pixels, w, h);
    stbi_image_free(pixels);
    return g_textures[path] = tex;
}

// A material's base colour texture: a file beside the model, or embedded.
GLuint texture_of(const cgltf_image* image, const std::filesystem::path& dir) {
    if (!image) return 0;
    if (image->uri && std::string(image->uri).rfind("data:", 0) != 0) {
        std::string uri = image->uri;
        cgltf_decode_uri(uri.data());
        uri.resize(std::char_traits<char>::length(uri.c_str()));
        return texture_file((dir / uri).string());
    }
    if (image->buffer_view && image->buffer_view->buffer->data) {
        const auto* bytes = static_cast<const unsigned char*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        int w = 0, h = 0, n = 0;
        unsigned char* pixels = stbi_load_from_memory(bytes, static_cast<int>(image->buffer_view->size), &w, &h, &n, 4);
        if (!pixels) return 0;
        const GLuint tex = upload(pixels, w, h);
        stbi_image_free(pixels);
        return tex;
    }
    return 0;
}

// glTF colour factors and vertex colours are linear; the drawing works in
// screen (sRGB) colours, as textures are stored.
float to_srgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

// `texture`, when given, replaces every textured material's texture (a
// recoloured palette, say).
std::optional<Model> load(const std::filesystem::path& path, const std::string& texture = {}) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.string().c_str(), &data) != cgltf_result_success) {
        std::fprintf(stderr, "models: cannot read %s\n", path.string().c_str());
        return std::nullopt;
    }
    if (cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success) {
        std::fprintf(stderr, "models: cannot read the buffers of %s\n", path.string().c_str());
        cgltf_free(data);
        return std::nullopt;
    }
    Model m;
    m.min[0] = m.min[1] = m.min[2] = 1e30f;
    m.max[0] = m.max[1] = m.max[2] = -1e30f;
    // Textures first: an upload while compiling the list would only be recorded.
    std::vector<GLuint> textures(data->materials_count, 0);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& mat = data->materials[i];
        if (mat.has_pbr_metallic_roughness && mat.pbr_metallic_roughness.base_color_texture.texture) {
            textures[i] = texture.empty() ? texture_of(mat.pbr_metallic_roughness.base_color_texture.texture->image, path.parent_path())
                                          : texture_file(texture);
        }
    }
    m.list = glGenLists(1);
    glNewList(m.list, GL_COMPILE);
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh) continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *col = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_color && at.index == 0) col = at.data;
            }
            if (!pos) continue;
            float factor[4] = {1, 1, 1, 1};
            GLuint tex = 0;
            if (prim.material) {
                if (prim.material->has_pbr_metallic_roughness) {
                    for (int k = 0; k < 4; ++k) factor[k] = prim.material->pbr_metallic_roughness.base_color_factor[k];
                    for (int k = 0; k < 3; ++k) factor[k] = to_srgb(factor[k]);
                }
                if (prim.material->name) {
                    if (const auto it = g_material_colors.find(prim.material->name); it != g_material_colors.end()) {
                        for (std::size_t k = 0; k < 3; ++k) factor[k] = it->second[k];
                    }
                }
                tex = textures[static_cast<std::size_t>(prim.material - data->materials)];
            }
            if (tex != 0) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, tex);
            } else {
                glBindTexture(GL_TEXTURE_2D, shading::white_texture());
                glDisable(GL_TEXTURE_2D);
            }
            glColor4fv(factor);
            const cgltf_size count = prim.indices ? prim.indices->count : pos->count;
            glBegin(GL_TRIANGLES);
            for (cgltf_size i = 0; i < count; ++i) {
                const cgltf_size v = prim.indices ? cgltf_accessor_read_index(prim.indices, i) : i;
                float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0}, c[4] = {1, 1, 1, 1};
                cgltf_accessor_read_float(pos, v, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, n, 3);
                if (uv) cgltf_accessor_read_float(uv, v, t, 2);
                if (col) {
                    cgltf_accessor_read_float(col, v, c, 4);
                    glColor4f(to_srgb(c[0]) * factor[0], to_srgb(c[1]) * factor[1], to_srgb(c[2]) * factor[2], c[3] * factor[3]);
                }
                // The node's transform, then glTF (x, y up, z) to map (x, z, y).
                const float wx = world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12];
                const float wy = world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13];
                const float wz = world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14];
                float nx = world[0] * n[0] + world[4] * n[1] + world[8] * n[2];
                float ny = world[1] * n[0] + world[5] * n[1] + world[9] * n[2];
                float nz = world[2] * n[0] + world[6] * n[1] + world[10] * n[2];
                const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 0) {
                    nx /= nl;
                    ny /= nl;
                    nz /= nl;
                }
                const float mp[3] = {wx, wz, wy};
                for (int k = 0; k < 3; ++k) {
                    m.min[k] = std::min(m.min[k], mp[k]);
                    m.max[k] = std::max(m.max[k], mp[k]);
                }
                glNormal3f(nx, nz, ny);
                glTexCoord2f(t[0], t[1]);
                glVertex3f(mp[0], mp[1], mp[2]);
            }
            glEnd();
            m.triangles += count / 3;
        }
    }
    glBindTexture(GL_TEXTURE_2D, shading::white_texture());
    glDisable(GL_TEXTURE_2D);
    glEndList();
    cgltf_free(data);
    if (m.triangles == 0) {
        std::fprintf(stderr, "models: %s has no triangles\n", path.string().c_str());
        return std::nullopt;
    }
    return m;
}

// Models used as pieces of prefabs, read once each (per texture).
std::map<std::string, std::optional<Model>> g_pieces;

const Model* piece(const std::filesystem::path& path, const std::string& texture) {
    const std::string key = path.string() + "|" + texture;
    auto it = g_pieces.find(key);
    if (it == g_pieces.end()) it = g_pieces.emplace(key, load(path, texture)).first;
    return it->second ? &*it->second : nullptr;
}

// A prefab: pieces placed and turned (about the up axis, degrees) in map
// axes, compiled into one model. In the manifest:
//   "prefabs": { "cottage": { "texture": "pack/Textures/palette.png",
//                             "pieces": [ { "file": "pack/wall.glb", "at": [x, y, z], "turn": 90,
//                                           "scale": [1, 1, 1.5] }, ... ] } }
// (scale is optional, applied before the turn).
std::optional<Model> assemble(const std::filesystem::path& root, const std::string& name, const nlohmann::json& spec) {
    const std::string texture = spec.contains("texture") ? (root / spec["texture"].get<std::string>()).string() : std::string();
    struct Placed {
        const Model* model;
        float at[3];
        float turn;
        float scale[3];
        std::optional<Spinner> spin{};
    };
    std::vector<Placed> placed;
    for (const auto& p : spec.value("pieces", nlohmann::json::array())) {
        const Model* m = piece(root / p.value("file", ""), texture);
        if (!m) continue;
        Placed pl{m, {0, 0, 0}, p.value("turn", 0.0f), {1, 1, 1}};
        if (p.contains("at") && p["at"].is_array() && p["at"].size() == 3) {
            for (std::size_t k = 0; k < 3; ++k) pl.at[k] = p["at"][k].get<float>();
        }
        if (p.contains("scale") && p["scale"].is_array() && p["scale"].size() == 3) {
            for (std::size_t k = 0; k < 3; ++k) pl.scale[k] = p["scale"][k].get<float>();
        }
        if (p.contains("spin")) {
            // A turning part: {"axis": "x" | "y" | "z", "rpm": turns a minute}.
            const nlohmann::json& sp = p["spin"];
            Spinner s;
            s.part = m;
            std::copy(std::begin(pl.at), std::end(pl.at), s.at);
            s.turn = pl.turn;
            std::copy(std::begin(pl.scale), std::end(pl.scale), s.scale);
            const std::string axis = sp.value("axis", std::string("y"));
            s.axis = axis == "x" ? 0 : axis == "z" ? 2 : 1;
            s.rpm = sp.value("rpm", 6.0f);
            pl.spin = s;
        }
        placed.push_back(pl);
    }
    if (placed.empty()) {
        std::fprintf(stderr, "models: prefab %s has no pieces it could read\n", name.c_str());
        return std::nullopt;
    }
    Model m;
    m.min[0] = m.min[1] = m.min[2] = 1e30f;
    m.max[0] = m.max[1] = m.max[2] = -1e30f;
    m.list = glGenLists(1);
    glNewList(m.list, GL_COMPILE);
    for (const Placed& pl : placed) {
        if (pl.spin) {
            m.spinners.push_back(*pl.spin); // drawn each frame, but counted in the bounds
        } else {
        glPushMatrix();
        glTranslatef(pl.at[0], pl.at[1], pl.at[2]);
        glRotatef(pl.turn, 0, 0, 1);
        glScalef(pl.scale[0], pl.scale[1], pl.scale[2]);
        glCallList(pl.model->list);
        glPopMatrix();
        }
        // The piece's bounds, scaled, turned and moved.
        const float c = std::cos(pl.turn * 0.0174532925f), sn = std::sin(pl.turn * 0.0174532925f);
        for (int corner = 0; corner < 4; ++corner) {
            const float px = (corner & 1 ? pl.model->max[0] : pl.model->min[0]) * pl.scale[0];
            const float py = (corner & 2 ? pl.model->max[1] : pl.model->min[1]) * pl.scale[1];
            const float wx = pl.at[0] + px * c - py * sn, wy = pl.at[1] + px * sn + py * c;
            m.min[0] = std::min(m.min[0], wx);
            m.max[0] = std::max(m.max[0], wx);
            m.min[1] = std::min(m.min[1], wy);
            m.max[1] = std::max(m.max[1], wy);
        }
        m.min[2] = std::min(m.min[2], pl.at[2] + pl.model->min[2] * pl.scale[2]);
        m.max[2] = std::max(m.max[2], pl.at[2] + pl.model->max[2] * pl.scale[2]);
        m.triangles += pl.model->triangles;
    }
    glEndList();
    return m;
}

} // namespace

void init(const std::string& data_dir) {
    const std::filesystem::path root = std::filesystem::path(data_dir) / "models";
    std::ifstream in(root / "models.json");
    if (!in) return; // no art: everything drawn in code
    nlohmann::json manifest;
    try {
        in >> manifest;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "models: models.json: %s\n", e.what());
        return;
    }
    std::size_t loaded = 0;
    if (manifest.contains("material_colors") && manifest["material_colors"].is_object()) {
        for (const auto& [name, rgb] : manifest["material_colors"].items()) {
            if (rgb.is_array() && rgb.size() == 3) g_material_colors[name] = {rgb[0].get<float>(), rgb[1].get<float>(), rgb[2].get<float>()};
        }
    }
    // Prefabs, from the manifest and from prefabs.json beside it (which a
    // generator may write).
    std::map<std::string, Model> prefabs;
    const auto read_prefabs = [&](const nlohmann::json& doc) {
        if (!doc.contains("prefabs") || !doc["prefabs"].is_object()) return;
        for (const auto& [name, spec] : doc["prefabs"].items()) {
            if (auto m = assemble(root, name, spec)) prefabs[name] = *m;
        }
    };
    read_prefabs(manifest);
    if (std::ifstream more(root / "prefabs.json"); more) {
        try {
            nlohmann::json doc;
            more >> doc;
            read_prefabs(doc);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "models: prefabs.json: %s\n", e.what());
        }
    }
    if (!manifest.contains("roles") || !manifest["roles"].is_object()) return;
    const nlohmann::json& roles = manifest["roles"];
    for (const auto& [role, entries] : roles.items()) {
        for (const auto& entry : entries) {
            const std::string file = entry.is_string() ? entry.get<std::string>() : entry.value("file", "");
            if (file.empty()) continue;
            if (file.rfind("prefab:", 0) == 0) {
                if (const auto it = prefabs.find(file.substr(7)); it != prefabs.end()) {
                    g_roles[role].push_back(it->second);
                    ++loaded;
                } else {
                    std::fprintf(stderr, "models: no prefab %s\n", file.c_str());
                }
                continue;
            }
            if (auto m = load(root / file)) {
                g_roles[role].push_back(*m);
                ++loaded;
            }
        }
    }
    std::printf("models: %zu loaded for %zu roles\n", loaded, g_roles.size());
}

const Model* pick(const std::string& role, unsigned seed) {
    const auto it = g_roles.find(role);
    if (it == g_roles.end() || it->second.empty()) return nullptr;
    return &it->second[seed % it->second.size()];
}

bool has(const std::string& role) { return pick(role, 0) != nullptr; }

void draw(const Model& m, float x, float y, float z, float fx, float fy, float width, float depth) {
    const float mw = std::max(1e-4f, m.max[0] - m.min[0]), md = std::max(1e-4f, m.max[1] - m.min[1]);
    const float sx = width > 0 ? width / mw : depth / md, sy = depth > 0 ? depth / md : sx;
    // The model's front (map +y) turned to face (fx, fy).
    const float angle = std::atan2(fy, fx) * 57.2957795f - 90.0f;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(angle, 0, 0, 1);
    glScalef(sx, sy, sx);
    glTranslatef(-(m.min[0] + m.max[0]) / 2, -(m.min[1] + m.max[1]) / 2, -m.min[2]);
    glCallList(m.list);
    glPopMatrix();
}

void draw_to_height(const Model& m, float x, float y, float z, float fx, float fy, float height) {
    const float k = height / std::max(1e-4f, m.max[2] - m.min[2]);
    draw(m, x, y, z, fx, fy, (m.max[0] - m.min[0]) * k);
}

void draw_spinners(const Model& m, float x, float y, float z, float fx, float fy, float width, float seconds) {
    if (m.spinners.empty()) return;
    const float mw = std::max(1e-4f, m.max[0] - m.min[0]);
    const float sx = width / mw;
    const float angle = std::atan2(fy, fx) * 57.2957795f - 90.0f;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(angle, 0, 0, 1);
    glScalef(sx, sx, sx);
    glTranslatef(-(m.min[0] + m.max[0]) / 2, -(m.min[1] + m.max[1]) / 2, -m.min[2]);
    for (const Spinner& s : m.spinners) {
        const Model& p = *s.part;
        const float c[3] = {(p.min[0] + p.max[0]) / 2, (p.min[1] + p.max[1]) / 2, (p.min[2] + p.max[2]) / 2};
        glPushMatrix();
        glTranslatef(s.at[0], s.at[1], s.at[2]);
        glRotatef(s.turn, 0, 0, 1);
        glScalef(s.scale[0], s.scale[1], s.scale[2]);
        // Turned about its middle.
        glTranslatef(c[0], c[1], c[2]);
        glRotatef(std::fmod(seconds * s.rpm * 6.0f, 360.0f), s.axis == 0 ? 1.0f : 0.0f, s.axis == 1 ? 1.0f : 0.0f,
                  s.axis == 2 ? 1.0f : 0.0f);
        glTranslatef(-c[0], -c[1], -c[2]);
        glCallList(p.list);
        glPopMatrix();
    }
    glPopMatrix();
}

void draw_spinners_to_height(const Model& m, float x, float y, float z, float fx, float fy, float height, float seconds) {
    const float k = height / std::max(1e-4f, m.max[2] - m.min[2]);
    draw_spinners(m, x, y, z, fx, fy, (m.max[0] - m.min[0]) * k, seconds);
}

} // namespace railmaster::client::models
