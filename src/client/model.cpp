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
#include <map>
#include <optional>

namespace railmaster::client::models {

namespace {

std::map<std::string, std::vector<Model>> g_roles;
std::map<std::string, GLuint> g_textures; // by file path, shared between models

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

// A material's base colour texture: a file beside the model, or embedded.
GLuint texture_of(const cgltf_image* image, const std::filesystem::path& dir) {
    if (!image) return 0;
    if (image->uri && std::string(image->uri).rfind("data:", 0) != 0) {
        std::string uri = image->uri;
        cgltf_decode_uri(uri.data());
        uri.resize(std::char_traits<char>::length(uri.c_str()));
        const std::string path = (dir / uri).string();
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

std::optional<Model> load(const std::filesystem::path& path) {
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
            textures[i] = texture_of(mat.pbr_metallic_roughness.base_color_texture.texture->image, path.parent_path());
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
                    glColor4f(c[0] * factor[0], c[1] * factor[1], c[2] * factor[2], c[3] * factor[3]);
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
    if (!manifest.contains("roles") || !manifest["roles"].is_object()) return;
    const nlohmann::json& roles = manifest["roles"];
    for (const auto& [role, entries] : roles.items()) {
        for (const auto& entry : entries) {
            const std::string file = entry.is_string() ? entry.get<std::string>() : entry.value("file", "");
            if (file.empty()) continue;
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
    const float sx = width / mw, sy = depth > 0 ? depth / md : sx;
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

} // namespace railmaster::client::models
