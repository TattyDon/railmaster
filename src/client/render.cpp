#include "render.hpp"

#include "font.hpp"
#include "model.hpp"
#include "shading.hpp"
#include "tools.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>
#include <unordered_set>

namespace railmaster::client {

namespace {

// Overlays and track sit this far above the ground (tiles), so they show
// over it without flickering.
constexpr float kLift = 0.04f;

float tiles(std::int64_t mm, const Camera& cam) { return static_cast<float>(mm) / cam.mm_per_tile; }

// Chimneys seen this frame (tiles): draw_smoke puffs from them, then forgets them.
struct SmokeSource {
    float x, y, z, size;
    bool steam; // white steam rather than grey smoke
    float seed;
};
std::vector<SmokeSource> g_smoke;
void add_smoke(float x, float y, float z, float size, bool steam = false) {
    g_smoke.push_back({x, y, z, size, steam, std::fmod(std::abs(x * 12.9898f + y * 78.233f), 1.0f)});
}

// A point on the ground (just above it in 3D).
void vertex(sim::MapPoint p, const Camera& cam) {
    const float x = tiles(p.x_mm, cam), y = tiles(p.y_mm, cam);
    glVertex3f(x, y, cam.three_d ? cam.ground_z(x, y) + kLift : 0.0f);
}

// A point at a given height (rails, nodes).
void vertex_at(sim::MapPoint p, std::int64_t z_mm, const Camera& cam, float lift = kLift) {
    glVertex3f(tiles(p.x_mm, cam), tiles(p.y_mm, cam), cam.three_d ? cam.z_of(z_mm) + lift : 0.0f);
}

// A tile-coordinate point on the ground.
void ground_vertex(float x, float y, const Camera& cam) { glVertex3f(x, y, cam.three_d ? cam.ground_z(x, y) + kLift : 0.0f); }

// A square lying on the ground, `half_tiles` from the point each way.
void square(sim::MapPoint p, const Camera& cam, float half_tiles) {
    const float x = tiles(p.x_mm, cam), y = tiles(p.y_mm, cam);
    ground_vertex(x - half_tiles, y - half_tiles, cam);
    ground_vertex(x + half_tiles, y - half_tiles, cam);
    ground_vertex(x + half_tiles, y + half_tiles, cam);
    ground_vertex(x - half_tiles, y + half_tiles, cam);
}

void ground_quad(float x0, float y0, float x1, float y1, const Camera& cam) {
    ground_vertex(x0, y0, cam);
    ground_vertex(x1, y0, cam);
    ground_vertex(x1, y1, cam);
    ground_vertex(x0, y1, cam);
}

// Lit drawing, for solid things: light from high in the north-west.
void begin_lit() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);
    const GLfloat dir[] = {-0.45f, -0.55f, 0.75f, 0.0f}; // in map axes (x east, y south, z up)
    const GLfloat ambient[] = {0.42f, 0.42f, 0.45f, 1.0f};
    const GLfloat diffuse[] = {0.70f, 0.68f, 0.62f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, dir);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    shading::set_lit(true);
}
void end_lit() {
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    shading::set_lit(false);
}

// A box on the ground (or at z), `hl` long along the direction (dx, dy),
// `hw` wide across it, `h` tall: walls in the colour given, the top `top`.
void box(float x, float y, float z, float dx, float dy, float hl, float hw, float h, const float* side, const float* top) {
    const float ax = dx * hl, ay = dy * hl;   // along
    const float cx = -dy * hw, cy = dx * hw;  // across
    const float px[4] = {x - ax - cx, x + ax - cx, x + ax + cx, x - ax + cx};
    const float py[4] = {y - ay - cy, y + ay - cy, y + ay + cy, y - ay + cy};
    glColor3fv(side);
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        // Outward normal of the wall from corner i to j.
        const float ex = px[j] - px[i], ey = py[j] - py[i];
        glNormal3f(ey, -ex, 0.0f);
        glVertex3f(px[i], py[i], z);
        glVertex3f(px[j], py[j], z);
        glVertex3f(px[j], py[j], z + h);
        glVertex3f(px[i], py[i], z + h);
    }
    glColor3fv(top);
    glNormal3f(0, 0, 1);
    for (int i = 0; i < 4; ++i) glVertex3f(px[i], py[i], z + h);
}

void track_colour(sim::TrackKind kind, float alpha) {
    switch (kind) {
    case sim::TrackKind::Ground: glColor4f(0.35f, 0.25f, 0.15f, alpha); break;
    case sim::TrackKind::Bridge: glColor4f(0.85f, 0.85f, 0.85f, alpha); break;
    case sim::TrackKind::Tunnel: glColor4f(0.10f, 0.08f, 0.05f, alpha); break;
    }
}

void shade(const sim::Terrain& t, int tx, int ty, int max_h) {
    const float h = static_cast<float>(t.corner_height(tx, ty)) / static_cast<float>(max_h);
    switch (t.ground(tx, ty)) {
    case sim::GroundType::Water: glColor3f(0.15f, 0.30f, 0.55f); break;
    default: glColor3f(0.25f + 0.45f * h, 0.45f + 0.35f * h, 0.20f + 0.25f * h); break;
    }
}

// A small deterministic hash, for scattering trees and varying colours.
std::uint32_t hash(std::uint32_t a, std::uint32_t b, std::uint32_t c = 0) {
    std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

float unit(std::uint32_t h) { return static_cast<float>(h & 0xFFFF) / 65535.0f; }

void ground_rgb(sim::GroundType g, float vary, float& r, float& gr, float& b) {
    r = 0.36f;
    gr = 0.56f;
    b = 0.24f;
    switch (g) {
    case sim::GroundType::Grass: r = 0.36f; gr = 0.56f; b = 0.24f; break;
    case sim::GroundType::Farmland: r = 0.64f; gr = 0.60f; b = 0.33f; break;
    case sim::GroundType::Forest: r = 0.22f; gr = 0.42f; b = 0.18f; break;
    case sim::GroundType::Desert: r = 0.82f; gr = 0.73f; b = 0.50f; break;
    case sim::GroundType::Rock: r = 0.52f; gr = 0.50f; b = 0.46f; break;
    case sim::GroundType::Snow: r = 0.93f; gr = 0.94f; b = 0.96f; break;
    case sim::GroundType::Water: r = 0.20f; gr = 0.40f; b = 0.66f; break;
    }
    const float k = 0.92f + 0.16f * vary;
    r *= k;
    gr *= k;
    b *= k;
}

// Value noise that wraps at the texture's edge: `cells` across, `rows` down.
float wrap_noise(float u, float v, int cells, int rows, std::uint32_t seed) {
    const float fx = u * static_cast<float>(cells), fy = v * static_cast<float>(rows);
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const auto at = [&](int cx, int cy) {
        return unit(hash(static_cast<std::uint32_t>((cx % cells + cells) % cells), static_cast<std::uint32_t>((cy % rows + rows) % rows), seed));
    };
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

// A tiling texture made in code: `texel` gives each texel's RGB (0 to 1).
// Mipmaps are box-filtered here, so far ground fades to its plain colour
// instead of shimmering.
template <typename Texel> GLuint make_texture(Texel texel) {
    constexpr int kSize = 256;
    std::vector<unsigned char> level(static_cast<std::size_t>(kSize) * kSize * 3);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            float rgb[3];
            texel(x, y, static_cast<float>(x) / kSize, static_cast<float>(y) / kSize, rgb);
            for (int c = 0; c < 3; ++c) {
                level[(static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(c)] =
                    static_cast<unsigned char>(std::clamp(rgb[c] * 255.0f, 0.0f, 255.0f));
            }
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int size = kSize;
    for (int lod = 0;; ++lod) {
        glTexImage2D(GL_TEXTURE_2D, lod, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, level.data());
        if (size == 1) break;
        const int half = size / 2;
        std::vector<unsigned char> next(static_cast<std::size_t>(half) * static_cast<std::size_t>(half) * 3);
        const auto px = [&](int sx, int sy, int c) {
            return static_cast<int>(level[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(size) + static_cast<std::size_t>(sx)) * 3 + static_cast<std::size_t>(c)]);
        };
        for (int y = 0; y < half; ++y)
            for (int x = 0; x < half; ++x)
                for (int c = 0; c < 3; ++c) {
                    const int sum = px(2 * x, 2 * y, c) + px(2 * x + 1, 2 * y, c) + px(2 * x, 2 * y + 1, c) + px(2 * x + 1, 2 * y + 1, c);
                    next[(static_cast<std::size_t>(y) * static_cast<std::size_t>(half) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(c)] =
                        static_cast<unsigned char>(sum / 4);
                }
        level = std::move(next);
        size = half;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // Sharp ground towards the horizon, where the card supports it.
    if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
        constexpr GLenum kMaxAnisotropy = 0x84FE, kMaxAnisotropyLimit = 0x84FF;
        GLfloat limit = 1.0f;
        glGetFloatv(kMaxAnisotropyLimit, &limit);
        glTexParameterf(GL_TEXTURE_2D, kMaxAnisotropy, std::min(8.0f, limit));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// The ground's detail: grey, about white, with soft blotches and fine
// speckle like grass and earth, so lit colours multiplied by it read as
// ground close up.
GLuint ground_texture() {
    static const GLuint tex = make_texture([](int x, int y, float u, float v, float* rgb) {
        const float blotch = wrap_noise(u, v, 4, 4, 31) * 0.5f + wrap_noise(u, v, 9, 9, 32) * 0.3f + wrap_noise(u, v, 23, 23, 33) * 0.2f;
        const float speck = unit(hash(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 34));
        float k = 0.78f + 0.2f * blotch + (speck - 0.5f) * 0.12f;
        if (speck > 0.94f) k += 0.08f; // light blades
        // A touch warmer in the dark patches, like earth showing through.
        rgb[0] = k + (1 - blotch) * 0.025f;
        rgb[1] = k;
        rgb[2] = k * 0.98f - (1 - blotch) * 0.025f;
    });
    return tex;
}

// Water's: long soft ripples with glints.
GLuint water_texture() {
    static const GLuint tex = make_texture([](int x, int y, float u, float v, float* rgb) {
        const float ripple = wrap_noise(u, v, 6, 24, 41) * 0.6f + wrap_noise(u, v, 13, 48, 42) * 0.4f;
        float k = 0.8f + 0.25f * ripple;
        if (ripple > 0.78f && unit(hash(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 43)) < 0.5f) k += 0.12f;
        rgb[0] = rgb[1] = rgb[2] = k;
    });
    return tex;
}

// How many tiles one copy of the ground texture covers.
constexpr float kGroundTextureTiles = 2.0f;

// The 3D ground as a display list, rebuilt when the terrain (or the height
// scale) changes.
struct TerrainMesh {
    GLuint list = 0;
    std::uint64_t key = 0;
};

std::uint64_t terrain_key(const sim::Terrain& t, const Camera& cam) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(static_cast<std::uint64_t>(t.width()));
    mix(static_cast<std::uint64_t>(t.height()));
    mix(static_cast<std::uint64_t>(cam.height_exaggeration * 1000));
    for (int y = 0; y <= t.height(); ++y)
        for (int x = 0; x <= t.width(); ++x) mix(static_cast<std::uint64_t>(t.corner_height(x, y)));
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x) mix(static_cast<std::uint64_t>(t.ground(x, y)));
    return h;
}

void build_terrain_mesh(const sim::Terrain& t, const Camera& cam) {
    const int w = t.width(), hgt = t.height();
    const float scale = cam.height_exaggeration / static_cast<float>(t.tile_size_m());
    const auto z = [&](int x, int y) {
        return static_cast<float>(t.corner_height(std::clamp(x, 0, w), std::clamp(y, 0, hgt))) * scale;
    };
    const auto normal = [&](int x, int y) {
        glNormal3f(-(z(x + 1, y) - z(x - 1, y)) / 2, -(z(x, y + 1) - z(x, y - 1)) / 2, 1.0f);
    };
    // Each tile's colour (its ground, a little varied, rockier where steep),
    // then blended at the corners so the land shades smoothly.
    std::vector<float> tile_rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(hgt) * 3);
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const sim::GroundType g = t.ground(tx, ty);
            float r, gr, b;
            ground_rgb(g, unit(hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty))) * 0.4f + 0.3f, r, gr, b);
            const float slope = std::max(std::fabs(z(tx + 1, ty) - z(tx, ty)), std::fabs(z(tx, ty + 1) - z(tx, ty)));
            const float rocky = g == sim::GroundType::Water ? 0.0f : std::clamp((slope - 0.12f) * 2.5f, 0.0f, 0.75f);
            float* c = &tile_rgb[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(w) + static_cast<std::size_t>(tx)) * 3];
            c[0] = r + (0.52f - r) * rocky;
            c[1] = gr + (0.49f - gr) * rocky;
            c[2] = b + (0.45f - b) * rocky;
        }
    }
    const auto corner_colour = [&](int cx, int cy) {
        float sum[3] = {0, 0, 0};
        int n = 0;
        for (int dy = -1; dy <= 0; ++dy)
            for (int dx = -1; dx <= 0; ++dx) {
                const int tx = cx + dx, ty = cy + dy;
                if (tx < 0 || ty < 0 || tx >= w || ty >= hgt || t.ground(tx, ty) == sim::GroundType::Water) continue;
                const float* c = &tile_rgb[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(w) + static_cast<std::size_t>(tx)) * 3];
                for (int k = 0; k < 3; ++k) sum[k] += c[k];
                ++n;
            }
        // A little brighter than the tile colours: the texture averages darker.
        constexpr float kBright = 1.12f;
        if (n == 0) glColor3f(0.45f * kBright, 0.42f * kBright, 0.3f * kBright);
        else glColor3f(sum[0] * kBright / static_cast<float>(n), sum[1] * kBright / static_cast<float>(n), sum[2] * kBright / static_cast<float>(n));
    };
    // Land, textured: the texture brightens and darkens the lit colours.
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, ground_texture());
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    const auto land_corner = [&](int cx, int cy) {
        corner_colour(cx, cy);
        normal(cx, cy);
        glTexCoord2f(static_cast<float>(cx) / kGroundTextureTiles, static_cast<float>(cy) / kGroundTextureTiles);
        glVertex3f(static_cast<float>(cx), static_cast<float>(cy), z(cx, cy));
    };
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            if (t.ground(tx, ty) == sim::GroundType::Water) continue;
            land_corner(tx, ty);
            land_corner(tx + 1, ty);
            land_corner(tx + 1, ty + 1);
            land_corner(tx, ty + 1);
        }
    }
    glEnd();
    // Water lies flat, at its lowest corner.
    glBindTexture(GL_TEXTURE_2D, water_texture());
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            if (t.ground(tx, ty) != sim::GroundType::Water) continue;
            const auto fx = static_cast<float>(tx), fy = static_cast<float>(ty);
            const float wz = std::min({z(tx, ty), z(tx + 1, ty), z(tx, ty + 1), z(tx + 1, ty + 1)});
            const float* c = &tile_rgb[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(w) + static_cast<std::size_t>(tx)) * 3];
            glColor3f(c[0], c[1], c[2]);
            const float u = fx / kGroundTextureTiles, v = fy / kGroundTextureTiles, step = 1 / kGroundTextureTiles;
            glTexCoord2f(u, v);
            glVertex3f(fx, fy, wz);
            glTexCoord2f(u + step, v);
            glVertex3f(fx + 1, fy, wz);
            glTexCoord2f(u + step, v + step);
            glVertex3f(fx + 1, fy + 1, wz);
            glTexCoord2f(u, v + step);
            glVertex3f(fx, fy + 1, wz);
        }
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, shading::white_texture());
    glDisable(GL_TEXTURE_2D);
    // A skirt round the edge of the map, down to below sea level.
    glBegin(GL_QUADS);
    glColor3f(0.35f, 0.3f, 0.24f);
    const float floor_z = -2.0f;
    for (int x = 0; x < w; ++x) {
        const auto fx = static_cast<float>(x);
        glNormal3f(0, -1, 0);
        glVertex3f(fx, 0, z(x, 0));
        glVertex3f(fx + 1, 0, z(x + 1, 0));
        glVertex3f(fx + 1, 0, floor_z);
        glVertex3f(fx, 0, floor_z);
        glNormal3f(0, 1, 0);
        glVertex3f(fx, static_cast<float>(hgt), z(x, hgt));
        glVertex3f(fx + 1, static_cast<float>(hgt), z(x + 1, hgt));
        glVertex3f(fx + 1, static_cast<float>(hgt), floor_z);
        glVertex3f(fx, static_cast<float>(hgt), floor_z);
    }
    for (int y = 0; y < hgt; ++y) {
        const auto fy = static_cast<float>(y);
        glNormal3f(-1, 0, 0);
        glVertex3f(0, fy, z(0, y));
        glVertex3f(0, fy + 1, z(0, y + 1));
        glVertex3f(0, fy + 1, floor_z);
        glVertex3f(0, fy, floor_z);
        glNormal3f(1, 0, 0);
        glVertex3f(static_cast<float>(w), fy, z(w, y));
        glVertex3f(static_cast<float>(w), fy + 1, z(w, y + 1));
        glVertex3f(static_cast<float>(w), fy + 1, floor_z);
        glVertex3f(static_cast<float>(w), fy, floor_z);
    }
    glEnd();
}

// Where a train is, which way it faces, and its rail height.
struct Pose {
    sim::MapPoint pos;
    float dx = 1, dy = 0; // unit, in map axes
    std::int64_t z_mm = 0;
};

Pose train_pose(const sim::Railway& rw, const sim::Train& t) {
    const sim::TrackNetwork& net = rw.track();
    Pose p;
    p.pos = rw.train_position(t.id);
    const sim::TrackEdge* e = nullptr;
    bool forward = true;
    std::int64_t along = 0;
    if (!t.path.empty() && t.step < t.path.size() &&
        (t.state == sim::TrainState::Moving || t.state == sim::TrainState::Servicing || t.state == sim::TrainState::BrokenDown)) {
        e = &net.edge(t.path[t.step].edge);
        forward = t.path[t.step].forward;
        along = t.offset_mm;
    } else {
        for (sim::EdgeId id : net.edges_at(t.at_node)) {
            if (!net.edge(id).removed) {
                e = &net.edge(id);
                forward = e->a == t.at_node;
                break;
            }
        }
    }
    if (!e) {
        p.z_mm = net.node(t.at_node).z_mm;
        return p;
    }
    const sim::TrackNode& from = net.node(forward ? e->a : e->b);
    const sim::TrackNode& to = net.node(forward ? e->b : e->a);
    const auto ddx = static_cast<float>(to.pos.x_mm - from.pos.x_mm), ddy = static_cast<float>(to.pos.y_mm - from.pos.y_mm);
    const float len = std::sqrt(ddx * ddx + ddy * ddy);
    if (len > 0) {
        p.dx = ddx / len;
        p.dy = ddy / len;
    }
    p.z_mm = e->length_mm > 0 ? from.z_mm + (to.z_mm - from.z_mm) * along / e->length_mm : from.z_mm;
    return p;
}

// A prism of eight sides along (dx, dy), centred at height zc: a boiler.
void prism(float x, float y, float zc, float dx, float dy, float hl, float r, const float* colour) {
    const float px = -dy, py = dx; // across
    glColor3fv(colour);
    constexpr int kSides = 8;
    for (int i = 0; i < kSides; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        const float cm = std::cos((a0 + a1) / 2), sm = std::sin((a0 + a1) / 2);
        glNormal3f(px * cm, py * cm, sm);
        glVertex3f(x - dx * hl + px * r * c0, y - dy * hl + py * r * c0, zc + r * s0);
        glVertex3f(x + dx * hl + px * r * c0, y + dy * hl + py * r * c0, zc + r * s0);
        glVertex3f(x + dx * hl + px * r * c1, y + dy * hl + py * r * c1, zc + r * s1);
        glVertex3f(x - dx * hl + px * r * c1, y - dy * hl + py * r * c1, zc + r * s1);
    }
}


// An upright cylinder of eight sides (tanks, silos, stacks, cooling towers);
// `top_r` narrows or widens the top. Inside GL_QUADS.
void cylinder(float x, float y, float z, float r, float h, const float* side, const float* top, float top_r = -1.0f) {
    if (top_r < 0) top_r = r;
    constexpr int kSides = 8;
    glColor3fv(side);
    for (int i = 0; i < kSides; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
        const float am = (a0 + a1) / 2;
        glNormal3f(std::cos(am), std::sin(am), (r - top_r) / std::max(0.001f, h));
        glVertex3f(x + r * std::cos(a0), y + r * std::sin(a0), z);
        glVertex3f(x + r * std::cos(a1), y + r * std::sin(a1), z);
        glVertex3f(x + top_r * std::cos(a1), y + top_r * std::sin(a1), z + h);
        glVertex3f(x + top_r * std::cos(a0), y + top_r * std::sin(a0), z + h);
    }
    glColor3fv(top);
    glNormal3f(0, 0, 1);
    for (int i = 0; i < kSides; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
        glVertex3f(x, y, z + h);
        glVertex3f(x + top_r * std::cos(a0), y + top_r * std::sin(a0), z + h);
        glVertex3f(x + top_r * std::cos(a1), y + top_r * std::sin(a1), z + h);
        glVertex3f(x, y, z + h);
    }
}

// A pitched roof over a box `hl` long along (dx, dy) and `hw` wide, its
// eaves at z and ridge `rise` above them. Inside GL_QUADS.
void gable(float x, float y, float z, float dx, float dy, float hl, float hw, float rise, const float* roof, const float* end) {
    const float px = -dy, py = dx;
    glColor3fv(roof);
    for (const float sgn : {-1.0f, 1.0f}) {
        glNormal3f(px * sgn * rise, py * sgn * rise, hw);
        glVertex3f(x - dx * hl + px * hw * sgn, y - dy * hl + py * hw * sgn, z);
        glVertex3f(x + dx * hl + px * hw * sgn, y + dy * hl + py * hw * sgn, z);
        glVertex3f(x + dx * hl, y + dy * hl, z + rise);
        glVertex3f(x - dx * hl, y - dy * hl, z + rise);
    }
    glColor3fv(end);
    for (const float sgn : {-1.0f, 1.0f}) {
        glNormal3f(dx * sgn, dy * sgn, 0);
        glVertex3f(x + dx * hl * sgn - px * hw, y + dy * hl * sgn - py * hw, z);
        glVertex3f(x + dx * hl * sgn + px * hw, y + dy * hl * sgn + py * hw, z);
        glVertex3f(x + dx * hl * sgn, y + dy * hl * sgn, z + rise);
        glVertex3f(x + dx * hl * sgn, y + dy * hl * sgn, z + rise);
    }
}

// A building with a pitched roof.
void barn(float x, float y, float z, float dx, float dy, float hl, float hw, float h, const float* wall, const float* roof) {
    box(x, y, z, dx, dy, hl, hw, h, wall, wall);
    gable(x, y, z + h, dx, dy, hl * 1.04f, hw * 1.1f, hw * 0.8f, roof, wall);
}

// A heap (spoil, logs, ore): a four-sided pyramid. Inside GL_QUADS.
void heap(float x, float y, float z, float half, float h, const float* colour) {
    glColor3fv(colour);
    const float cx[4] = {x - half, x + half, x + half, x - half}, cy[4] = {y - half, y - half, y + half, y + half};
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const float ex = cx[j] - cx[i], ey = cy[j] - cy[i];
        glNormal3f(ey, -ex, half);
        glVertex3f(cx[i], cy[i], z);
        glVertex3f(cx[j], cy[j], z);
        glVertex3f(x, y, z + h);
        glVertex3f(x, y, z + h);
    }
}

// The end of a prism: an octagon facing along +-(dx, dy). Inside GL_QUADS.
void prism_cap(float x, float y, float zc, float dx, float dy, float r, float facing, const float* colour) {
    const float px = -dy, py = dx;
    glColor3fv(colour);
    glNormal3f(dx * facing, dy * facing, 0);
    constexpr int kSides = 8;
    for (int i = 0; i < kSides; i += 2) {
        // Two triangles of the fan as one quad: centre, then three rim points.
        const auto rim = [&](int j) {
            const float an = 6.2831853f * static_cast<float>(j) / kSides;
            glVertex3f(x + px * r * std::cos(an), y + py * r * std::cos(an), zc + r * std::sin(an));
        };
        glVertex3f(x, y, zc);
        if (facing > 0) { rim(i); rim(i + 1); rim(i + 2); }
        else { rim(i + 2); rim(i + 1); rim(i); }
    }
}

// A pair of wheels on an axle across the track at (x, y), its hub `r` above
// the rail: a drum with its faces. Inside GL_QUADS.
void wheel(float x, float y, float rail_z, float dx, float dy, float r, float hw, const float* tyre, const float* face) {
    const float px = -dy, py = dx, zc = rail_z + r;
    constexpr int kSides = 8;
    glColor3fv(tyre);
    for (int i = 0; i < kSides; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
        const float am = (a0 + a1) / 2;
        glNormal3f(dx * std::cos(am), dy * std::cos(am), std::sin(am));
        const float x0 = dx * r * std::cos(a0), y0 = dy * r * std::cos(a0), z0 = r * std::sin(a0);
        const float x1 = dx * r * std::cos(a1), y1 = dy * r * std::cos(a1), z1 = r * std::sin(a1);
        glVertex3f(x + x0 - px * hw, y + y0 - py * hw, zc + z0);
        glVertex3f(x + x1 - px * hw, y + y1 - py * hw, zc + z1);
        glVertex3f(x + x1 + px * hw, y + y1 + py * hw, zc + z1);
        glVertex3f(x + x0 + px * hw, y + y0 + py * hw, zc + z0);
    }
    // The faces, seen from the side.
    for (const float sgn : {-1.0f, 1.0f}) {
        glColor3fv(face);
        glNormal3f(px * sgn, py * sgn, 0);
        for (int i = 0; i < kSides; i += 2) {
            const auto rim = [&](int j) {
                const float an = 6.2831853f * static_cast<float>(j) / kSides;
                glVertex3f(x + dx * r * std::cos(an) + px * hw * sgn, y + dy * r * std::cos(an) + py * hw * sgn, zc + r * std::sin(an));
            };
            glVertex3f(x + px * hw * sgn, y + py * hw * sgn, zc);
            if (sgn > 0) { rim(i + 2); rim(i + 1); rim(i); }
            else { rim(i); rim(i + 1); rim(i + 2); }
        }
    }
}

// Marks along both sides of a body (windows, doors, stripes): `n` of them
// evenly over its length, `w` half wide, from z0 to z1. Inside GL_QUADS.
void side_marks(float x, float y, float dx, float dy, float hl, float hw, int n, float w, float z0, float z1, const float* colour) {
    const float px = -dy, py = dx;
    glColor3fv(colour);
    for (const float sgn : {-1.0f, 1.0f}) {
        glNormal3f(px * sgn, py * sgn, 0);
        const float off = hw + 0.002f;
        for (int i = 0; i < n; ++i) {
            const float t = -hl + (static_cast<float>(i) + 0.5f) * 2 * hl / static_cast<float>(n);
            const float cx = x + dx * t + px * off * sgn, cy = y + dy * t + py * off * sgn;
            const float ex = dx * w * sgn, ey = dy * w * sgn;
            glVertex3f(cx + ex, cy + ey, z0);
            glVertex3f(cx - ex, cy - ey, z0);
            glVertex3f(cx - ex, cy - ey, z1);
            glVertex3f(cx + ex, cy + ey, z1);
        }
    }
}

// One piece of rolling stock: its kind decides the shape and colours.
enum class Stock { Steam, Diesel, Electric, Coach, Mail, Diner, Boxcar, Hopper, Caboose };

// Draw it with its front at (x, y), rail height z, facing (dx, dy), `s`
// times life size; returns its length. `load` colours a hopper's heap.
float rolling_stock(Stock kind, float x, float y, float z, float dx, float dy, float s, const float* livery,
                    const float* load = nullptr) {
    const float wide = 0.05f * s;
    const float frame[3] = {0.12f, 0.11f, 0.11f}, steel[3] = {0.22f, 0.22f, 0.24f};
    const float glass[3] = {0.16f, 0.2f, 0.26f};
    const auto at = [&](float along, float& ox, float& oy) {
        ox = x - dx * along;
        oy = y - dy * along;
    };
    float cx, cy;
    // A bogie: a dark frame on two axles.
    const auto bogie = [&](float along) {
        at(along, cx, cy);
        box(cx, cy, z + 0.008f * s, dx, dy, 0.03f * s, wide * 0.8f, 0.016f * s, frame, frame);
        for (const float d : {-0.017f, 0.017f}) {
            float wx, wy;
            at(along + d * s, wx, wy);
            wheel(wx, wy, z, dx, dy, 0.012f * s, wide * 0.75f, steel, frame);
        }
    };
    switch (kind) {
    case Stock::Steam: {
        const float len = 0.31f * s;
        const float black[3] = {0.13f, 0.13f, 0.14f}, smokebox[3] = {0.08f, 0.08f, 0.09f};
        const float driver[3] = {0.62f, 0.12f, 0.1f}, brass[3] = {0.78f, 0.62f, 0.28f}, lamp[3] = {1.0f, 0.92f, 0.55f};
        const float boiler[3] = {livery[0] * 0.35f + 0.08f, livery[1] * 0.35f + 0.08f, livery[2] * 0.35f + 0.08f};
        // Wheels: a pony truck, three big drivers, and the tender's.
        at(0.035f * s, cx, cy);
        wheel(cx, cy, z, dx, dy, 0.016f * s, wide * 0.7f, steel, frame);
        for (const float d : {0.095f, 0.15f, 0.205f}) {
            at(d * s, cx, cy);
            wheel(cx, cy, z, dx, dy, 0.03f * s, wide * 0.8f, steel, driver);
        }
        for (const float d : {0.25f, 0.295f}) {
            at(d * s, cx, cy);
            wheel(cx, cy, z, dx, dy, 0.015f * s, wide * 0.75f, steel, frame);
        }
        // The frame and running board.
        at(0.13f * s, cx, cy);
        box(cx, cy, z + 0.03f * s, dx, dy, 0.13f * s, wide * 0.55f, 0.012f * s, frame, frame);
        box(cx, cy, z + 0.042f * s, dx, dy, 0.13f * s, wide, 0.006f * s, black, black);
        // The cowcatcher and buffer beam.
        at(0.002f * s, cx, cy);
        box(cx, cy, z + 0.006f * s, dx, dy, 0.008f * s, wide * 0.8f, 0.034f * s, driver, driver);
        // Boiler and smokebox, capped at the front.
        const float zc = z + 0.084f * s, r = 0.036f * s;
        at(0.11f * s, cx, cy);
        prism(cx, cy, zc, dx, dy, 0.08f * s, r, boiler);
        at(0.017f * s, cx, cy);
        prism(cx, cy, zc, dx, dy, 0.013f * s, r * 1.05f, smokebox);
        at(0.004f * s, cx, cy);
        prism_cap(cx, cy, zc, dx, dy, r * 1.05f, 1.0f, smokebox);
        // Chimney (flared), domes and headlamp.
        at(0.02f * s, cx, cy);
        cylinder(cx, cy, zc + r * 0.8f, 0.009f * s, 0.04f * s, black, black, 0.015f * s);
        box(cx + dx * 0.012f * s, cy + dy * 0.012f * s, zc + r * 0.7f, dx, dy, 0.006f * s, 0.008f * s, 0.012f * s, lamp, lamp);
        at(0.1f * s, cx, cy);
        cylinder(cx, cy, zc + r * 0.8f, 0.013f * s, 0.024f * s, brass, brass, 0.008f * s);
        at(0.15f * s, cx, cy);
        cylinder(cx, cy, zc + r * 0.8f, 0.011f * s, 0.018f * s, boiler, boiler, 0.007f * s);
        // The cab, with a roof and windows.
        at(0.215f * s, cx, cy);
        box(cx, cy, z + 0.048f * s, dx, dy, 0.035f * s, wide, 0.072f * s, livery, livery);
        box(cx, cy, z + 0.12f * s, dx, dy, 0.042f * s, wide * 1.12f, 0.007f * s, black, black);
        side_marks(cx, cy, dx, dy, 0.035f * s, wide, 1, 0.014f * s, z + 0.085f * s, z + 0.11f * s, glass);
        // The tender, heaped with coal.
        at(0.272f * s, cx, cy);
        box(cx, cy, z + 0.03f * s, dx, dy, 0.035f * s, wide * 0.95f, 0.062f * s, livery, black);
        heap(cx, cy, z + 0.092f * s, 0.028f * s, 0.018f * s, black);
        return len;
    }
    case Stock::Diesel:
    case Stock::Electric: {
        const float len = 0.27f * s;
        const float roof[3] = {livery[0] * 0.55f, livery[1] * 0.55f, livery[2] * 0.55f};
        const float stripe[3] = {std::min(1.0f, livery[0] * 0.4f + 0.6f), std::min(1.0f, livery[1] * 0.4f + 0.55f),
                                 std::min(1.0f, livery[2] * 0.4f + 0.3f)};
        bogie(0.05f * s);
        bogie(0.22f * s);
        at(len / 2, cx, cy);
        box(cx, cy, z + 0.024f * s, dx, dy, len / 2, wide * 0.9f, 0.012f * s, frame, frame);
        if (kind == Stock::Diesel) {
            // A road-switcher: a long hood, the cab near the front, a short nose.
            at(0.03f * s, cx, cy);
            box(cx, cy, z + 0.036f * s, dx, dy, 0.025f * s, wide * 0.7f, 0.045f * s, livery, roof);
            at(0.075f * s, cx, cy);
            box(cx, cy, z + 0.036f * s, dx, dy, 0.022f * s, wide, 0.075f * s, livery, roof);
            side_marks(cx, cy, dx, dy, 0.022f * s, wide, 2, 0.007f * s, z + 0.08f * s, z + 0.1f * s, glass);
            at(0.0545f * s, cx, cy);
            box(cx, cy, z + 0.08f * s, dx, dy, 0.0015f * s, wide * 0.8f, 0.02f * s, glass, glass); // windscreen
            at(0.18f * s, cx, cy);
            box(cx, cy, z + 0.036f * s, dx, dy, 0.083f * s, wide * 0.72f, 0.058f * s, livery, roof);
            side_marks(cx, cy, dx, dy, 0.083f * s, wide * 0.72f, 1, 0.08f * s, z + 0.05f * s, z + 0.058f * s, stripe);
            // An exhaust stack.
            at(0.16f * s, cx, cy);
            cylinder(cx, cy, z + 0.094f * s, 0.006f * s, 0.01f * s, frame, frame);
        } else {
            // A box cab, windscreens at both ends and a pantograph.
            at(len / 2, cx, cy);
            box(cx, cy, z + 0.036f * s, dx, dy, len / 2 * 0.96f, wide, 0.075f * s, livery, roof);
            side_marks(cx, cy, dx, dy, len / 2 * 0.96f, wide, 6, 0.008f * s, z + 0.08f * s, z + 0.098f * s, glass);
            side_marks(cx, cy, dx, dy, len / 2 * 0.96f, wide, 1, len / 2 * 0.9f, z + 0.05f * s, z + 0.058f * s, stripe);
            for (const float end : {0.006f, len / s - 0.006f}) {
                at(end * s, cx, cy);
                box(cx, cy, z + 0.08f * s, dx, dy, 0.002f * s, wide * 0.8f, 0.02f * s, glass, glass);
            }
            const float bar[3] = {0.3f, 0.3f, 0.32f};
            at(0.09f * s, cx, cy);
            box(cx, cy, z + 0.111f * s, dx, dy, 0.025f * s, wide * 0.5f, 0.006f * s, bar, bar); // base
            at(0.08f * s, cx, cy);
            box(cx, cy, z + 0.117f * s, dx, dy, 0.003f * s, 0.003f * s, 0.04f * s, bar, bar); // arm
            box(cx, cy, z + 0.155f * s, dx, dy, 0.005f * s, wide * 0.7f, 0.004f * s, bar, bar); // shoe
        }
        return len;
    }
    case Stock::Coach:
    case Stock::Mail:
    case Stock::Diner: {
        const float len = 0.23f * s;
        const float body[3] = {kind == Stock::Mail ? 0.45f : 0.36f, kind == Stock::Mail ? 0.12f : 0.17f,
                               kind == Stock::Mail ? 0.1f : 0.11f};
        const float roof[3] = {0.3f, 0.3f, 0.32f};
        const float lit[3] = {kind == Stock::Diner ? 0.85f : 0.75f, kind == Stock::Diner ? 0.72f : 0.7f,
                              kind == Stock::Diner ? 0.4f : 0.55f};
        bogie(0.04f * s);
        bogie(len - 0.04f * s);
        at(len / 2, cx, cy);
        box(cx, cy, z + 0.024f * s, dx, dy, len / 2 * 0.97f, wide, 0.068f * s, body, roof);
        // A clerestory along the roof.
        box(cx, cy, z + 0.092f * s, dx, dy, len / 2 * 0.8f, wide * 0.5f, 0.01f * s, roof, roof);
        if (kind == Stock::Mail) {
            const float door[3] = {0.3f, 0.08f, 0.06f};
            side_marks(cx, cy, dx, dy, len / 2 * 0.6f, wide, 2, 0.012f * s, z + 0.03f * s, z + 0.085f * s, door);
        } else {
            side_marks(cx, cy, dx, dy, len / 2 * 0.9f, wide, 8, 0.008f * s, z + 0.058f * s, z + 0.078f * s, lit);
        }
        return len;
    }
    case Stock::Boxcar:
    case Stock::Hopper: {
        const float len = 0.19f * s;
        bogie(0.035f * s);
        bogie(len - 0.035f * s);
        at(len / 2, cx, cy);
        if (kind == Stock::Boxcar) {
            const float body[3] = {0.5f, 0.3f, 0.19f}, top[3] = {0.4f, 0.25f, 0.16f}, door[3] = {0.36f, 0.21f, 0.13f};
            box(cx, cy, z + 0.024f * s, dx, dy, len / 2 * 0.97f, wide, 0.062f * s, body, top);
            side_marks(cx, cy, dx, dy, len / 2 * 0.95f, wide, 6, 0.002f * s, z + 0.026f * s, z + 0.084f * s, door); // ribs
            side_marks(cx, cy, dx, dy, 0.0f, wide, 1, 0.018f * s, z + 0.028f * s, z + 0.08f * s, door); // the door
        } else {
            const float body[3] = {0.24f, 0.23f, 0.22f};
            const float heap_rgb[3] = {load ? load[0] : 0.08f, load ? load[1] : 0.08f, load ? load[2] : 0.08f};
            box(cx, cy, z + 0.024f * s, dx, dy, len / 2 * 0.97f, wide, 0.05f * s, body, heap_rgb);
            side_marks(cx, cy, dx, dy, len / 2 * 0.95f, wide, 4, 0.002f * s, z + 0.026f * s, z + 0.074f * s, frame); // ribs
            // Its load heaped above the sides.
            heap(cx - dx * len * 0.22f, cy - dy * len * 0.22f, z + 0.074f * s, wide * 0.85f, 0.02f * s, heap_rgb);
            heap(cx + dx * len * 0.22f, cy + dy * len * 0.22f, z + 0.074f * s, wide * 0.85f, 0.02f * s, heap_rgb);
        }
        return len;
    }
    case Stock::Caboose: {
        const float len = 0.13f * s;
        const float red[3] = {0.7f, 0.15f, 0.1f}, roof[3] = {0.3f, 0.1f, 0.08f};
        bogie(0.03f * s);
        bogie(len - 0.03f * s);
        at(len / 2, cx, cy);
        box(cx, cy, z + 0.024f * s, dx, dy, len / 2 * 0.9f, wide, 0.058f * s, red, roof);
        side_marks(cx, cy, dx, dy, len / 2 * 0.8f, wide, 3, 0.007f * s, z + 0.055f * s, z + 0.072f * s, glass);
        box(cx, cy, z + 0.082f * s, dx, dy, len / 6, wide * 0.65f, 0.026f * s, red, roof); // cupola
        side_marks(cx, cy, dx, dy, len / 6, wide * 0.65f, 2, 0.006f * s, z + 0.09f * s, z + 0.102f * s, glass);
        return len;
    }
    }
    return 0.0f;
}

// Each kind's length, as drawn in code, so trains space the same whether
// their stock comes from code or art.
float stock_length(Stock kind, float s) {
    switch (kind) {
    case Stock::Steam: return 0.31f * s;
    case Stock::Diesel:
    case Stock::Electric: return 0.27f * s;
    case Stock::Coach:
    case Stock::Mail:
    case Stock::Diner: return 0.23f * s;
    case Stock::Boxcar:
    case Stock::Hopper: return 0.19f * s;
    case Stock::Caboose: return 0.13f * s;
    }
    return 0.2f * s;
}

const char* stock_role(Stock kind) {
    switch (kind) {
    case Stock::Steam: return "engine_steam";
    case Stock::Diesel: return "engine_diesel";
    case Stock::Electric: return "engine_electric";
    case Stock::Coach: return "car_coach";
    case Stock::Mail: return "car_mail";
    case Stock::Diner: return "car_diner";
    case Stock::Boxcar: return "car_boxcar";
    case Stock::Hopper: return "car_hopper";
    case Stock::Caboose: return "car_caboose";
    }
    return "";
}

// Rolling stock from the art where it has a model, else drawn in code;
// returns its length. A freight car looks first for its cargo's role
// ("car:coal"), then its kind's; `seed` picks among several. Inside
// GL_QUADS (a model briefly leaves it).
float stock(Stock kind, float x, float y, float z, float dx, float dy, float s, const float* livery, std::uint32_t seed,
            const std::string& cargo = {}, const float* load = nullptr) {
    const models::Model* m = cargo.empty() ? nullptr : models::pick("car:" + cargo, seed);
    if (!m) m = models::pick(stock_role(kind), seed);
    if (m) {
        const float len = stock_length(kind, s);
        glEnd();
        models::draw(*m, x - dx * len / 2, y - dy * len / 2, z, dx, dy, 0.0f, len * 0.96f);
        glBegin(GL_QUADS);
        return len;
    }
    return rolling_stock(kind, x, y, z, dx, dy, s, livery, load);
}

// A patch of crop or pasture on the ground. Inside GL_QUADS.
void field(float x, float y, float half, const float* colour, const Camera& cam) {
    glColor3fv(colour);
    glNormal3f(0, 0, 1);
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const float x0 = x - half + half * 0.5f * static_cast<float>(i), y0 = y - half + half * 0.5f * static_cast<float>(j);
            ground_quad(x0, y0, x0 + half * 0.5f, y0 + half * 0.5f, cam);
        }
}

bool ends_with(const std::string& s, const char* suffix) {
    const std::string tail(suffix);
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// One industry, shaped by what it is (k: map cells across its spot).
void industry_3d(const sim::IndustryType& t, float x, float y, float k, std::uint32_t seed, const Camera& cam) {
    const std::string& key = t.key;
    const float z = cam.ground_z(x, y);
    const float grey[3] = {0.55f, 0.53f, 0.5f}, dark[3] = {0.25f, 0.24f, 0.24f}, slate[3] = {0.35f, 0.36f, 0.4f};
    const float brick[3] = {0.6f, 0.3f, 0.22f}, white[3] = {0.88f, 0.88f, 0.86f}, metal[3] = {0.7f, 0.72f, 0.75f};
    const float timber[3] = {0.5f, 0.36f, 0.22f}, red_barn[3] = {0.62f, 0.18f, 0.14f};
    const float yaw = unit(seed) < 0.5f ? 0.0f : 1.5708f;
    const float dx = std::cos(yaw), dy = std::sin(yaw);
    if (ends_with(key, "_mine")) {
        float spoil[3] = {0.12f, 0.12f, 0.12f};
        if (key == "iron_mine") { spoil[0] = 0.45f; spoil[1] = 0.22f; spoil[2] = 0.15f; }
        else if (key == "bauxite_mine") { spoil[0] = 0.6f; spoil[1] = 0.3f; spoil[2] = 0.2f; }
        else if (key == "uranium_mine") { spoil[0] = 0.55f; spoil[1] = 0.55f; spoil[2] = 0.35f; }
        barn(x - 0.12f * k, y + 0.1f * k, z, dx, dy, 0.14f * k, 0.08f * k, 0.12f, timber, slate);
        // The headframe: two legs and a beam with its wheel.
        box(x + 0.08f * k, y - 0.06f * k, z, dx, dy, 0.012f, 0.012f, 0.5f, dark, dark);
        box(x + 0.08f * k, y + 0.06f * k, z, dx, dy, 0.012f, 0.012f, 0.5f, dark, dark);
        box(x + 0.08f * k, y, z + 0.48f, dx, dy, 0.03f, 0.07f * k, 0.04f, dark, dark);
        heap(x + 0.18f * k, y + 0.22f * k, z, 0.14f * k, 0.22f, spoil);
        return;
    }
    if (key == "logging_camp") {
        barn(x, y - 0.12f * k, z, dx, dy, 0.1f * k, 0.07f * k, 0.1f, timber, dark);
        for (int i = 0; i < 3; ++i) prism(x - 0.1f * k + 0.1f * k * static_cast<float>(i), y + 0.15f * k, z + 0.03f, 0, 1, 0.12f * k, 0.03f, timber);
        return;
    }
    if (ends_with(key, "_farm") || key == "fruit_orchard" || key == "cattle_ranch" || key == "rubber_plantation") {
        float crop[3] = {0.55f, 0.6f, 0.25f};
        if (key == "corn_farm" || key == "grain_farm") { crop[0] = 0.82f; crop[1] = 0.72f; crop[2] = 0.3f; }
        else if (key == "cotton_farm") { crop[0] = 0.85f; crop[1] = 0.85f; crop[2] = 0.8f; }
        else if (key == "rice_farm") { crop[0] = 0.5f; crop[1] = 0.7f; crop[2] = 0.45f; }
        else if (key == "sugar_farm" || key == "coffee_farm" || key == "rubber_plantation") { crop[0] = 0.3f; crop[1] = 0.5f; crop[2] = 0.22f; }
        else if (key == "dairy_farm" || key == "cattle_ranch" || key == "sheep_farm") { crop[0] = 0.42f; crop[1] = 0.62f; crop[2] = 0.3f; }
        field(x + 0.12f * k, y + 0.12f * k, 0.3f * k, crop, cam);
        barn(x - 0.18f * k, y - 0.15f * k, z, dx, dy, 0.1f * k, 0.07f * k, 0.12f, red_barn, slate);
        cylinder(x - 0.02f * k, y - 0.22f * k, z, 0.04f * k, 0.3f, white, metal);
        if (key == "fruit_orchard" || key == "coffee_farm" || key == "rubber_plantation") {
            const float leaf[3] = {0.18f, 0.42f, 0.16f};
            for (int i = 0; i < 6; ++i) {
                const float tx = x + 0.12f * k + (static_cast<float>(i % 3) - 1) * 0.12f * k;
                const float ty = y + 0.12f * k + (static_cast<float>(i / 3) - 0.5f) * 0.14f * k;
                cylinder(tx, ty, cam.ground_z(tx, ty), 0.035f, 0.1f, leaf, leaf, 0.01f);
            }
        }
        return;
    }
    if (key == "oil_well") {
        // A derrick over a small pump house.
        heap(x, y, z, 0.07f, 0.6f, dark);
        box(x + 0.12f * k, y, z, dx, dy, 0.06f, 0.04f, 0.06f, grey, grey);
        cylinder(x - 0.15f * k, y + 0.12f * k, z, 0.07f * k, 0.12f, metal, white);
        return;
    }
    if (key == "nuclear_plant") {
        cylinder(x + 0.12f * k, y, z, 0.16f * k, 0.55f, white, grey, 0.11f * k);
        add_smoke(x + 0.12f * k, y, z + 0.55f, 0.1f * k, true);
        box(x - 0.15f * k, y, z, dx, dy, 0.1f * k, 0.1f * k, 0.2f, grey, slate);
        cylinder(x - 0.15f * k, y + 0.2f * k, z, 0.07f * k, 0.18f, white, white, 0.04f * k);
        return;
    }
    if (key == "electric_plant") {
        box(x, y, z, dx, dy, 0.2f * k, 0.12f * k, 0.22f, brick, slate);
        cylinder(x + 0.12f * k, y + 0.15f * k, z, 0.035f, 0.75f, brick, dark, 0.025f);
        cylinder(x - 0.04f * k, y + 0.15f * k, z, 0.035f, 0.75f, brick, dark, 0.025f);
        add_smoke(x + 0.12f * k, y + 0.15f * k, z + 0.75f, 0.075f);
        add_smoke(x - 0.04f * k, y + 0.15f * k, z + 0.75f, 0.075f);
        return;
    }
    if (key == "chemical_plant" || key == "refinery" || key == "plastics_factory" || key == "fertilizer_factory") {
        box(x - 0.12f * k, y - 0.1f * k, z, dx, dy, 0.12f * k, 0.08f * k, 0.16f, grey, slate);
        for (int i = 0; i < 3; ++i) {
            cylinder(x + 0.12f * k, y - 0.2f * k + 0.17f * k * static_cast<float>(i), z, 0.065f * k, 0.18f, white, metal);
        }
        cylinder(x - 0.2f * k, y + 0.16f * k, z, 0.02f, 0.6f, metal, dark);
        add_smoke(x - 0.2f * k, y + 0.16f * k, z + 0.6f, 0.06f);
        return;
    }
    if (key == "steel_mill" || key == "aluminum_mill") {
        box(x - 0.05f * k, y, z, dx, dy, 0.22f * k, 0.14f * k, 0.24f, dark, slate);
        cylinder(x + 0.2f * k, y - 0.15f * k, z, 0.07f * k, 0.4f, brick, dark, 0.05f * k); // furnace
        for (int i = 0; i < 3; ++i) {
            cylinder(x - 0.18f * k + 0.12f * k * static_cast<float>(i), y + 0.18f * k, z, 0.03f, 0.8f, brick, dark);
            add_smoke(x - 0.18f * k + 0.12f * k * static_cast<float>(i), y + 0.18f * k, z + 0.8f, 0.07f);
        }
        return;
    }
    if (key == "barracks") {
        const float olive[3] = {0.45f, 0.45f, 0.3f};
        for (int i = 0; i < 3; ++i) barn(x, y - 0.2f * k + 0.2f * k * static_cast<float>(i), z, 1, 0, 0.25f * k, 0.05f * k, 0.08f, olive, slate);
        return;
    }
    if (key == "commercial_building") {
        const float stone[3] = {0.78f, 0.74f, 0.66f}, glass[3] = {0.35f, 0.45f, 0.55f};
        box(x, y, z, dx, dy, 0.14f * k, 0.12f * k, 0.6f, stone, slate);
        box(x, y, z + 0.1f, dx, dy, 0.145f * k, 0.125f * k, 0.03f, glass, glass);
        box(x, y, z + 0.3f, dx, dy, 0.145f * k, 0.125f * k, 0.03f, glass, glass);
        return;
    }
    if (key == "bakery") {
        barn(x, y, z, dx, dy, 0.1f * k, 0.07f * k, 0.1f, brick, slate);
        return;
    }
    if (t.kind == sim::IndustryKind::Port) {
        const float quay[3] = {0.5f, 0.48f, 0.44f};
        box(x, y, z, dx, dy, 0.32f * k, 0.22f * k, 0.05f, quay, quay);
        barn(x - 0.12f * k, y - 0.08f * k, z + 0.05f, dx, dy, 0.14f * k, 0.07f * k, 0.12f, timber, slate);
        // A crane: a tower and its jib.
        box(x + 0.18f * k, y + 0.1f * k, z + 0.05f, dx, dy, 0.02f, 0.02f, 0.45f, dark, dark);
        box(x + 0.18f * k + dx * 0.12f, y + 0.1f * k + dy * 0.12f, z + 0.45f, dx, dy, 0.14f, 0.015f, 0.025f, dark, dark);
        return;
    }
    if (t.kind == sim::IndustryKind::Warehouse) {
        barn(x, y, z, dx, dy, 0.28f * k, 0.12f * k, 0.15f, timber, slate);
        return;
    }
    // Other mills and factories: a works with sawtooth roofs and a chimney.
    const float wall[3] = {0.58f, 0.42f, 0.36f};
    box(x, y, z, dx, dy, 0.24f * k, 0.15f * k, 0.18f, wall, wall);
    for (int i = 0; i < 3; ++i) {
        const float along = -0.16f * k + 0.16f * k * static_cast<float>(i);
        gable(x + dx * along, y + dy * along, z + 0.18f, -dy, dx, 0.15f * k, 0.08f * k, 0.07f, slate, wall);
    }
    cylinder(x + dx * 0.2f * k - dy * 0.1f * k, y + dy * 0.2f * k + dx * 0.1f * k, z, 0.03f, 0.55f, brick, dark, 0.022f);
    add_smoke(x + dx * 0.2f * k - dy * 0.1f * k, y + dy * 0.2f * k + dx * 0.1f * k, z + 0.55f, 0.06f);
}

// Bridge decks by type: wood brown, stone grey, steel dark, suspension pale.
void bridge_rgb(sim::BridgeType b, float* c) {
    switch (b) {
    case sim::BridgeType::Wood: c[0] = 0.45f; c[1] = 0.32f; c[2] = 0.2f; break;
    case sim::BridgeType::Stone: c[0] = 0.62f; c[1] = 0.6f; c[2] = 0.56f; break;
    case sim::BridgeType::Steel: c[0] = 0.3f; c[1] = 0.33f; c[2] = 0.38f; break;
    case sim::BridgeType::Suspension: c[0] = 0.85f; c[1] = 0.85f; c[2] = 0.82f; break;
    case sim::BridgeType::None: c[0] = c[1] = c[2] = 0.5f; break;
    }
}

// Track close up (rt3-clone-spec §15.6 views): a ballast bed with sleepers
// and two rails on each track, following the ground in cuttings; bridges as
// decks on piers; tunnels unseen but for their mouths.
void track_3d(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player) {
    const sim::TrackNetwork& net = rw.track();
    const bool sleepers = cam.zoom >= 26.0f;
    const auto tile = [&](std::int64_t mm) { return static_cast<float>(mm) / cam.mm_per_tile; };
    struct Strip {
        float x0, y0, z0, x1, y1, z1; // a straight bit of one edge
    };
    begin_lit();
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    for (const sim::TrackEdge& e : net.edges()) {
        if (e.removed || e.kind == sim::TrackKind::Tunnel) continue;
        const sim::TrackNode& a = net.node(e.a);
        const sim::TrackNode& b = net.node(e.b);
        const float ax = tile(a.pos.x_mm), ay = tile(a.pos.y_mm), bx = tile(b.pos.x_mm), by = tile(b.pos.y_mm);
        const float len = std::hypot(bx - ax, by - ay);
        if (len <= 0) continue;
        const float dx = (bx - ax) / len, dy = (by - ay) / len, px = -dy, py = dx;
        float ballast[3] = {0.47f, 0.43f, 0.38f};
        if (e.kind == sim::TrackKind::Bridge) bridge_rgb(e.bridge, ballast);
        if (e.owner != player) {
            float r, g, bl;
            owner_rgb(e.owner, player, r, g, bl);
            ballast[0] = ballast[0] * 0.55f + r * 0.45f;
            ballast[1] = ballast[1] * 0.55f + g * 0.45f;
            ballast[2] = ballast[2] * 0.55f + bl * 0.45f;
        }
        const float sleeper[3] = {0.28f, 0.2f, 0.13f};
        const int pieces = std::max(1, static_cast<int>(std::ceil(len / 0.25f)));
        const float za = cam.z_of(a.z_mm), zb = cam.z_of(b.z_mm);
        const auto z_at = [&](float t) {
            const float rail = za + (zb - za) * t;
            if (e.kind != sim::TrackKind::Ground) return rail;
            // In a cutting the ground stands above the rail profile: follow it.
            return std::max(rail, cam.ground_z(ax + (bx - ax) * t, ay + (by - ay) * t));
        };
        const float offsets[2] = {-0.07f, 0.07f};
        const int tracks = e.double_track ? 2 : 1;
        for (int k = 0; k < tracks; ++k) {
            const float off = e.double_track ? offsets[k] : 0.0f;
            const float hw = 0.065f; // half the ballast width
            for (int i = 0; i < pieces; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(pieces), t1 = static_cast<float>(i + 1) / static_cast<float>(pieces);
                const Strip st{ax + (bx - ax) * t0 + px * off, ay + (by - ay) * t0 + py * off, z_at(t0) + 0.02f,
                               ax + (bx - ax) * t1 + px * off, ay + (by - ay) * t1 + py * off, z_at(t1) + 0.02f};
                glColor3fv(ballast);
                glVertex3f(st.x0 - px * hw, st.y0 - py * hw, st.z0);
                glVertex3f(st.x1 - px * hw, st.y1 - py * hw, st.z1);
                glVertex3f(st.x1 + px * hw, st.y1 + py * hw, st.z1);
                glVertex3f(st.x0 + px * hw, st.y0 + py * hw, st.z0);
                if (e.kind == sim::TrackKind::Bridge) {
                    // The deck's sides.
                    const float side[3] = {ballast[0] * 0.7f, ballast[1] * 0.7f, ballast[2] * 0.7f};
                    glColor3fv(side);
                    for (const float sgn : {-1.0f, 1.0f}) {
                        glNormal3f(px * sgn, py * sgn, 0);
                        glVertex3f(st.x0 + px * hw * sgn, st.y0 + py * hw * sgn, st.z0);
                        glVertex3f(st.x1 + px * hw * sgn, st.y1 + py * hw * sgn, st.z1);
                        glVertex3f(st.x1 + px * hw * sgn, st.y1 + py * hw * sgn, st.z1 - 0.07f);
                        glVertex3f(st.x0 + px * hw * sgn, st.y0 + py * hw * sgn, st.z0 - 0.07f);
                    }
                    glNormal3f(0, 0, 1);
                }
                if (sleepers) {
                    glColor3fv(sleeper);
                    const float seg = len / static_cast<float>(pieces);
                    const int n = std::max(1, static_cast<int>(seg / 0.05f));
                    for (int j = 0; j < n; ++j) {
                        const float u = (static_cast<float>(j) + 0.5f) / static_cast<float>(n);
                        const float sx = st.x0 + (st.x1 - st.x0) * u, sy = st.y0 + (st.y1 - st.y0) * u;
                        const float sz = st.z0 + (st.z1 - st.z0) * u + 0.004f;
                        const float hl = 0.009f, sw = 0.045f;
                        glVertex3f(sx - dx * hl - px * sw, sy - dy * hl - py * sw, sz);
                        glVertex3f(sx + dx * hl - px * sw, sy + dy * hl - py * sw, sz);
                        glVertex3f(sx + dx * hl + px * sw, sy + dy * hl + py * sw, sz);
                        glVertex3f(sx - dx * hl + px * sw, sy - dy * hl + py * sw, sz);
                    }
                }
            }
        }
        // Piers under bridges, from the deck down to the ground.
        if (e.kind == sim::TrackKind::Bridge) {
            const float pier[3] = {ballast[0] * 0.8f, ballast[1] * 0.8f, ballast[2] * 0.8f};
            const int n = std::max(1, static_cast<int>(len / 0.6f));
            for (int i = 0; i <= n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n);
                const float x = ax + (bx - ax) * t, y = ay + (by - ay) * t;
                const float top = za + (zb - za) * t, ground = cam.ground_z(x, y) - 0.2f;
                if (top - ground > 0.05f) box(x, y, ground, dx, dy, 0.03f, 0.05f, top - ground, pier, pier);
            }
            glNormal3f(0, 0, 1);
        }
    }
    // Tunnel mouths, where a tunnel meets track in the open.
    const float stone[3] = {0.4f, 0.38f, 0.36f}, dark[3] = {0.05f, 0.05f, 0.05f};
    for (const sim::TrackEdge& e : net.edges()) {
        if (e.removed || e.kind != sim::TrackKind::Tunnel) continue;
        for (const sim::NodeId n : {e.a, e.b}) {
            bool open = false;
            for (sim::EdgeId other : net.edges_at(n)) {
                const sim::TrackEdge& o = net.edge(other);
                open = open || (!o.removed && o.kind != sim::TrackKind::Tunnel);
            }
            if (!open) continue;
            const sim::TrackNode& here = net.node(n);
            const sim::TrackNode& there = net.node(n == e.a ? e.b : e.a);
            const float hx = tile(here.pos.x_mm), hy = tile(here.pos.y_mm);
            const float l = std::hypot(tile(there.pos.x_mm) - hx, tile(there.pos.y_mm) - hy);
            if (l <= 0) continue;
            const float dx = (tile(there.pos.x_mm) - hx) / l, dy = (tile(there.pos.y_mm) - hy) / l;
            const float z = cam.z_of(here.z_mm);
            box(hx + dx * 0.03f, hy + dy * 0.03f, z, dx, dy, 0.03f, e.double_track ? 0.2f : 0.12f, 0.16f, stone, stone);
            box(hx + dx * 0.031f, hy + dy * 0.031f, z, dx, dy, 0.031f, e.double_track ? 0.15f : 0.07f, 0.11f, dark, stone);
        }
    }
    glEnd();
    end_lit();
    // The rails themselves: thin steel lines on each track.
    if (cam.zoom >= 14.0f) {
        glLineWidth(cam.zoom >= 40.0f ? 2.0f : 1.0f);
        glBegin(GL_LINES);
        for (const sim::TrackEdge& e : net.edges()) {
            if (e.removed || e.kind == sim::TrackKind::Tunnel) continue;
            const sim::TrackNode& a = net.node(e.a);
            const sim::TrackNode& b = net.node(e.b);
            const float ax = tile(a.pos.x_mm), ay = tile(a.pos.y_mm), bx = tile(b.pos.x_mm), by = tile(b.pos.y_mm);
            const float len = std::hypot(bx - ax, by - ay);
            if (len <= 0) continue;
            const float px = -(by - ay) / len, py = (bx - ax) / len;
            if (e.owner == player) glColor3f(0.62f, 0.62f, 0.66f);
            else {
                float r, g, bl;
                owner_rgb(e.owner, player, r, g, bl);
                glColor3f(r, g, bl);
            }
            const int pieces = std::max(1, static_cast<int>(std::ceil(len / 0.25f)));
            const float za = cam.z_of(a.z_mm), zb = cam.z_of(b.z_mm);
            const float offsets[2] = {e.double_track ? -0.07f : 0.0f, 0.07f};
            for (int k = 0; k < (e.double_track ? 2 : 1); ++k) {
                const float off = offsets[k];
                for (const float rail : {-0.026f, 0.026f}) {
                    for (int i = 0; i < pieces; ++i) {
                        for (const int end : {i, i + 1}) {
                            const float t = static_cast<float>(end) / static_cast<float>(pieces);
                            const float x = ax + (bx - ax) * t, y = ay + (by - ay) * t;
                            float z = za + (zb - za) * t;
                            if (e.kind == sim::TrackKind::Ground) z = std::max(z, cam.ground_z(x, y));
                            glVertex3f(x + px * (off + rail), y + py * (off + rail), z + 0.035f);
                        }
                    }
                }
            }
        }
        glEnd();
    }
}

// The size buildings and trains are drawn at in 3D: never smaller than a
// few pixels.
float object_scale(const Camera& cam) { return std::max(1.5f, 8.0f / (cam.zoom * 0.2f)); }

bool on_water(float x, float y, const Camera& cam) {
    if (!cam.terrain) return false;
    const auto tx = static_cast<std::int32_t>(std::floor(x)), ty = static_cast<std::int32_t>(std::floor(y));
    if (tx < 0 || ty < 0 || tx >= cam.terrain->width() || ty >= cam.terrain->height()) return true;
    return cam.terrain->ground(tx, ty) == sim::GroundType::Water;
}

// Where the railway and industries stand, in quarter tiles, so houses keep
// off them.
class Occupancy {
public:
    static constexpr float kCell = 0.25f;
    Occupancy(const sim::Railway& rw, const Camera& cam, int width, int height)
        : w_(static_cast<int>(static_cast<float>(width) / kCell) + 1), h_(static_cast<int>(static_cast<float>(height) / kCell) + 1),
          cells_(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), false) {
        const sim::TrackNetwork& net = rw.track();
        const auto tile = [&](std::int64_t mm) { return static_cast<float>(mm) / cam.mm_per_tile; };
        const float s = object_scale(cam);
        for (const sim::TrackEdge& e : net.edges()) {
            if (e.removed || e.kind == sim::TrackKind::Tunnel) continue;
            const sim::TrackNode& a = net.node(e.a);
            const sim::TrackNode& b = net.node(e.b);
            const float ax = tile(a.pos.x_mm), ay = tile(a.pos.y_mm), bx = tile(b.pos.x_mm), by = tile(b.pos.y_mm);
            const int steps = std::max(1, static_cast<int>(std::hypot(bx - ax, by - ay) / 0.1f));
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                mark(ax + (bx - ax) * t, ay + (by - ay) * t, e.double_track ? 0.3f : 0.22f);
            }
        }
        // Stations' platforms and houses reach about half their scale out.
        for (const sim::Station& st : rw.stations()) {
            const sim::TrackNode& n = net.node(st.node);
            const float reach = 0.25f + (st.size == sim::StationSize::Large ? 0.55f : st.size == sim::StationSize::Small ? 0.3f : 0.4f) * s;
            mark(tile(n.pos.x_mm), tile(n.pos.y_mm), reach);
        }
        for (const sim::StationBuilding& b : rw.station_buildings()) mark(tile(b.pos.x_mm), tile(b.pos.y_mm), 0.12f * s);
        for (const sim::ServiceBuilding& b : rw.service_buildings()) {
            const sim::TrackNode& n = net.node(b.node);
            mark(tile(n.pos.x_mm), tile(n.pos.y_mm), 0.25f + 0.25f * s);
        }
    }
    // A disc `r` tiles round a point (towns, for the trees).
    void mark_disc(float x, float y, float r) { mark(x, y, r); }
    // Industries' spots, `half` tiles each way from their centres.
    void mark_square(float x, float y, float half) {
        for (float yy = y - half; yy <= y + half; yy += kCell)
            for (float xx = x - half; xx <= x + half; xx += kCell) mark(xx, yy, 0.0f);
    }
    // Is any cell within `r` tiles of the point taken?
    bool blocked(float x, float y, float r) const {
        const int x0 = cell(x - r), x1 = cell(x + r), y0 = cell(y - r), y1 = cell(y + r);
        for (int cy = std::max(0, y0); cy <= std::min(h_ - 1, y1); ++cy) {
            for (int cx = std::max(0, x0); cx <= std::min(w_ - 1, x1); ++cx) {
                if (cells_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(cx)]) return true;
            }
        }
        return false;
    }

private:
    static int cell(float v) { return static_cast<int>(std::floor(v / kCell)); }
    void mark(float x, float y, float r) {
        const int x0 = cell(x - r), x1 = cell(x + r), y0 = cell(y - r), y1 = cell(y + r);
        for (int cy = std::max(0, y0); cy <= std::min(h_ - 1, y1); ++cy) {
            for (int cx = std::max(0, x0); cx <= std::min(w_ - 1, x1); ++cx) {
                const float mx = (static_cast<float>(cx) + 0.5f) * kCell, my = (static_cast<float>(cy) + 0.5f) * kCell;
                if (std::hypot(mx - x, my - y) <= r + kCell * 0.5f) {
                    cells_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(cx)] = true;
                }
            }
        }
    }
    int w_, h_;
    std::vector<bool> cells_;
};

// Towns (rt3-clone-spec §15.6 views): each town's houses stand in rows
// along a grid of streets centred on it, one building to a plot, and what
// they are depends on the town's size and how near the centre they stand:
// cottages, two-storey houses and terraces outside, shops and offices
// (taller as the town grows) in the middle, and a church.
enum class Building : std::uint8_t { Cottage, TwoStorey, Terrace, Shop, Church };

struct Plot {
    float x, y;   // centre, tiles
    float dx, dy; // along the street
    float front;  // +1 or -1: which side (across) faces the street
    float hl, hw; // half length along the street, half depth
    Building kind;
    int floors;
    std::uint32_t seed;
    std::uint32_t row_seed; // shared along a block's row, so terraces match
};

struct TownLayout {
    std::vector<Plot> plots;
    std::vector<float> streets; // segments x0, y0, x1, y1 (centrelines)
};

constexpr float kRow = 0.9f;    // between streets along which houses face
constexpr float kCross = 2.0f;  // between the cross streets
constexpr float kPlot = 0.36f;  // plot width along the street
constexpr float kPlotMargin = 0.1f; // clear of the cross street at each end
constexpr float kStreetHalf = 0.045f;

TownLayout layout_towns(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Occupancy& taken,
                        const Camera& cam) {
    TownLayout out;
    const auto k = static_cast<float>(eco.cells_per_node());
    std::unordered_set<std::uint64_t> street_keys;
    const auto key = [](std::uint32_t town, int a, int b, int c, int d) {
        return (std::uint64_t{town} << 40) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a) & 0x3ff) << 30) ^
               (static_cast<std::uint64_t>(static_cast<std::uint32_t>(b) & 0x3ff) << 20) ^
               (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c) & 0x3ff) << 10) ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(d) & 0x3ff);
    };
    const auto lay_out = [&](std::uint32_t town_id, float xc, float yc, const std::vector<sim::SiteId>& sites, std::int64_t town_size) {
        // Rows run east-west or north-south, by town.
        const bool rows_ns = unit(hash(town_id, 5, 77)) < 0.5f;
        const auto world = [&](float a, float c, float& x, float& y) {
            x = rows_ns ? xc + c : xc + a;
            y = rows_ns ? yc + a : yc + c;
        };
        const float dx = rows_ns ? 0.0f : 1.0f, dy = rows_ns ? 1.0f : 0.0f;
        const auto street = [&](float a0, float c0, float a1, float c1, std::uint64_t id) {
            if (!street_keys.insert(id).second) return;
            float x0, y0, x1, y1;
            world(a0, c0, x0, y0);
            world(a1, c1, x1, y1);
            out.streets.insert(out.streets.end(), {x0, y0, x1, y1});
        };
        const std::size_t first = out.plots.size();
        // The town's buildings fill the plots nearest its centre (a little
        // jittered, so the edge is ragged, with the odd plot left as a
        // garden), skipping any the railway, water or industry has.
        std::int64_t wanted = 0;
        for (const sim::SiteId id : sites) {
            const sim::Site& site = eco.sites()[id];
            if (!site.closed && industries.get(site.type).kind == sim::IndustryKind::House) wanted += std::max(1, site.level);
        }
        wanted = std::min<std::int64_t>(wanted, 80);
        if (wanted == 0) return;
        const int plots_per_block = static_cast<int>((kCross - 2 * kPlotMargin) / kPlot);
        const int rows = 2 + static_cast<int>(std::sqrt(static_cast<float>(wanted)) * 0.6f);
        const int blocks = 1 + static_cast<int>(std::sqrt(static_cast<float>(wanted)) * 0.35f);
        struct Candidate {
            float rank;
            int row, side, block, slot;
        };
        std::vector<Candidate> candidates;
        for (int row = -rows; row <= rows; ++row)
            for (int side = 0; side < 2; ++side)
                for (int block = -blocks; block < blocks; ++block)
                    for (int slot = 0; slot < plots_per_block; ++slot) {
                        const float a = static_cast<float>(block) * kCross + kPlotMargin + (static_cast<float>(slot) + 0.5f) * kPlot;
                        const float c = static_cast<float>(row) * kRow + (side == 1 ? 0.3f : -0.3f);
                        const std::uint32_t h = hash(town_id, static_cast<std::uint32_t>((row + 64) * 2 + side),
                                                     static_cast<std::uint32_t>((block + 64) * 16 + slot));
                        if (unit(h) < 0.12f) continue; // a garden
                        candidates.push_back({std::hypot(a, c) + unit(h >> 8) * 0.9f, row, side, block, slot});
                    }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& l, const Candidate& r) {
            if (l.rank != r.rank) return l.rank < r.rank;
            return std::tie(l.row, l.side, l.block, l.slot) < std::tie(r.row, r.side, r.block, r.slot);
        });
        std::int64_t placed = 0;
        for (const Candidate& cand : candidates) {
            if (placed >= wanted) break;
            const float side = cand.side == 1 ? 1.0f : -1.0f;
            const std::uint32_t h = hash(town_id, static_cast<std::uint32_t>((cand.row + 64) * 2 + cand.side),
                                         static_cast<std::uint32_t>((cand.block + 64) * 16 + cand.slot + 7777));
            Plot p{};
            p.hw = 0.11f + 0.04f * unit(h >> 8);
            p.hl = kPlot * 0.5f - 0.04f;
            const float pa = static_cast<float>(cand.block) * kCross + kPlotMargin + (static_cast<float>(cand.slot) + 0.5f) * kPlot;
            const float pc = static_cast<float>(cand.row) * kRow + side * (kStreetHalf + 0.04f + p.hw);
            world(pa, pc, p.x, p.y);
            if (taken.blocked(p.x, p.y, p.hl + 0.02f) || on_water(p.x, p.y, cam)) continue;
            p.dx = dx;
            p.dy = dy;
            // The street is towards the row line: across is (-dy, dx).
            p.front = -side;
            p.seed = h;
            p.row_seed = hash(town_id, static_cast<std::uint32_t>((cand.row + 64) * 2 + cand.side), static_cast<std::uint32_t>(cand.block + 500));
            out.plots.push_back(p);
            ++placed;
            // The street it faces, the length of the block, and the cross
            // streets at the block's ends.
            const float b0 = static_cast<float>(cand.block) * kCross, b1 = b0 + kCross, rc = static_cast<float>(cand.row) * kRow;
            street(b0, rc, b1, rc, key(town_id, cand.row, 0, cand.block, 1));
            street(b0, rc, b0, rc + side * kRow, key(town_id, cand.row * 2 + cand.side, 0, cand.block, 2));
            street(b1, rc, b1, rc + side * kRow, key(town_id, cand.row * 2 + cand.side, 0, cand.block + 1, 2));
        }
        // What each building is.
        const float centre_reach = 0.9f + static_cast<float>(town_size) / 30.0f;
        std::size_t church = out.plots.size();
        float church_d = 1e9f;
        for (std::size_t i = first; i < out.plots.size(); ++i) {
            Plot& p = out.plots[i];
            const float d = std::hypot(p.x - xc, p.y - yc);
            const float r = unit(p.seed >> 4), row_r = unit(p.row_seed);
            if (town_size >= 10 && d < centre_reach) {
                p.kind = Building::Shop;
                p.floors = 2 + static_cast<int>(std::min<std::int64_t>(3, town_size / 25)) - (r < 0.3f ? 1 : 0);
                p.hl = kPlot * 0.5f - 0.01f;
            } else if (row_r < 0.3f && town_size >= 6) {
                p.kind = Building::Terrace; // the row's plots join up
                p.floors = 2;
                p.hl = kPlot * 0.5f;
            } else if (town_size >= 4 && r < 0.45f) {
                p.kind = Building::TwoStorey;
                p.floors = 2;
            } else {
                p.kind = Building::Cottage;
                p.floors = 1;
            }
            if (town_size >= 5 && std::fabs(d - 0.8f) < church_d) {
                church_d = std::fabs(d - 0.8f);
                church = i;
            }
        }
        if (church < out.plots.size()) {
            out.plots[church].kind = Building::Church;
            out.plots[church].floors = 1;
        }
    };
    std::vector<bool> in_town(eco.sites().size(), false);
    for (std::size_t t = 0; t < eco.towns().size(); ++t) {
        const sim::Town& town = eco.towns()[t];
        for (const sim::SiteId id : town.houses) in_town[id] = true;
        lay_out(static_cast<std::uint32_t>(t), (static_cast<float>(town.cx) + 0.5f) * k, (static_cast<float>(town.cy) + 0.5f) * k,
                town.houses, eco.town_houses(t));
    }
    // Houses outside any town: each its own hamlet.
    for (const sim::Site& site : eco.sites()) {
        if (in_town[site.id] || industries.get(site.type).kind != sim::IndustryKind::House) continue;
        lay_out(0x8000u + site.id, (static_cast<float>(site.cx) + 0.5f) * k, (static_cast<float>(site.cy) + 0.5f) * k, {site.id}, site.level);
    }
    return out;
}

// Streets on the ground: dirt, then gravel, then asphalt. Inside GL_QUADS.
void draw_streets(const TownLayout& layout, std::int32_t year, const Camera& cam) {
    if (year < 1890) glColor3f(0.6f, 0.52f, 0.4f);
    else if (year < 1930) glColor3f(0.58f, 0.56f, 0.52f);
    else glColor3f(0.33f, 0.33f, 0.35f);
    glNormal3f(0, 0, 1);
    for (std::size_t i = 0; i + 3 < layout.streets.size(); i += 4) {
        const float x0 = layout.streets[i], y0 = layout.streets[i + 1], x1 = layout.streets[i + 2], y1 = layout.streets[i + 3];
        const float len = std::hypot(x1 - x0, y1 - y0);
        if (len <= 0) continue;
        const float ux = (x1 - x0) / len, uy = (y1 - y0) / len, qx = -uy * kStreetHalf, qy = ux * kStreetHalf;
        // In short pieces, so the street follows the ground; a little past
        // each end so crossings meet.
        const int pieces = std::max(1, static_cast<int>(std::ceil(len / 0.25f)));
        for (int k = 0; k < pieces; ++k) {
            const float t0 = static_cast<float>(k) / static_cast<float>(pieces) * len - (k == 0 ? kStreetHalf : 0.0f);
            const float t1 = static_cast<float>(k + 1) / static_cast<float>(pieces) * len + (k == pieces - 1 ? kStreetHalf : 0.0f);
            const float ax = x0 + ux * t0, ay = y0 + uy * t0, bx = x0 + ux * t1, by = y0 + uy * t1;
            ground_vertex(ax - qx, ay - qy, cam);
            ground_vertex(bx - qx, by - qy, cam);
            ground_vertex(bx + qx, by + qy, cam);
            ground_vertex(ax + qx, ay + qy, cam);
        }
    }
}

// Windows on a building's long walls, a row to each floor, and a door on
// the street side. Inside GL_QUADS.
void windows(const Plot& p, float z, float floor_h, float wall_h) {
    const float glass[3] = {0.2f, 0.23f, 0.3f}, door[3] = {0.32f, 0.2f, 0.12f};
    const float qx = -p.dy, qy = p.dx;
    const int per_floor = std::max(1, static_cast<int>(p.hl * 2 / 0.075f));
    const float ww = std::min(0.014f, p.hl / static_cast<float>(per_floor) * 0.45f), wh = floor_h * 0.2f;
    for (const float sgn : {-1.0f, 1.0f}) {
        const float off = p.hw + 0.003f;
        glNormal3f(qx * sgn, qy * sgn, 0);
        for (int f = 0; f < p.floors; ++f) {
            const float zc = z + floor_h * (static_cast<float>(f) + 0.55f);
            if (zc + wh > z + wall_h) break;
            for (int i = 0; i < per_floor; ++i) {
                const float t = -p.hl + (static_cast<float>(i) + 0.5f) * 2 * p.hl / static_cast<float>(per_floor);
                const bool is_door = f == 0 && sgn == p.front && i == per_floor / 2;
                glColor3fv(is_door ? door : glass);
                const float cx = p.x + p.dx * t + qx * off * sgn, cy = p.y + p.dy * t + qy * off * sgn;
                const float z0 = is_door ? z : zc - wh, z1 = is_door ? z + floor_h * 0.7f : zc + wh;
                // Wound to face outward on either side.
                const float ex = p.dx * ww * sgn, ey = p.dy * ww * sgn;
                glVertex3f(cx + ex, cy + ey, z0);
                glVertex3f(cx - ex, cy - ey, z0);
                glVertex3f(cx - ex, cy - ey, z1);
                glVertex3f(cx + ex, cy + ey, z1);
            }
        }
    }
}

void draw_building(const Plot& p, const Camera& cam) {
    // Set into the ground on a slope: the lowest corner's height.
    const float qx = -p.dy * p.hw, qy = p.dx * p.hw, ax = p.dx * p.hl, ay = p.dy * p.hl;
    const float z = std::min({cam.ground_z(p.x - ax - qx, p.y - ay - qy), cam.ground_z(p.x + ax - qx, p.y + ay - qy),
                              cam.ground_z(p.x + ax + qx, p.y + ay + qy), cam.ground_z(p.x - ax + qx, p.y - ay + qy)}) - 0.01f;
    static constexpr float kWalls[][3] = {{0.9f, 0.86f, 0.74f}, {0.94f, 0.93f, 0.9f}, {0.72f, 0.4f, 0.3f},
                                          {0.8f, 0.74f, 0.6f},  {0.86f, 0.8f, 0.55f}, {0.66f, 0.64f, 0.6f}};
    static constexpr float kRoofs[][3] = {{0.62f, 0.22f, 0.16f}, {0.36f, 0.37f, 0.42f}, {0.45f, 0.3f, 0.2f}, {0.55f, 0.28f, 0.2f}};
    const std::uint32_t pick = p.kind == Building::Terrace ? p.row_seed : p.seed;
    const float* wall = kWalls[(pick >> 3) % 6];
    const float* roof = kRoofs[(pick >> 9) % 4];
    const float floor_h = 0.085f;
    switch (p.kind) {
    case Building::Cottage:
    case Building::TwoStorey:
    case Building::Terrace: {
        const float h = floor_h * static_cast<float>(p.floors) + 0.02f;
        box(p.x, p.y, z, p.dx, p.dy, p.hl, p.hw, h, wall, wall);
        gable(p.x, p.y, z + h, p.dx, p.dy, p.hl * (p.kind == Building::Terrace ? 1.0f : 1.06f), p.hw * 1.12f, p.hw * 0.75f, roof, wall);
        windows(p, z, floor_h, h);
        // A chimney stack on the ridge.
        const float dark[3] = {0.4f, 0.3f, 0.26f};
        const float along = p.hl * 0.6f * (unit(p.seed >> 12) < 0.5f ? -1.0f : 1.0f);
        box(p.x + p.dx * along, p.y + p.dy * along, z + h, p.dx, p.dy, 0.012f, 0.018f, p.hw * 0.75f + 0.03f, dark, dark);
        break;
    }
    case Building::Shop: {
        static constexpr float kShopWalls[][3] = {{0.7f, 0.42f, 0.32f}, {0.8f, 0.76f, 0.68f}, {0.62f, 0.56f, 0.5f}, {0.85f, 0.8f, 0.7f}};
        static constexpr float kAwnings[][3] = {{0.7f, 0.15f, 0.12f}, {0.15f, 0.45f, 0.25f}, {0.2f, 0.3f, 0.6f}, {0.8f, 0.65f, 0.2f}};
        const float* shop_wall = kShopWalls[(p.seed >> 3) % 4];
        const float* awning = kAwnings[(p.seed >> 7) % 4];
        const float h = floor_h * static_cast<float>(p.floors) + 0.025f;
        const float top[3] = {0.4f, 0.4f, 0.42f};
        box(p.x, p.y, z, p.dx, p.dy, p.hl, p.hw, h, shop_wall, top);
        // A parapet round the flat roof.
        box(p.x, p.y, z + h, p.dx, p.dy, p.hl, p.hw, 0.015f, shop_wall, shop_wall);
        windows(p, z, floor_h, h);
        // An awning over the shop front.
        const float fx = -p.dy * p.front, fy = p.dx * p.front;
        const float wz = z + floor_h * 0.95f, ox = p.x + fx * p.hw, oy = p.y + fy * p.hw;
        glColor3fv(awning);
        glNormal3f(fx, fy, 1.2f);
        glVertex3f(ox - p.dx * p.hl, oy - p.dy * p.hl, wz);
        glVertex3f(ox + p.dx * p.hl, oy + p.dy * p.hl, wz);
        glVertex3f(ox + p.dx * p.hl + fx * 0.04f, oy + p.dy * p.hl + fy * 0.04f, wz - 0.025f);
        glVertex3f(ox - p.dx * p.hl + fx * 0.04f, oy - p.dy * p.hl + fy * 0.04f, wz - 0.025f);
        break;
    }
    case Building::Church: {
        const float stone[3] = {0.78f, 0.75f, 0.68f}, slate[3] = {0.33f, 0.34f, 0.38f};
        // The nave along the street, the tower and spire at one end.
        const float nave_hl = p.hl * 0.95f, nave_hw = std::min(p.hw, 0.1f);
        box(p.x, p.y, z, p.dx, p.dy, nave_hl, nave_hw, 0.12f, stone, stone);
        gable(p.x, p.y, z + 0.12f, p.dx, p.dy, nave_hl, nave_hw * 1.1f, 0.08f, slate, stone);
        const float tx = p.x + p.dx * (nave_hl - 0.04f), ty = p.y + p.dy * (nave_hl - 0.04f);
        box(tx, ty, z, p.dx, p.dy, 0.04f, 0.04f, 0.26f, stone, slate);
        cylinder(tx, ty, z + 0.26f, 0.045f, 0.2f, slate, slate, 0.002f);
        break;
    }
    }
}

// A station (rt3-clone-spec §15.6 views) at (x, y), rail height z, its
// track along (dx, dy), `s` times life size, trimmed in its owner's colour.
// Small: one platform and a wooden depot with a lean-to canopy. Medium:
// platforms both sides, a brick two-storey house and a canopy on posts.
// Large: longer platforms with canopies, a stone hall with wings and a clock
// tower, and a footbridge over the tracks. The house stands on the -across
// side, (dy, -dx). Inside GL_QUADS.
void draw_station(sim::StationSize size, float x, float y, float z, float dx, float dy, float s, const float* trim,
                  std::uint32_t seed) {
    const float qx = -dy, qy = dx;
    const float stone[3] = {0.72f, 0.7f, 0.64f}, edge[3] = {0.92f, 0.86f, 0.45f}, post[3] = {0.25f, 0.25f, 0.27f};
    const float slate[3] = {0.33f, 0.34f, 0.38f}, lamp[3] = {1.0f, 0.9f, 0.55f}, white[3] = {0.95f, 0.95f, 0.92f};
    const bool small = size == sim::StationSize::Small, large = size == sim::StationSize::Large;
    const float len = (large ? 0.5f : small ? 0.24f : 0.34f) * s; // half the platforms' length
    const float pw = 0.03f * s, ph = 0.03f;                         // half width, height
    const float off = 0.15f + pw;                                   // track centre to platform centre
    const auto at = [&](float along, float across, float& ox, float& oy) {
        ox = x + dx * along + qx * across;
        oy = y + dy * along + qy * across;
    };
    float cx, cy;
    // Platforms, a painted edge on the track side, and lamps.
    for (const float side : {-1.0f, 1.0f}) {
        if (small && side > 0) continue;
        at(0, off * side, cx, cy);
        box(cx, cy, z, dx, dy, len, pw, ph, stone, stone);
        at(0, (off - pw * 0.85f) * side, cx, cy);
        box(cx, cy, z, dx, dy, len, pw * 0.12f, ph + 0.001f, stone, edge);
        for (const float t : {-0.8f, -0.27f, 0.27f, 0.8f}) {
            at(len * t, (off - pw * 0.4f) * side, cx, cy);
            box(cx, cy, z + ph, dx, dy, 0.003f * s, 0.003f * s, 0.07f * s, post, post);
            box(cx, cy, z + ph + 0.07f * s, dx, dy, 0.007f * s, 0.007f * s, 0.01f * s, lamp, lamp);
        }
    }
    // A name board in the owner's colours at the near platform's end.
    at(-len * 0.92f, -(off + pw * 0.3f), cx, cy);
    for (const float d : {-0.025f, 0.025f}) {
        box(cx + dx * d * s, cy + dy * d * s, z + ph, dx, dy, 0.003f * s, 0.003f * s, 0.05f * s, post, post);
    }
    box(cx, cy, z + ph + 0.04f * s, dx, dy, 0.035f * s, 0.004f * s, 0.022f * s, trim, trim);
    // A canopy over a platform: posts along its middle and a shallow roof.
    const auto canopy = [&](float side, float half_len) {
        for (float t = -0.85f; t <= 0.86f; t += 0.425f) {
            at(half_len * t, off * side, cx, cy);
            box(cx, cy, z + ph, dx, dy, 0.004f * s, 0.004f * s, 0.075f * s, post, post);
        }
        at(0, off * side, cx, cy);
        box(cx, cy, z + ph + 0.075f * s, dx, dy, half_len, pw * 1.25f, 0.006f * s, trim, trim);
        gable(cx, cy, z + ph + 0.081f * s, dx, dy, half_len, pw * 1.3f, 0.012f * s, slate, trim);
    };
    const float back = off + pw + 0.012f * s; // the platform's far edge, where the house starts
    // A station building from the art, facing the track, in place of the
    // one drawn in code (platforms and canopies stay).
    const char* role = small ? "station_small" : large ? "station_large" : "station_medium";
    if (const models::Model* m = models::pick(role, seed)) {
        if (!small) canopy(1.0f, len * 0.8f);
        if (large) canopy(-1.0f, len * 0.8f);
        const float depth = (large ? 0.12f : small ? 0.08f : 0.1f) * s;
        at(0, -(back + depth / 2), cx, cy);
        glEnd();
        models::draw(*m, cx, cy, z, qx, qy, len * (large ? 1.6f : 1.1f), depth);
        glBegin(GL_QUADS);
        return;
    }
    if (small) {
        // A wooden depot, a lean-to canopy out over the platform.
        const float timber[3] = {0.6f, 0.45f, 0.3f};
        Plot p{};
        p.hl = len * 0.55f;
        p.hw = 0.04f * s;
        at(-len * 0.1f, -(back + p.hw), p.x, p.y);
        p.dx = dx; p.dy = dy; p.front = 1.0f; p.floors = 1; p.seed = seed;
        const float h = 0.07f * s;
        box(p.x, p.y, z, dx, dy, p.hl, p.hw, h, timber, timber);
        gable(p.x, p.y, z + h, dx, dy, p.hl * 1.05f, p.hw * 1.15f, p.hw * 0.7f, trim, timber);
        windows(p, z + ph * 0.5f, h * 0.85f, h);
        // The lean-to: from the wall's top down to posts at the platform's edge.
        float fx, fy;
        at(-len * 0.1f, -(back - 0.002f), fx, fy);
        float ex, ey;
        at(-len * 0.1f, -(off - pw * 0.5f), ex, ey);
        for (const float t : {-0.9f, 0.0f, 0.9f}) {
            box(ex + dx * p.hl * t, ey + dy * p.hl * t, z + ph, dx, dy, 0.003f * s, 0.003f * s, 0.05f * s, post, post);
        }
        glColor3fv(trim);
        glNormal3f(qx * 0.3f, qy * 0.3f, 1.0f);
        glVertex3f(fx - dx * p.hl, fy - dy * p.hl, z + h * 0.95f);
        glVertex3f(ex - dx * p.hl, ey - dy * p.hl, z + ph + 0.05f * s);
        glVertex3f(ex + dx * p.hl, ey + dy * p.hl, z + ph + 0.05f * s);
        glVertex3f(fx + dx * p.hl, fy + dy * p.hl, z + h * 0.95f);
        return;
    }
    canopy(1.0f, len * 0.8f);
    if (!large) {
        // A two-storey brick station house.
        const float brick[3] = {0.66f, 0.37f, 0.28f};
        Plot p{};
        p.hl = len * 0.55f;
        p.hw = 0.05f * s;
        at(0, -(back + p.hw), p.x, p.y);
        p.dx = dx; p.dy = dy; p.front = 1.0f; p.floors = 2; p.seed = seed;
        const float floor_h = 0.055f * s, h = floor_h * 2 + 0.01f * s;
        box(p.x, p.y, z, dx, dy, p.hl, p.hw, h, brick, brick);
        gable(p.x, p.y, z + h, dx, dy, p.hl * 1.04f, p.hw * 1.12f, p.hw * 0.8f, slate, brick);
        box(p.x, p.y, z + h - 0.008f * s, dx, dy, p.hl * 1.01f, p.hw * 1.01f, 0.008f * s, trim, trim); // a band under the eaves
        windows(p, z + ph * 0.5f, floor_h, h);
        return;
    }
    canopy(-1.0f, len * 0.8f);
    // A stone hall with two wings and a clock tower.
    const float hall[3] = {0.8f, 0.76f, 0.66f};
    const float floor_h = 0.055f * s;
    Plot p{};
    p.hl = len * 0.3f;
    p.hw = 0.06f * s;
    at(0, -(back + p.hw), p.x, p.y);
    p.dx = dx; p.dy = dy; p.front = 1.0f; p.floors = 3; p.seed = seed;
    const float h = floor_h * 3 + 0.012f * s;
    box(p.x, p.y, z, dx, dy, p.hl, p.hw, h, hall, slate);
    box(p.x, p.y, z + h, dx, dy, p.hl, p.hw, 0.01f * s, trim, trim); // cornice
    windows(p, z + ph * 0.5f, floor_h, h);
    for (const float side : {-1.0f, 1.0f}) {
        Plot w = p;
        w.hl = len * 0.3f;
        w.hw = 0.05f * s;
        w.floors = 2;
        at(side * len * 0.6f, -(back + w.hw), w.x, w.y);
        const float wh = floor_h * 2 + 0.01f * s;
        box(w.x, w.y, z, dx, dy, w.hl, w.hw, wh, hall, hall);
        gable(w.x, w.y, z + wh, dx, dy, w.hl, w.hw * 1.1f, w.hw * 0.7f, slate, hall);
        windows(w, z + ph * 0.5f, floor_h, wh);
    }
    // The clock tower, faces on its sides, and a pyramid roof.
    const float tw = 0.035f * s, th = h + 0.1f * s;
    box(p.x, p.y, z, dx, dy, tw, tw, th, hall, hall);
    side_marks(p.x, p.y, -qx, -qy, tw, tw, 1, 0.016f * s, z + th - 0.045f * s, z + th - 0.013f * s, white);
    side_marks(p.x, p.y, dx, dy, tw, tw, 1, 0.016f * s, z + th - 0.045f * s, z + th - 0.013f * s, white);
    heap(p.x, p.y, z + th, tw * 1.15f, 0.05f * s, trim);
    // A footbridge: stair towers on both platforms and a deck between them.
    const float deck_z = z + ph + 0.12f * s;
    for (const float side : {-1.0f, 1.0f}) {
        at(len * 0.55f, off * side, cx, cy);
        box(cx, cy, z + ph, dx, dy, 0.018f * s, pw * 0.8f, deck_z - z - ph + 0.03f * s, trim, slate);
    }
    at(len * 0.55f, 0, cx, cy);
    box(cx, cy, deck_z, qx, qy, off, 0.014f * s, 0.03f * s, trim, slate);
}

// What stands where (railway, industries, towns) and how the towns are laid
// out, kept between frames: rebuilt only when the railway, the sites, the
// towns or the drawing scale change. `version` counts rebuilds.
struct Scene {
    std::uint64_t key = 0;
    std::uint64_t version = 0;
    std::optional<Occupancy> taken;     // railway and industries: houses keep off
    std::optional<Occupancy> wooded;    // that and the towns: trees keep off
    TownLayout towns;
};

std::uint64_t scene_key(const sim::Economy& eco, const sim::Railway& rw, const Camera& cam) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const sim::TrackEdge& e : rw.track().edges()) mix((e.removed ? 1u : 0u) | (e.double_track ? 2u : 0u) | (static_cast<unsigned>(e.kind) << 2));
    for (const sim::Station& st : rw.stations()) mix((static_cast<std::uint64_t>(st.node) << 4) | static_cast<std::uint64_t>(st.size));
    mix(rw.station_buildings().size());
    mix(rw.service_buildings().size());
    for (const sim::Site& site : eco.sites()) mix((static_cast<std::uint64_t>(site.level) << 1) | (site.closed ? 1u : 0u));
    mix(static_cast<std::uint64_t>(std::lround(object_scale(cam) * 4)));
    mix(cam.terrain ? static_cast<std::uint64_t>(cam.terrain->width()) : 0);
    return h;
}

const Scene& scene(const sim::Economy& eco, const sim::IndustryRegistry& industries, const sim::Railway& rw, const Camera& cam) {
    static Scene sc;
    const std::uint64_t key = scene_key(eco, rw, cam);
    if (sc.taken && sc.key == key) return sc;
    const auto k = static_cast<float>(eco.cells_per_node());
    const int width = cam.terrain ? cam.terrain->width() : eco.width() * eco.cells_per_node();
    const int height = cam.terrain ? cam.terrain->height() : eco.height() * eco.cells_per_node();
    sc.taken.emplace(rw, cam, width, height);
    for (const sim::Site& site : eco.sites()) {
        if (site.closed || industries.get(site.type).kind == sim::IndustryKind::House) continue;
        sc.taken->mark_square((static_cast<float>(site.cx) + 0.5f) * k, (static_cast<float>(site.cy) + 0.5f) * k, 0.4f * k);
    }
    sc.towns = layout_towns(eco, industries, *sc.taken, cam);
    sc.wooded = sc.taken;
    for (const Plot& p : sc.towns.plots) sc.wooded->mark_disc(p.x, p.y, 0.35f);
    for (std::size_t i = 0; i + 3 < sc.towns.streets.size(); i += 4) {
        const float x0 = sc.towns.streets[i], y0 = sc.towns.streets[i + 1], x1 = sc.towns.streets[i + 2], y1 = sc.towns.streets[i + 3];
        const int steps = std::max(1, static_cast<int>(std::hypot(x1 - x0, y1 - y0) / 0.2f));
        for (int j = 0; j <= steps; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(steps);
            sc.wooded->mark_disc(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 0.12f);
        }
    }
    sc.key = key;
    ++sc.version;
    return sc;
}

// Smooth noise over the map (not wrapping), `cells` tiles to a cell.
float map_noise(float x, float y, float cells, std::uint32_t seed) {
    const float fx = x / cells, fy = y / cells;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const auto at = [&](int cx, int cy) { return unit(hash(static_cast<std::uint32_t>(cx + 4096), static_cast<std::uint32_t>(cy + 4096), seed)); };
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

// A cone or frustum of six sides without a base, from radius r0 at z0 to r1
// at z1. Inside GL_QUADS.
void frustum(float x, float y, float z0, float r0, float z1, float r1, const float* colour) {
    constexpr int kSides = 6;
    glColor3fv(colour);
    for (int i = 0; i < kSides; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
        const float am = (a0 + a1) / 2;
        glNormal3f(std::cos(am), std::sin(am), (r0 - r1) / std::max(0.001f, z1 - z0));
        glVertex3f(x + r0 * std::cos(a0), y + r0 * std::sin(a0), z0);
        glVertex3f(x + r0 * std::cos(a1), y + r0 * std::sin(a1), z0);
        glVertex3f(x + r1 * std::cos(a1), y + r1 * std::sin(a1), z1);
        glVertex3f(x + r1 * std::cos(a0), y + r1 * std::sin(a0), z1);
    }
}

// The trees of one 32x32-tile chunk: woods where the noise runs high, the
// odd tree elsewhere; conifers (more of them higher up) and broadleaves.
constexpr int kTreeChunk = 32;

void build_tree_chunk(const sim::Terrain& t, const Occupancy& wooded, const Camera& cam, int cx0, int cy0) {
    const float trunk[3] = {0.36f, 0.26f, 0.17f};
    struct ArtTree {
        const models::Model* model;
        float x, y, z, fx, fy, height;
    };
    std::vector<ArtTree> art;
    glBegin(GL_QUADS);
    for (int ty = cy0; ty < std::min(cy0 + kTreeChunk, t.height()); ++ty) {
        for (int tx = cx0; tx < std::min(cx0 + kTreeChunk, t.width()); ++tx) {
            const sim::GroundType g = t.ground(tx, ty);
            if (g == sim::GroundType::Water || g == sim::GroundType::Desert) continue;
            const auto fx = static_cast<float>(tx), fy = static_cast<float>(ty);
            const float wood = map_noise(fx, fy, 14.0f, 61) * 0.7f + map_noise(fx, fy, 5.0f, 62) * 0.3f;
            const std::uint32_t th = hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), 7);
            int n = 0;
            if (g == sim::GroundType::Forest) n = 5;
            else if (g == sim::GroundType::Rock || g == sim::GroundType::Snow) n = unit(th) < 0.12f ? 1 : 0;
            else if (wood > 0.66f) n = 4;
            else if (wood > 0.58f) n = 2;
            else if (unit(th) < 0.035f) n = 1;
            for (int i = 0; i < n; ++i) {
                const std::uint32_t hh = hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), static_cast<std::uint32_t>(i + 11));
                const float x = fx + 0.1f + 0.8f * unit(hh), y = fy + 0.1f + 0.8f * unit(hh >> 16);
                if (wooded.blocked(x, y, 0.08f)) continue;
                const float base = cam.ground_z(x, y) - 0.01f;
                const float size = 0.65f + 0.4f * unit(hh >> 8);
                const float shade = 0.82f + 0.3f * unit(hh >> 4);
                // Stands of conifers among broadleaf woods, and the odd one mixed in.
                const float pick = unit(hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), static_cast<std::uint32_t>(i + 211)));
                const bool conifer = map_noise(x, y, 18.0f, 63) > 0.58f ? pick < 0.85f : pick < 0.12f;
                // A tree from the art where there is some, drawn after this batch.
                if (const models::Model* m = models::pick(conifer ? "tree_conifer" : "tree_broadleaf", hh >> 3)) {
                    const float yaw = unit(hh >> 6) * 6.2831853f;
                    art.push_back({m, x, y, base + 0.01f, std::cos(yaw), std::sin(yaw), 0.42f * size});
                    continue;
                }
                box(x, y, base, 1, 0, 0.012f * size, 0.012f * size, 0.1f * size, trunk, trunk);
                if (conifer) {
                    const float green[3] = {0.1f * shade, 0.27f * shade, 0.13f * shade};
                    const float tip[3] = {0.12f * shade, 0.31f * shade, 0.15f * shade};
                    frustum(x, y, base + 0.05f * size, 0.12f * size, base + 0.3f * size, 0.0f, green);
                    frustum(x, y, base + 0.2f * size, 0.09f * size, base + 0.45f * size, 0.0f, tip);
                } else {
                    const float tint = unit(hh >> 12);
                    const float leaf[3] = {(0.2f + 0.12f * tint) * shade, (0.4f + 0.08f * tint) * shade, 0.14f * shade};
                    const float top[3] = {leaf[0] * 1.1f, leaf[1] * 1.1f, leaf[2] * 1.1f};
                    const float r = 0.12f * size;
                    frustum(x, y, base + 0.09f * size, r * 0.45f, base + 0.2f * size, r, leaf);
                    frustum(x, y, base + 0.2f * size, r, base + 0.3f * size, r * 0.85f, leaf);
                    frustum(x, y, base + 0.3f * size, r * 0.85f, base + 0.38f * size, 0.0f, top);
                }
            }
        }
    }
    glEnd();
    for (const ArtTree& a : art) models::draw_to_height(*a.model, a.x, a.y, a.z, a.fx, a.fy, a.height);
}

// The chunks near the camera, each a display list built when first seen or
// after the scene changes (a few a frame, so building track never stalls).
void draw_trees(const sim::Terrain& t, const Scene& sc, const Camera& cam) {
    struct Chunk {
        GLuint list = 0;
        std::uint64_t version = 0;
        std::uint64_t terrain = 0;
    };
    static std::vector<Chunk> chunks;
    static int across = 0, down = 0;
    across = (t.width() + kTreeChunk - 1) / kTreeChunk;
    down = (t.height() + kTreeChunk - 1) / kTreeChunk;
    if (chunks.size() != static_cast<std::size_t>(across * down)) {
        for (Chunk& c : chunks) if (c.list != 0) glDeleteLists(c.list, 1);
        chunks.assign(static_cast<std::size_t>(across * down), Chunk{});
    }
    static std::uint64_t terrain = 0;
    static const sim::Terrain* terrain_of = nullptr;
    if (terrain_of != &t || terrain == 0) {
        terrain = terrain_key(t, cam);
        terrain_of = &t;
    }
    const float reach = std::min(cam.distance * 3.0f + 30.0f, 220.0f);
    int builds = 0;
    for (int cy = 0; cy < down; ++cy) {
        for (int cx = 0; cx < across; ++cx) {
            const float mx = (static_cast<float>(cx) + 0.5f) * kTreeChunk, my = (static_cast<float>(cy) + 0.5f) * kTreeChunk;
            if (std::hypot(mx - cam.target_x, my - cam.target_y) > reach + kTreeChunk) continue;
            Chunk& c = chunks[static_cast<std::size_t>(cy * across + cx)];
            const bool stale = c.list == 0 || c.version != sc.version || c.terrain != terrain;
            if (stale && (builds < 6 || c.list == 0)) {
                if (c.list == 0) c.list = glGenLists(1);
                glNewList(c.list, GL_COMPILE);
                build_tree_chunk(t, *sc.wooded, cam, cx * kTreeChunk, cy * kTreeChunk);
                glEndList();
                c.version = sc.version;
                c.terrain = terrain;
                ++builds;
            }
            glCallList(c.list);
        }
    }
}

} // namespace

void draw_terrain(const sim::Terrain& t, const Camera& cam) {
    if (!cam.three_d) {
        glBegin(GL_QUADS);
        for (int ty = 0; ty < t.height(); ++ty) {
            for (int tx = 0; tx < t.width(); ++tx) {
                shade(t, tx, ty, 400);
                const auto x = static_cast<float>(tx), y = static_cast<float>(ty);
                glVertex2f(x, y);
                glVertex2f(x + 1, y);
                glVertex2f(x + 1, y + 1);
                glVertex2f(x, y + 1);
            }
        }
        glEnd();
        return;
    }
    static TerrainMesh mesh;
    const std::uint64_t key = terrain_key(t, cam);
    if (mesh.list == 0 || mesh.key != key) {
        // Made now: a texture upload inside the list would only be recorded.
        ground_texture();
        water_texture();
        if (mesh.list == 0) mesh.list = glGenLists(1);
        glNewList(mesh.list, GL_COMPILE);
        build_terrain_mesh(t, cam);
        glEndList();
        mesh.key = key;
    }
    begin_lit();
    glCallList(mesh.list);
    end_lit();
}

void owner_rgb(sim::CompanyId owner, std::optional<sim::CompanyId> player, float& r, float& g, float& b) {
    static constexpr float kPalette[][3] = {
        {0.95f, 0.95f, 0.90f}, {0.90f, 0.35f, 0.20f}, {0.20f, 0.75f, 0.85f}, {0.80f, 0.30f, 0.75f},
        {0.90f, 0.80f, 0.20f}, {0.35f, 0.80f, 0.35f}, {0.40f, 0.50f, 0.95f}, {0.65f, 0.45f, 0.25f},
    };
    constexpr std::size_t kHues = sizeof(kPalette) / sizeof(kPalette[0]) - 1;
    const auto& c = owner == player ? kPalette[0] : kPalette[1 + owner % kHues];
    r = c[0];
    g = c[1];
    b = c[2];
}

void draw_railway(const sim::World& w, const Camera& cam) {
    const sim::Railway& rw = w.railway();
    const std::optional<sim::CompanyId> player = w.player_company();
    const sim::TrackNetwork& net = rw.track();
    // Close enough in 3D, track is drawn as ballast, sleepers and rails.
    const bool detailed = cam.three_d && cam.zoom >= 9.0f;
    if (detailed) track_3d(rw, cam, player);
    for (const sim::TrackEdge& e : net.edges()) {
        if (e.removed || detailed) continue;
        // Wider as the view closes in, in 3D.
        const float width = cam.three_d ? std::clamp(cam.zoom * 0.12f, 2.0f, 8.0f) : 2.0f;
        glLineWidth(e.double_track ? width * 2 : width);
        glBegin(GL_LINES);
        if (e.owner == player) {
            track_colour(e.kind, 1.0f);
        } else {
            // Rivals' track in their colour, darker in tunnels.
            float r, g, b;
            owner_rgb(e.owner, player, r, g, b);
            const float k = e.kind == sim::TrackKind::Tunnel ? 0.4f : 0.8f;
            glColor3f(r * k, g * k, b * k);
        }
        vertex_at(net.node(e.a).pos, net.node(e.a).z_mm, cam);
        vertex_at(net.node(e.b).pos, net.node(e.b).z_mm, cam);
        glEnd();
    }
    // Bridges stand on piers down to the ground (3D, from afar).
    if (cam.three_d && !detailed) {
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glColor3f(0.55f, 0.52f, 0.48f);
        for (const sim::TrackEdge& e : net.edges()) {
            if (e.removed || e.kind != sim::TrackKind::Bridge) continue;
            for (const sim::NodeId n : {e.a, e.b}) {
                const sim::TrackNode& node = net.node(n);
                vertex_at(node.pos, node.z_mm, cam, 0.0f);
                vertex(node.pos, cam);
            }
        }
        glEnd();
    }
    // Overhead wires: a thin copper line along electrified track.
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor3f(0.95f, 0.6f, 0.2f);
    for (const sim::TrackEdge& e : net.edges()) {
        if (!e.electrified || e.removed) continue;
        vertex_at(net.node(e.a).pos, net.node(e.a).z_mm, cam, kLift + 0.06f);
        vertex_at(net.node(e.b).pos, net.node(e.b).z_mm, cam, kLift + 0.06f);
    }
    glEnd();

    // Crossings: a white cross where lines meet at level, a blue bar where
    // one goes over the other.
    {
        const float k = 5.0f / cam.zoom; // five pixels, in tiles
        for (const sim::Crossing& c : rw.crossings()) {
            const float x = tiles(c.at.x_mm, cam), y = tiles(c.at.y_mm, cam);
            glLineWidth(2.0f);
            glBegin(GL_LINES);
            if (c.overpass) {
                glColor3f(0.4f, 0.75f, 1.0f);
                ground_vertex(x - k, y - k, cam);
                ground_vertex(x + k, y + k, cam);
            } else {
                glColor3f(1.0f, 1.0f, 1.0f);
                ground_vertex(x - k, y - k, cam);
                ground_vertex(x + k, y + k, cam);
                ground_vertex(x - k, y + k, cam);
                ground_vertex(x + k, y - k, cam);
            }
            glEnd();
        }
    }
    // Markers stay the same size on screen at any zoom.
    const float px = 1.0f / cam.zoom;
    if (cam.three_d) {
        // Buildings and trains as solid boxes, never smaller than a few pixels.
        const float s = object_scale(cam);
        begin_lit();
        glBegin(GL_QUADS);
        // The direction of the track at a node, for lining buildings up with it.
        const auto along = [&](sim::NodeId n, float& dx, float& dy) {
            dx = 1;
            dy = 0;
            for (sim::EdgeId id : net.edges_at(n)) {
                const sim::TrackEdge& e = net.edge(id);
                if (e.removed) continue;
                const sim::MapPoint a = net.node(e.a).pos, b = net.node(e.b).pos;
                const auto ex = static_cast<float>(b.x_mm - a.x_mm), ey = static_cast<float>(b.y_mm - a.y_mm);
                const float l = std::hypot(ex, ey);
                if (l > 0) {
                    dx = ex / l;
                    dy = ey / l;
                }
                return;
            }
        };
        const float slate[3] = {0.35f, 0.36f, 0.4f}, post[3] = {0.3f, 0.3f, 0.32f};
        for (const sim::Station& st : rw.stations()) {
            float trim[3];
            owner_rgb(st.owner, player, trim[0], trim[1], trim[2]);
            const sim::TrackNode& n = net.node(st.node);
            const float x = tiles(n.pos.x_mm, cam), y = tiles(n.pos.y_mm, cam), z = cam.z_of(n.z_mm);
            float dx, dy;
            along(st.node, dx, dy);
            draw_station(st.size, x, y, z, dx, dy, s, trim, hash(st.id, 3, 9));
        }
        for (const sim::StationBuilding& b : rw.station_buildings()) {
            const float x = tiles(b.pos.x_mm, cam), y = tiles(b.pos.y_mm, cam), z = cam.ground_z(x, y);
            switch (b.type) {
            case sim::StationBuildingType::PostOffice: {
                const float wall[3] = {0.85f, 0.82f, 0.75f}, roof[3] = {0.25f, 0.4f, 0.8f};
                barn(x, y, z, 1, 0, 0.08f * s, 0.06f * s, 0.08f * s, wall, roof);
                break;
            }
            case sim::StationBuildingType::Hotel: {
                const float wall[3] = {0.92f, 0.9f, 0.86f}, roof[3] = {0.3f, 0.3f, 0.45f};
                box(x, y, z, 1, 0, 0.09f * s, 0.07f * s, 0.2f * s, wall, roof);
                break;
            }
            case sim::StationBuildingType::Restaurant: {
                const float wall[3] = {0.88f, 0.8f, 0.65f}, roof[3] = {0.25f, 0.6f, 0.3f};
                barn(x, y, z, 1, 0, 0.07f * s, 0.05f * s, 0.07f * s, wall, roof);
                break;
            }
            case sim::StationBuildingType::Tavern: {
                const float wall[3] = {0.5f, 0.35f, 0.22f}, roof[3] = {0.55f, 0.2f, 0.15f};
                barn(x, y, z, 1, 0, 0.07f * s, 0.05f * s, 0.07f * s, wall, roof);
                break;
            }
            }
        }
        for (const sim::ServiceBuilding& b : rw.service_buildings()) {
            const sim::TrackNode& n = net.node(b.node);
            float dx, dy;
            along(b.node, dx, dy);
            // Set back from the track, on the side away from the station house.
            const float back = 0.2f + 0.08f * s;
            const float x = tiles(n.pos.x_mm, cam) - dy * back, y = tiles(n.pos.y_mm, cam) + dx * back;
            const float z = cam.ground_z(x, y);
            if (b.type == sim::ServiceType::ServiceTower) {
                // A water tank on legs.
                const float tank[3] = {0.3f, 0.35f, 0.42f};
                box(x, y, z, dx, dy, 0.012f * s, 0.012f * s, 0.16f * s, post, post);
                cylinder(x, y, z + 0.16f * s, 0.05f * s, 0.08f * s, tank, slate);
            } else {
                // An engine shed.
                const float wall[3] = {0.55f, 0.32f, 0.22f};
                barn(x + dx * 0.15f * s, y + dy * 0.15f * s, z, dx, dy, 0.16f * s, 0.07f * s, 0.09f * s, wall, slate);
            }
        }
        for (const sim::Train& t : rw.trains()) {
            if (!t.in_service()) continue;
            const Pose p = train_pose(rw, t);
            // The engine in its owner's livery, or flagged by its state.
            float c[3];
            owner_rgb(t.owner, player, c[0], c[1], c[2]);
            if (t.owner == player) {
                c[0] = 0.15f; c[1] = 0.42f; c[2] = 0.22f; // the player's in green
            }
            if (t.state == sim::TrainState::BrokenDown) {
                c[0] = 1.0f; c[1] = 0.55f; c[2] = 0.0f;
            } else if (t.yielding) {
                c[0] = 0.9f; c[1] = 0.15f; c[2] = 0.1f;
            }
            const float x = tiles(p.pos.x_mm, cam), y = tiles(p.pos.y_mm, cam), z = cam.z_of(p.z_mm) + 0.03f;
            const sim::LocomotiveType& loco = w.data().locomotives.get(t.loco);
            const Stock engine = loco.fuel == sim::Fuel::Steam ? Stock::Steam
                                 : loco.fuel == sim::Fuel::Diesel ? Stock::Diesel
                                                                  : Stock::Electric;
            const float gap = 0.012f * s;
            const std::uint32_t seed = hash(t.id, 17, 5);
            float back = stock(engine, x, y, z, p.dx, p.dy, s, c, seed) + gap;
            if (engine == Stock::Steam && t.speed_mm_per_tick > 0) {
                add_smoke(x - p.dx * 0.02f * s, y - p.dy * 0.02f * s, z + 0.155f * s, 0.016f * s);
            }
            std::uint32_t car_seed = seed;
            const auto behind = [&](Stock kind, const std::string& cargo = {}, const float* load = nullptr) {
                car_seed = hash(car_seed, 31, 7);
                back += stock(kind, x - p.dx * back, y - p.dy * back, z, p.dx, p.dy, s, c, car_seed, cargo, load) + gap;
            };
            for (const sim::Car& car : t.cars) {
                if (!car.cargo) continue; // empty cars are left behind
                const sim::CargoType& cargo = w.data().cargo.get(*car.cargo);
                if (cargo.cargo_class == sim::CargoClass::Express) {
                    behind(cargo.key == "mail" ? Stock::Mail : Stock::Coach, cargo.key);
                } else if (cargo.key == "coal" || cargo.key == "iron" || cargo.key == "bauxite") {
                    static constexpr float kCoal[3] = {0.07f, 0.07f, 0.08f}, kIron[3] = {0.45f, 0.24f, 0.16f},
                                           kBauxite[3] = {0.62f, 0.3f, 0.2f};
                    behind(Stock::Hopper, cargo.key, cargo.key == "coal" ? kCoal : cargo.key == "iron" ? kIron : kBauxite);
                } else {
                    behind(Stock::Boxcar, cargo.key);
                }
            }
            if (t.diner) behind(Stock::Diner);
            if (t.caboose) behind(Stock::Caboose);
        }
        glEnd();
        end_lit();
        return;
    }
    glBegin(GL_QUADS);
    for (const sim::Station& s : rw.stations()) {
        float r, g, b;
        owner_rgb(s.owner, player, r, g, b);
        glColor3f(r, g, b);
        square(net.node(s.node).pos, cam, 6 * px);
    }
    // Post offices blue, hotels white, restaurants green, taverns red; edged in the owner's colour.
    for (const sim::StationBuilding& b : rw.station_buildings()) {
        float r, g, bl;
        owner_rgb(b.owner, player, r, g, bl);
        glColor3f(r, g, bl);
        square(b.pos, cam, 4 * px);
        switch (b.type) {
        case sim::StationBuildingType::PostOffice: glColor3f(0.3f, 0.5f, 0.95f); break;
        case sim::StationBuildingType::Hotel: glColor3f(0.95f, 0.95f, 0.95f); break;
        case sim::StationBuildingType::Restaurant: glColor3f(0.3f, 0.85f, 0.35f); break;
        case sim::StationBuildingType::Tavern: glColor3f(0.9f, 0.3f, 0.25f); break;
        }
        square(b.pos, cam, 2.5f * px);
    }
    for (const sim::ServiceBuilding& b : rw.service_buildings()) {
        if (b.type == sim::ServiceType::ServiceTower) glColor3f(0.3f, 0.6f, 0.95f);
        else glColor3f(0.95f, 0.75f, 0.2f);
        square(net.node(b.node).pos, cam, 3 * px);
    }
    for (const sim::Train& t : rw.trains()) {
        if (!t.in_service()) continue;
        if (t.state == sim::TrainState::BrokenDown) glColor3f(1.0f, 0.55f, 0.0f);
        else if (t.state == sim::TrainState::Servicing) glColor3f(0.2f, 0.4f, 1.0f);
        else if (t.yielding) glColor3f(0.9f, 0.15f, 0.1f);
        else if (t.owner == player) glColor3f(0.1f, 0.1f, 0.1f);
        else {
            float r, g, b;
            owner_rgb(t.owner, player, r, g, b);
            glColor3f(r * 0.5f, g * 0.5f, b * 0.5f);
        }
        square(rw.train_position(t.id), cam, 4 * px);
    }
    glEnd();
}

void draw_analysis_overlay(const sim::World& w, MapOverlay mode, const Camera& cam) {
    const sim::Railway& rw = w.railway();
    const sim::TrackNetwork& net = rw.track();
    const sim::Balance& b = w.data().balance;
    const auto rating_rgb = [](sim::Rating r) {
        switch (r) {
        case sim::Rating::Loss: glColor3f(0.95f, 0.2f, 0.15f); break;
        case sim::Rating::Low: glColor3f(0.95f, 0.85f, 0.2f); break;
        case sim::Rating::Good: glColor3f(0.2f, 0.9f, 0.3f); break;
        }
    };
    const auto edge_line = [&](const sim::TrackEdge& e) {
        vertex_at(net.node(e.a).pos, net.node(e.a).z_mm, cam, kLift * 2);
        vertex_at(net.node(e.b).pos, net.node(e.b).z_mm, cam, kLift * 2);
    };
    switch (mode) {
    case MapOverlay::None:
    case MapOverlay::Cargo: return;
    case MapOverlay::Holdings: {
        const auto player = w.player_company();
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        for (const sim::TrackEdge& e : net.edges()) {
            if (e.removed) continue;
            float r, g, bl;
            owner_rgb(e.owner, player, r, g, bl);
            glColor3f(r, g, bl);
            edge_line(e);
        }
        glEnd();
        const float px = 1.0f / cam.zoom;
        glBegin(GL_QUADS);
        for (const sim::Station& s : rw.stations()) {
            float r, g, bl;
            owner_rgb(s.owner, player, r, g, bl);
            glColor3f(r, g, bl);
            square(net.node(s.node).pos, cam, 9 * px);
        }
        const sim::Economy& eco = w.economy();
        for (const sim::Site& s : eco.sites()) {
            if (!s.owner || s.closed) continue;
            float r, g, bl;
            owner_rgb(*s.owner, player, r, g, bl);
            glColor3f(r, g, bl);
            square(eco.node_centre(s.cx, s.cy), cam, 8 * px);
        }
        glEnd();
        return;
    }
    case MapOverlay::Grade: {
        const std::int64_t limit = std::max<std::int32_t>(1, b.track.default_max_grade_bp);
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        for (const sim::TrackEdge& e : net.edges()) {
            if (e.removed) continue;
            const std::int64_t grade = std::abs(net.grade_bp({e.id, true}));
            const float k = static_cast<float>(std::min<std::int64_t>(grade, limit)) / static_cast<float>(limit);
            glColor3f(0.2f + 0.75f * k, 0.9f - 0.7f * k, 0.2f);
            edge_line(e);
        }
        glEnd();
        return;
    }
    case MapOverlay::Traffic: {
        std::uint32_t busiest = 1;
        for (const sim::TrackEdge& e : net.edges())
            if (!e.removed) busiest = std::max(busiest, rw.traffic_last_month(e.id));
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        for (const sim::TrackEdge& e : net.edges()) {
            if (e.removed) continue;
            if (rw.needs_double_track(e.id)) {
                glColor3f(1.0f, 0.1f, 0.1f);
            } else {
                const float k = static_cast<float>(rw.traffic_last_month(e.id)) / static_cast<float>(busiest);
                if (k <= 0) glColor3f(0.25f, 0.25f, 0.3f);
                else glColor3f(0.4f + 0.6f * k, 0.4f + 0.45f * k, 0.4f - 0.3f * k);
            }
            edge_line(e);
        }
        glEnd();
        return;
    }
    case MapOverlay::Profit: {
        const float px = 1.0f / cam.zoom;
        glBegin(GL_QUADS);
        for (const sim::Station& s : rw.stations()) {
            rating_rgb(sim::rate(s.income_last_year(), s.income_months.size(), sim::station_cost(s.size, b), b));
            square(net.node(s.node).pos, cam, 9 * px);
        }
        for (const sim::Train& t : rw.trains()) {
            if (!t.in_service()) continue;
            rating_rgb(sim::rate(t.profit_last_year(), t.profit_months.size(), w.data().locomotives.get(t.loco).cost, b));
            square(rw.train_position(t.id), cam, 7 * px);
        }
        glEnd();
        return;
    }
    }
}

void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const sim::Railway& rw,
                std::int32_t year, const Camera& cam, std::optional<sim::CargoId> cargo) {
    const float px = 1.0f / cam.zoom;
    // Drawing is in map cells; a site sits at the centre of its economy node.
    const auto k = static_cast<float>(eco.cells_per_node());
    const auto cell_square = [&](std::int32_t cx, std::int32_t cy, float half) {
        const float x = (static_cast<float>(cx) + 0.5f) * k, y = (static_cast<float>(cy) + 0.5f) * k;
        ground_quad(x - half, y - half, x + half, y + half, cam);
    };
    // Which sites make or use the cargo on the price map: rings under them.
    if (cargo) {
        glBegin(GL_QUADS);
        for (const sim::Site& s : eco.sites()) {
            const sim::IndustryType& t = industries.get(s.type);
            const bool makes = std::find(t.outputs.begin(), t.outputs.end(), *cargo) != t.outputs.end();
            const bool uses = std::any_of(t.inputs.begin(), t.inputs.end(),
                                          [&](const sim::IndustryInput& i) { return i.cargo == *cargo; });
            if (!makes && !uses) continue;
            if (makes) glColor3f(0.9f, 0.1f, 0.1f);
            else glColor3f(0.1f, 0.85f, 0.2f);
            cell_square(s.cx, s.cy, std::max(0.45f * k, 6 * px));
        }
        glEnd();
    }
    if (cam.three_d) {
        const Scene& sc = scene(eco, industries, rw, cam);
        const TownLayout& towns = sc.towns;
        begin_lit();
        glBegin(GL_QUADS);
        draw_streets(towns, year, cam);
        // Houses come from the art where there is some (drawn after this
        // batch); the rest are drawn in code.
        const auto role = [](const Plot& p) {
            switch (p.kind) {
            case Building::Cottage:
            case Building::TwoStorey: return "house";
            case Building::Terrace: return "terrace";
            case Building::Shop: return "shop";
            case Building::Church: return "church";
            }
            return "";
        };
        const auto modelled = [&](const Plot& p) { return models::has(role(p)); };
        for (const Plot& p : towns.plots) {
            if (!modelled(p)) draw_building(p, cam);
        }
        for (const sim::Site& s : eco.sites()) {
            if (s.closed) continue;
            const sim::IndustryType& t = industries.get(s.type);
            const float x0 = (static_cast<float>(s.cx) + 0.5f) * k, y0 = (static_cast<float>(s.cy) + 0.5f) * k;
            if (t.kind == sim::IndustryKind::House) continue; // laid out by town, above
            const std::uint32_t seed = hash(static_cast<std::uint32_t>(s.cx), static_cast<std::uint32_t>(s.cy), 3);
            if (const models::Model* m = models::pick("industry:" + t.key, seed)) {
                glEnd();
                models::draw(*m, x0, y0, cam.ground_z(x0, y0) - 0.01f, 0, 1, 0.8f * k, 0.8f * k);
                glBegin(GL_QUADS);
            } else {
                industry_3d(t, x0, y0, k, seed, cam);
            }
        }
        glEnd();
        for (const Plot& p : towns.plots) {
            if (!modelled(p)) continue;
            const models::Model* m = models::pick(role(p), p.seed >> 5);
            const float qx = -p.dy * p.hw, qy = p.dx * p.hw, ax = p.dx * p.hl, ay = p.dy * p.hl;
            const float z = std::min({cam.ground_z(p.x - ax - qx, p.y - ay - qy), cam.ground_z(p.x + ax - qx, p.y + ay - qy),
                                      cam.ground_z(p.x + ax + qx, p.y + ay + qy), cam.ground_z(p.x - ax + qx, p.y - ay + qy)});
            // Its front to the street; the plot along the street is its
            // width. It may be stretched deeper (up to 1.8 times) to fill
            // the plot, but never squeezed.
            const float width = p.hl * 2.1f, sx = width / std::max(1e-4f, m->max[0] - m->min[0]);
            const float natural = sx * (m->max[1] - m->min[1]);
            const float depth = std::clamp(p.hw * 2.1f, natural, natural * 1.8f);
            models::draw(*m, p.x, p.y, z - 0.005f, -p.dy * p.front, p.dx * p.front, width, depth);
        }
        if (cam.terrain) draw_trees(*cam.terrain, sc, cam);
        end_lit();
        return;
    }
    glBegin(GL_QUADS);
    for (const sim::Site& s : eco.sites()) {
        const sim::IndustryType& t = industries.get(s.type);
        switch (t.kind) {
        case sim::IndustryKind::Raw: glColor3f(0.55f, 0.35f, 0.15f); break;
        case sim::IndustryKind::Processor: glColor3f(0.55f, 0.25f, 0.65f); break;
        case sim::IndustryKind::Sink: glColor3f(0.45f, 0.45f, 0.5f); break;
        case sim::IndustryKind::House: glColor3f(0.92f, 0.85f, 0.7f); break;
        case sim::IndustryKind::Port: glColor3f(0.95f, 0.6f, 0.15f); break; // stands out against the water
        case sim::IndustryKind::Warehouse: glColor3f(0.75f, 0.55f, 0.3f); break;
        }
        const float half = t.kind == sim::IndustryKind::House ? 0.2f + 0.02f * static_cast<float>(std::min(s.level, 10))
                                                                : 0.3f;
        cell_square(s.cx, s.cy, std::max(half * k, 2 * px));
    }
    glEnd();
}

void draw_price_overlay(const sim::Economy& eco, const sim::CargoType& cargo, const Camera& cam) {
    const auto base = static_cast<float>(std::max<std::int64_t>(1, cargo.base_price.whole_dollars()));
    const auto k = static_cast<float>(eco.cells_per_node()); // map cells per node
    const int cells = std::max(1, eco.cells_per_node());
    glBegin(GL_QUADS);
    for (std::int32_t y = 0; y < eco.height(); ++y) {
        for (std::int32_t x = 0; x < eco.width(); ++x) {
            // 0 at half the base price (red), 1 at one and a half times (green).
            const float t = std::clamp(static_cast<float>(eco.price(cargo.id, x, y)) / base - 0.5f, 0.0f, 1.0f);
            glColor4f(1.0f - t, t, 0.1f, 0.55f);
            const auto fx = static_cast<float>(x) * k, fy = static_cast<float>(y) * k;
            if (!cam.three_d) {
                ground_quad(fx, fy, fx + k, fy + k, cam);
                continue;
            }
            // Cell by cell, so the colour follows the ground.
            for (int j = 0; j < cells; ++j)
                for (int i = 0; i < cells; ++i) {
                    const float cx = fx + static_cast<float>(i), cy = fy + static_cast<float>(j);
                    ground_quad(cx, cy, cx + 1, cy + 1, cam);
                }
        }
    }
    glEnd();
    // Stock waiting in each cell, as dark dots sized by quantity.
    glBegin(GL_QUADS);
    glColor4f(0.05f, 0.05f, 0.05f, 0.8f);
    for (std::int32_t y = 0; y < eco.height(); ++y) {
        for (std::int32_t x = 0; x < eco.width(); ++x) {
            const std::int32_t s = eco.stock_milli(cargo.id, x, y);
            if (s < sim::kMilli / 4) continue;
            const float half = k * std::min(0.4f, 0.08f + 0.04f * static_cast<float>(s) / sim::kMilli);
            const float cx = (static_cast<float>(x) + 0.5f) * k, cy = (static_cast<float>(y) + 0.5f) * k;
            ground_quad(cx - half, cy - half, cx + half, cy + half, cam);
        }
    }
    glEnd();
}

void draw_town_names(const sim::Economy& eco, const Camera& cam, const sim::Balance::Towns& towns) {
    const float cell_mm = cam.mm_per_tile;
    for (std::size_t i = 0; i < eco.towns().size(); ++i) {
        const sim::Town& t = eco.towns()[i];
        const std::string label = t.name + " " + std::string(static_cast<std::size_t>(sim::town_stars(eco.town_houses(i), towns)), '*');
        float sx = 0, sy = 0;
        const sim::MapPoint c = eco.node_centre(t.cx, t.cy);
        // Over the town: above it in 3D, north of it in 2D.
        const bool shown = cam.three_d ? cam.to_screen(c, sx, sy)
                                       : cam.to_screen({c.x_mm, c.y_mm - static_cast<std::int64_t>(3.0f * cell_mm)}, sx, sy);
        if (!shown) continue;
        if (cam.three_d) sy -= 30;
        const auto w = static_cast<float>(text_width(label, 2));
        glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
        glBegin(GL_QUADS);
        glVertex2f(sx - w / 2 - 3, sy - 3);
        glVertex2f(sx + w / 2 + 3, sy - 3);
        glVertex2f(sx + w / 2 + 3, sy + 17);
        glVertex2f(sx - w / 2 - 3, sy + 17);
        glEnd();
        glColor3f(1.0f, 1.0f, 0.95f);
        draw_text(sx - w / 2, sy, label, 2);
    }
}

void draw_plan(sim::MapPoint start, const sim::TrackPlan& plan, const Camera& cam) {
    glLineWidth(plan.double_track ? 5.0f : 3.0f);
    glBegin(GL_LINES);
    sim::MapPoint prev = start;
    std::int64_t prev_z = cam.terrain ? cam.terrain->height_at_mm(start) : 0;
    for (std::size_t i = 0; i < plan.points.size(); ++i) {
        track_colour(plan.pieces[i].kind, 0.85f);
        const std::int64_t z = i < plan.rail_z_mm.size() ? plan.rail_z_mm[i] : prev_z;
        vertex_at(prev, prev_z, cam, kLift * 2);
        vertex_at(plan.points[i], z, cam, kLift * 2);
        prev = plan.points[i];
        prev_z = z;
    }
    glEnd();
}

void draw_line(sim::MapPoint a, sim::MapPoint b, const Camera& cam, float r, float g, float bl, float alpha) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor4f(r, g, bl, alpha);
    vertex(a, cam);
    vertex(b, cam);
    glEnd();
}

void draw_marker(sim::MapPoint p, const Camera& cam, float half_px, float r, float g, float b) {
    glBegin(GL_QUADS);
    glColor3f(r, g, b);
    square(p, cam, half_px / cam.zoom);
    glEnd();
}

void draw_territory_borders(const sim::TerritoryMap& map, const Camera& cam) {
    if (map.empty()) return;
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
            const sim::TerritoryId here = map.at(x, y);
            const auto edge = [&](sim::TerritoryId there, float x0, float y0, float x1, float y1) {
                if (there == here || there == sim::kNoTerritory) return;
                const bool closed = (here < map.territories.size() && map.territories[here].closed_border) ||
                                    (there < map.territories.size() && map.territories[there].closed_border);
                if (closed) glColor4f(0.85f, 0.1f, 0.1f, 0.9f);
                else glColor4f(0.15f, 0.1f, 0.25f, 0.75f);
                ground_vertex(x0, y0, cam);
                ground_vertex(x1, y1, cam);
            };
            const auto fx = static_cast<float>(x), fy = static_cast<float>(y);
            if (x + 1 < map.width) edge(map.at(x + 1, y), fx + 1, fy, fx + 1, fy + 1);
            if (y + 1 < map.height) edge(map.at(x, y + 1), fx, fy + 1, fx + 1, fy + 1);
        }
    }
    glEnd();
}

void draw_territory_names(const sim::TerritoryMap& map, const Camera& cam, const sim::Company* company) {
    if (map.empty()) return;
    // Centre of each territory's cells.
    std::vector<std::int64_t> sx(map.territories.size(), 0), sy(map.territories.size(), 0), n(map.territories.size(), 0);
    for (std::int32_t y = 0; y < map.height; ++y)
        for (std::int32_t x = 0; x < map.width; ++x) {
            const sim::TerritoryId t = map.at(x, y);
            if (t >= map.territories.size()) continue;
            sx[t] += x;
            sy[t] += y;
            ++n[t];
        }
    for (std::size_t t = 0; t < map.territories.size(); ++t) {
        if (n[t] == 0) continue;
        const sim::Territory& terr = map.territories[t];
        std::string label = terr.name;
        for (char& ch : label) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (company && company->has_access(static_cast<sim::TerritoryId>(t))) label += " (YOURS)";
        else if (!terr.open()) label += " " + format_money(terr.access_cost);
        float px = 0, py = 0;
        const auto cell_mm = static_cast<std::int64_t>(cam.mm_per_tile);
        if (!cam.to_screen({(sx[t] * cell_mm) / n[t] + cell_mm / 2, (sy[t] * cell_mm) / n[t] + cell_mm / 2}, px, py)) continue;
        const auto w = static_cast<float>(text_width(label, 2));
        glColor4f(0.25f, 0.15f, 0.4f, 0.9f);
        draw_text(px - w / 2, py, label, 2);
    }
}

void draw_radar(const sim::World& w, const Camera& cam, float x0, float y0, float size) {
    const sim::Terrain& t = w.terrain();
    const float across = static_cast<float>(std::max(t.width(), t.height()));
    const float k = size / across; // pixels per map cell
    glColor4f(0.05f, 0.05f, 0.07f, 0.9f);
    glBegin(GL_QUADS);
    glVertex2f(x0 - 3, y0 - 3);
    glVertex2f(x0 + size + 3, y0 - 3);
    glVertex2f(x0 + size + 3, y0 + size + 3);
    glVertex2f(x0 - 3, y0 + size + 3);
    // The land, a sample every few cells, lighter higher up.
    const int step = std::max(1, static_cast<int>(across / 110.0f));
    for (int ty = 0; ty < t.height(); ty += step) {
        for (int tx = 0; tx < t.width(); tx += step) {
            float r, g, b;
            ground_rgb(t.ground(tx, ty), 0.5f, r, g, b);
            const float lift = std::clamp(static_cast<float>(t.corner_height(tx, ty)) / 600.0f, 0.0f, 0.35f);
            if (t.ground(tx, ty) != sim::GroundType::Water) {
                r += lift;
                g += lift;
                b += lift;
            }
            glColor3f(r, g, b);
            const float sx = x0 + static_cast<float>(tx) * k, sy = y0 + static_cast<float>(ty) * k;
            const float e = static_cast<float>(step) * k;
            glVertex2f(sx, sy);
            glVertex2f(sx + e, sy);
            glVertex2f(sx + e, sy + e);
            glVertex2f(sx, sy + e);
        }
    }
    // Towns as white dots, stations in their owners' colours.
    const auto dot = [&](sim::MapPoint p, float half) {
        const float sx = x0 + static_cast<float>(p.x_mm) / cam.mm_per_tile * k, sy = y0 + static_cast<float>(p.y_mm) / cam.mm_per_tile * k;
        glVertex2f(sx - half, sy - half);
        glVertex2f(sx + half, sy - half);
        glVertex2f(sx + half, sy + half);
        glVertex2f(sx - half, sy + half);
    };
    glColor3f(1, 1, 1);
    for (const sim::Town& town : w.economy().towns()) dot(w.economy().node_centre(town.cx, town.cy), 2.0f);
    for (const sim::Station& st : w.railway().stations()) {
        float r, g, b;
        owner_rgb(st.owner, w.player_company(), r, g, b);
        glColor3f(r * 0.6f, g * 0.6f, b * 0.6f);
        dot(w.railway().track().node(st.node).pos, 1.5f);
    }
    glEnd();
    // Track as thin lines.
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor3f(0.25f, 0.18f, 0.12f);
    const sim::TrackNetwork& net = w.railway().track();
    for (const sim::TrackEdge& e : net.edges()) {
        if (e.removed) continue;
        for (const sim::NodeId n : {e.a, e.b}) {
            const sim::MapPoint p = net.node(n).pos;
            glVertex2f(x0 + static_cast<float>(p.x_mm) / cam.mm_per_tile * k, y0 + static_cast<float>(p.y_mm) / cam.mm_per_tile * k);
        }
    }
    // Where the view looks: a yellow mark and the way it faces.
    const float vx = x0 + (cam.three_d ? cam.target_x : cam.pan_x + static_cast<float>(cam.width_px) / (2 * cam.zoom)) * k;
    const float vy = y0 + (cam.three_d ? cam.target_y : cam.pan_y + static_cast<float>(cam.height_px) / (2 * cam.zoom)) * k;
    glColor3f(1.0f, 0.9f, 0.2f);
    glVertex2f(vx - 4, vy);
    glVertex2f(vx + 4, vy);
    glVertex2f(vx, vy - 4);
    glVertex2f(vx, vy + 4);
    if (cam.three_d) {
        glVertex2f(vx, vy);
        glVertex2f(vx + std::sin(cam.yaw) * 12, vy - std::cos(cam.yaw) * 12);
    }
    glEnd();
}

std::optional<sim::MapPoint> radar_point(const sim::World& w, const Camera& cam, float x0, float y0, float size, float sx, float sy) {
    if (sx < x0 || sy < y0 || sx > x0 + size || sy > y0 + size) return std::nullopt;
    const sim::Terrain& t = w.terrain();
    const float across = static_cast<float>(std::max(t.width(), t.height()));
    const float cells_x = (sx - x0) / size * across, cells_y = (sy - y0) / size * across;
    if (cells_x > static_cast<float>(t.width()) || cells_y > static_cast<float>(t.height())) return std::nullopt;
    return sim::MapPoint{static_cast<std::int64_t>(cells_x * cam.mm_per_tile), static_cast<std::int64_t>(cells_y * cam.mm_per_tile)};
}

void clear_smoke() { g_smoke.clear(); }

void draw_smoke(const Camera& cam, float seconds) {
    if (!cam.three_d) {
        g_smoke.clear();
        return;
    }
    // Each chimney sends up a stream of puffs that rise, drift downwind,
    // swell and fade; the puffs are little cubes, shaded by face.
    constexpr int kPuffs = 6;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const SmokeSource& src : g_smoke) {
        const float grey = src.steam ? 0.95f : 0.5f;
        for (int i = 0; i < kPuffs; ++i) {
            float age = seconds / 2.4f + (static_cast<float>(i) + src.seed) / kPuffs;
            age -= std::floor(age);
            const float r = src.size * (0.6f + 2.2f * age);
            const float x = src.x + age * 6.0f * src.size, y = src.y - age * 3.0f * src.size;
            const float z = src.z + r + age * 12.0f * src.size;
            const float alpha = 0.75f * (1.0f - age) * std::min(1.0f, age * 8.0f);
            const auto face = [&](float shade) { glColor4f(grey * shade, grey * shade, grey * shade, alpha); };
            face(1.0f); // top
            glVertex3f(x - r, y - r, z + r); glVertex3f(x + r, y - r, z + r); glVertex3f(x + r, y + r, z + r); glVertex3f(x - r, y + r, z + r);
            face(0.85f); // north and south
            glVertex3f(x - r, y - r, z - r); glVertex3f(x + r, y - r, z - r); glVertex3f(x + r, y - r, z + r); glVertex3f(x - r, y - r, z + r);
            face(0.7f);
            glVertex3f(x - r, y + r, z - r); glVertex3f(x + r, y + r, z - r); glVertex3f(x + r, y + r, z + r); glVertex3f(x - r, y + r, z + r);
            face(0.9f); // west and east
            glVertex3f(x - r, y - r, z - r); glVertex3f(x - r, y + r, z - r); glVertex3f(x - r, y + r, z + r); glVertex3f(x - r, y - r, z + r);
            face(0.75f);
            glVertex3f(x + r, y - r, z - r); glVertex3f(x + r, y + r, z - r); glVertex3f(x + r, y + r, z + r); glVertex3f(x + r, y - r, z + r);
        }
    }
    glEnd();
    glDepthMask(GL_TRUE);
    g_smoke.clear();
}

} // namespace railmaster::client
