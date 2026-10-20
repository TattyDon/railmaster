#include "render.hpp"

#include "font.hpp"
#include "model.hpp"
#include "shading.hpp"
#include "ui.hpp"
#include "tools.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <future>
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
// Turning parts (windmill sails, water wheels): recorded as the scene's
// display list is made, drawn each frame against the animation clock.
struct Turning {
    const models::Model* model;
    float x, y, z, fx, fy, size;
    bool to_height;
};
std::vector<Turning> g_turning_build;
float g_animation_seconds = 0;

// Lights seen this frame (tiles), shown after dark: a glow at each, and
// for headlamps a pool of light on the ground ahead.
struct LightSource {
    float x, y, z, size;
    float r, g, b;
    float ahead_x = 0, ahead_y = 0; // a headlamp's beam direction, else 0
};
std::vector<LightSource> g_lights;
void add_light(float x, float y, float z, float size, float r, float g, float b, float ahead_x = 0, float ahead_y = 0) {
    if (shading::night() > 0.05f) g_lights.push_back({x, y, z, size, r, g, b, ahead_x, ahead_y});
}
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
    // The time of day's light, in map axes (x east, y south, z up).
    GLfloat dir[4] = {0, 0, 1, 0}, ambient[4] = {0, 0, 0, 1}, diffuse[4] = {0, 0, 0, 1};
    shading::light_params(dir, diffuse, ambient);
    for (int k = 0; k < 3; ++k) {
        ambient[k] *= 1.1f;
        diffuse[k] *= 0.9f;
    }
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
    if (SDL_GL_ExtensionSupported("Gl_ext_texture_filter_anisotropic")) {
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

// The terrain's key as draw_terrain last found it, so others need not hash
// the whole map again each frame.
std::uint64_t g_terrain_key = 0;

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
    // Each water tile's level: its lowest corner, then lowered to its water
    // neighbours' where they are close, so a lake lies at one level while a
    // river still steps down its rapids.
    std::vector<float> level(static_cast<std::size_t>(w) * static_cast<std::size_t>(hgt), 0.0f);
    const auto at = [&](int x, int y) -> float& { return level[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]; };
    for (int ty = 0; ty < hgt; ++ty)
        for (int tx = 0; tx < w; ++tx)
            if (t.ground(tx, ty) == sim::GroundType::Water)
                at(tx, ty) = std::min({z(tx, ty), z(tx + 1, ty), z(tx, ty + 1), z(tx + 1, ty + 1)});
    constexpr float kSameWater = 0.3f; // tiles of height
    for (bool changed = true; changed;) {
        changed = false;
        for (int ty = 0; ty < hgt; ++ty) {
            for (int tx = 0; tx < w; ++tx) {
                if (t.ground(tx, ty) != sim::GroundType::Water) continue;
                const int nb[4][2] = {{tx - 1, ty}, {tx + 1, ty}, {tx, ty - 1}, {tx, ty + 1}};
                for (const auto& n : nb) {
                    if (n[0] < 0 || n[1] < 0 || n[0] >= w || n[1] >= hgt || t.ground(n[0], n[1]) != sim::GroundType::Water) continue;
                    const float other = at(n[0], n[1]);
                    if (other < at(tx, ty) - 1e-4f && at(tx, ty) - other < kSameWater) {
                        at(tx, ty) = other;
                        changed = true;
                    }
                }
            }
        }
    }
    // Banks: water lies flat at its tile's lowest corner, so where the
    // ground round it stands higher, an earth bank runs up from the water
    // to the ground along each edge; no gap to the sky shows between them.
    glBindTexture(GL_TEXTURE_2D, shading::white_texture());
    glBegin(GL_QUADS);
    glColor3f(0.42f, 0.36f, 0.26f);
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            if (t.ground(tx, ty) != sim::GroundType::Water) continue;
            const float wz = at(tx, ty);
            // The four edges, corner to corner, with the way out of the tile.
            const int edges[4][6] = {{tx, ty, tx + 1, ty, 0, -1},
                                     {tx + 1, ty, tx + 1, ty + 1, 1, 0},
                                     {tx + 1, ty + 1, tx, ty + 1, 0, 1},
                                     {tx, ty + 1, tx, ty, -1, 0}};
            for (const auto& e : edges) {
                float za = z(e[0], e[1]), zb = z(e[2], e[3]);
                // Next to more water, only the step up to its level.
                const int nx = tx + e[4], ny = ty + e[5];
                if (nx >= 0 && ny >= 0 && nx < w && ny < hgt && t.ground(nx, ny) == sim::GroundType::Water) {
                    za = zb = std::min(at(nx, ny), std::max(za, zb));
                }
                if (za <= wz + 1e-4f && zb <= wz + 1e-4f) continue;
                glNormal3f(static_cast<float>(-e[4]), static_cast<float>(-e[5]), 0.6f); // facing the water
                glVertex3f(static_cast<float>(e[0]), static_cast<float>(e[1]), wz);
                glVertex3f(static_cast<float>(e[2]), static_cast<float>(e[3]), wz);
                glVertex3f(static_cast<float>(e[2]), static_cast<float>(e[3]), zb);
                glVertex3f(static_cast<float>(e[0]), static_cast<float>(e[1]), za);
            }
        }
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, ground_texture());
    // Water lies flat, at its lowest corner.
    glBindTexture(GL_TEXTURE_2D, water_texture());
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            if (t.ground(tx, ty) != sim::GroundType::Water) continue;
            const auto fx = static_cast<float>(tx), fy = static_cast<float>(ty);
            const float wz = at(tx, ty);
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

// A beam between two points (tiles), `w` thick: girders, legs, braces and
// rails of lattice work. Inside GL_QUADS.
void beam(float x0, float y0, float z0, float x1, float y1, float z1, float w, const float* colour) {
    const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len <= 0) return;
    const float ax = dx / len, ay = dy / len, az = dz / len;
    // Two directions square to the beam.
    float ux = ay, uy = -ax, uz = 0;
    float ul = std::sqrt(ux * ux + uy * uy);
    if (ul < 1e-3f) {
        ux = 1;
        uy = 0;
        ul = 1;
    }
    ux /= ul;
    uy /= ul;
    const float vx = ay * uz - az * uy, vy = az * ux - ax * uz, vz = ax * uy - ay * ux;
    const float h = w / 2;
    const float cu[4] = {1, 1, -1, -1}, cv[4] = {1, -1, -1, 1};
    glColor3fv(colour);
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const float nx = (cu[i] + cu[j]) * ux + (cv[i] + cv[j]) * vx, ny = (cu[i] + cu[j]) * uy + (cv[i] + cv[j]) * vy,
                    nz = (cu[i] + cu[j]) * uz + (cv[i] + cv[j]) * vz;
        glNormal3f(nx, ny, nz);
        const auto corner = [&](int c, float bx, float by, float bz) {
            glVertex3f(bx + (cu[c] * ux + cv[c] * vx) * h, by + (cu[c] * uy + cv[c] * vy) * h, bz + (cu[c] * uz + cv[c] * vz) * h);
        };
        corner(i, x0, y0, z0);
        corner(j, x0, y0, z0);
        corner(j, x1, y1, z1);
        corner(i, x1, y1, z1);
    }
}

// A barn with a gambrel roof (steep below, shallow above) over walls `h`
// tall, `hl` long along (dx, dy) and `hw` wide. Inside GL_QUADS.
void gambrel(float x, float y, float z, float dx, float dy, float hl, float hw, float h, const float* wall, const float* roof,
             const float* trim) {
    box(x, y, z, dx, dy, hl, hw, h, wall, wall);
    const float px = -dy, py = dx;
    const float eave = hw * 1.08f, knee = hw * 0.62f, kz = z + h + hw * 0.62f, rz = z + h + hw * 0.92f;
    const auto pt = [&](float along, float across, float pz) { glVertex3f(x + dx * along + px * across, y + dy * along + py * across, pz); };
    glColor3fv(roof);
    for (const float sgn : {-1.0f, 1.0f}) {
        glNormal3f(px * sgn, py * sgn, 0.35f);
        pt(-hl * 1.04f, eave * sgn, z + h - 0.01f);
        pt(hl * 1.04f, eave * sgn, z + h - 0.01f);
        pt(hl * 1.04f, knee * sgn, kz);
        pt(-hl * 1.04f, knee * sgn, kz);
        glNormal3f(px * sgn * 0.4f, py * sgn * 0.4f, 1.0f);
        pt(-hl * 1.04f, knee * sgn, kz);
        pt(hl * 1.04f, knee * sgn, kz);
        pt(hl * 1.04f, 0, rz);
        pt(-hl * 1.04f, 0, rz);
    }
    // The gable ends, in the wall colour, with a white trim line.
    for (const float end : {-1.0f, 1.0f}) {
        const float a = hl * end;
        glColor3fv(wall);
        glNormal3f(dx * end, dy * end, 0);
        for (const float sgn : {-1.0f, 1.0f}) {
            pt(a, hw * sgn, z + h);
            pt(a, 0, z + h);
            pt(a, 0, kz);
            pt(a, knee * sgn, kz);
            pt(a, knee * sgn, kz);
            pt(a, 0, kz);
            pt(a, 0, rz);
            pt(a, 0, rz);
        }
        // A hay door, white-framed.
        glColor3fv(trim);
        pt(a + end * 0.002f, -hw * 0.3f, z + h * 0.05f);
        pt(a + end * 0.002f, hw * 0.3f, z + h * 0.05f);
        pt(a + end * 0.002f, hw * 0.3f, z + h * 0.8f);
        pt(a + end * 0.002f, -hw * 0.3f, z + h * 0.8f);
    }
}

// A split-rail fence along the ground from one point to another. Inside GL_QUADS.
void fence(float x0, float y0, float x1, float y1, const Camera& cam, const float* wood) {
    const float len = std::hypot(x1 - x0, y1 - y0);
    const int posts = std::max(2, static_cast<int>(len / 0.09f) + 1);
    float px = x0, py = y0, pz = cam.ground_z(x0, y0);
    for (int i = 0; i < posts; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(posts - 1);
        const float x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t, z = cam.ground_z(x, y);
        beam(x, y, z, x, y, z + 0.045f, 0.008f, wood);
        if (i > 0) {
            beam(px, py, pz + 0.02f, x, y, z + 0.02f, 0.005f, wood);
            beam(px, py, pz + 0.038f, x, y, z + 0.038f, 0.005f, wood);
        }
        px = x;
        py = y;
        pz = z;
    }
}

// A ploughed field: rows of crop, alternately lighter and darker, along x,
// fenced round. Inside GL_QUADS.
void furrowed_field(float x, float y, float half, const float* crop, const Camera& cam, const float* wood) {
    const int rows = 24;
    glNormal3f(0, 0, 1);
    for (int r = 0; r < rows; ++r) {
        const float k = r % 2 == 0 ? 1.0f : 0.72f;
        glColor3f(crop[0] * k, crop[1] * k, crop[2] * k);
        const float y0 = y - half + 2 * half * static_cast<float>(r) / rows, y1 = y - half + 2 * half * static_cast<float>(r + 1) / rows;
        for (int c = 0; c < 4; ++c) {
            const float x0 = x - half + half * 0.5f * static_cast<float>(c);
            ground_quad(x0, y0, x0 + half * 0.5f, y1, cam);
        }
    }
    fence(x - half, y - half, x + half, y - half, cam, wood);
    fence(x + half, y - half, x + half, y + half, cam, wood);
    fence(x + half, y + half, x - half, y + half, cam, wood);
    fence(x - half, y + half, x - half, y - half, cam, wood);
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
        const float px = -dy, py = dx;
        // The headframe: two legs over the shaft, raked back-stays, braces,
        // and the winding wheel at the top.
        const float hx = x + 0.1f * k, hy = y - 0.02f * k, top = z + 0.52f;
        for (const float side : {-1.0f, 1.0f}) {
            const float lx = hx + px * 0.05f * side, ly = hy + py * 0.05f * side;
            beam(lx, ly, z, lx, ly, top, 0.014f, timber);
            beam(lx - dx * 0.28f, ly - dy * 0.28f, z, lx, ly, top - 0.02f, 0.012f, timber); // back-stay
            for (const float hz : {0.15f, 0.32f}) beam(lx, ly, z + hz, lx - dx * 0.28f * (1 - hz / 0.5f), ly - dy * 0.28f * (1 - hz / 0.5f), z + hz, 0.008f, timber);
        }
        for (const float hz : {0.12f, 0.3f, 0.48f}) {
            beam(hx + px * 0.05f, hy + py * 0.05f, z + hz, hx - px * 0.05f, hy - py * 0.05f, z + hz, 0.008f, timber);
        }
        beam(hx + px * 0.05f, hy + py * 0.05f, z + 0.12f, hx - px * 0.05f, hy - py * 0.05f, z + 0.3f, 0.006f, timber);
        beam(hx + px * 0.06f, hy + py * 0.06f, top, hx - px * 0.06f, hy - py * 0.06f, top, 0.016f, dark);
        wheel(hx, hy, top - 0.005f, dx, dy, 0.045f, 0.012f, dark, metal);
        // The winding engine house, brick with a slate roof, and its stack.
        const float ex = x - 0.16f * k, ey = y - 0.02f * k;
        barn(ex, ey, z, dx, dy, 0.11f * k, 0.07f * k, 0.13f, brick, slate);
        cylinder(ex - dx * 0.1f * k + px * 0.06f * k, ey - dy * 0.1f * k + py * 0.06f * k, z, 0.025f, 0.38f, brick, dark, 0.02f);
        add_smoke(ex - dx * 0.1f * k + px * 0.06f * k, ey - dy * 0.1f * k + py * 0.06f * k, z + 0.38f, 0.05f);
        // The winding rope from the drum to the wheel.
        const float rope[3] = {0.15f, 0.15f, 0.15f};
        beam(ex + dx * 0.11f * k, ey + dy * 0.11f * k, z + 0.1f, hx, hy, top + 0.03f, 0.004f, rope);
        // Spoil heaped behind.
        heap(x + 0.2f * k, y + 0.24f * k, z, 0.18f * k, 0.26f, spoil);
        heap(x + 0.34f * k, y + 0.1f * k, z, 0.1f * k, 0.14f, spoil);
        return;
    }
    if (key == "logging_camp") {
        const float px = -dy, py = dx;
        const float bark[3] = {0.36f, 0.26f, 0.17f}, cut[3] = {0.78f, 0.62f, 0.42f}, roof[3] = {0.32f, 0.28f, 0.24f};
        // A log cabin with a stone chimney.
        const float hx = x - 0.22f * k, hy = y - 0.18f * k;
        barn(hx, hy, cam.ground_z(hx, hy), dx, dy, 0.08f * k, 0.06f * k, 0.08f, timber, roof);
        const float chimney[3] = {0.5f, 0.48f, 0.45f};
        box(hx + dx * 0.08f * k, hy + dy * 0.08f * k, cam.ground_z(hx, hy), dx, dy, 0.012f, 0.012f, 0.17f, chimney, chimney);
        add_smoke(hx + dx * 0.08f * k, hy + dy * 0.08f * k, cam.ground_z(hx, hy) + 0.17f, 0.025f);
        // The saw shed: open sides, posts and a pitched roof over the saw pit.
        const float sx = x + 0.1f * k, sy = y - 0.15f * k, sz = cam.ground_z(sx, sy);
        for (const float a2 : {-1.0f, 1.0f})
            for (const float b2 : {-1.0f, 1.0f}) {
                beam(sx + dx * 0.12f * k * a2 + px * 0.06f * k * b2, sy + dy * 0.12f * k * a2 + py * 0.06f * k * b2, sz,
                     sx + dx * 0.12f * k * a2 + px * 0.06f * k * b2, sy + dy * 0.12f * k * a2 + py * 0.06f * k * b2, sz + 0.1f, 0.012f, timber);
            }
        gable(sx, sy, sz + 0.1f, dx, dy, 0.13f * k, 0.075f * k, 0.05f, roof, timber);
        const float pit[3] = {0.18f, 0.14f, 0.1f};
        box(sx, sy, sz - 0.005f, dx, dy, 0.1f * k, 0.02f * k, 0.01f, pit, pit);
        prism(sx, sy, sz + 0.035f, dx, dy, 0.1f * k, 0.018f, bark); // a log on the trestles
        // A log deck: logs stacked in a pyramid, cut ends showing.
        const float lx = x + 0.12f * k, ly = y + 0.16f * k, lz = cam.ground_z(lx, ly);
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < 4 - row; ++i) {
                const float off = (static_cast<float>(i) - static_cast<float>(3 - row) / 2.0f) * 0.042f;
                const float cx = lx + px * off, cy = ly + py * off, cz = lz + 0.02f + static_cast<float>(row) * 0.036f;
                prism(cx, cy, cz, dx, dy, 0.16f * k, 0.02f, bark);
                for (const float end : {-1.0f, 1.0f}) {
                    prism_cap(cx + dx * 0.16f * k * end, cy + dy * 0.16f * k * end, cz, dx, dy, 0.02f, end, cut);
                }
            }
        // Stumps where the trees came down.
        for (int i = 0; i < 8; ++i) {
            const std::uint32_t h = hash(seed, static_cast<std::uint32_t>(i), 31);
            const float tx = x + (unit(h) - 0.5f) * 0.8f * k, ty = y + (unit(h >> 16) - 0.5f) * 0.8f * k;
            cylinder(tx, ty, cam.ground_z(tx, ty), 0.02f, 0.025f, bark, cut);
        }
        return;
    }
    if (ends_with(key, "_farm") || key == "fruit_orchard" || key == "cattle_ranch" || key == "rubber_plantation") {
        float crop[3] = {0.55f, 0.6f, 0.25f};
        if (key == "corn_farm" || key == "grain_farm") { crop[0] = 0.82f; crop[1] = 0.72f; crop[2] = 0.3f; }
        else if (key == "cotton_farm") { crop[0] = 0.85f; crop[1] = 0.85f; crop[2] = 0.8f; }
        else if (key == "rice_farm") { crop[0] = 0.5f; crop[1] = 0.7f; crop[2] = 0.45f; }
        else if (key == "sugar_farm" || key == "coffee_farm" || key == "rubber_plantation") { crop[0] = 0.3f; crop[1] = 0.5f; crop[2] = 0.22f; }
        else if (key == "dairy_farm" || key == "cattle_ranch" || key == "sheep_farm") { crop[0] = 0.42f; crop[1] = 0.62f; crop[2] = 0.3f; }
        const bool pasture = key == "dairy_farm" || key == "cattle_ranch" || key == "sheep_farm";
        const bool trees = key == "fruit_orchard" || key == "coffee_farm" || key == "rubber_plantation";
        const float fx = x + 0.12f * k, fy = y + 0.12f * k, half = 0.28f * k;
        const float rail[3] = {0.45f, 0.36f, 0.26f};
        if (pasture || trees) {
            // Grass (or an orchard floor), fenced.
            glColor3fv(crop);
            glNormal3f(0, 0, 1);
            field(fx, fy, half, crop, cam);
            fence(fx - half, fy - half, fx + half, fy - half, cam, rail);
            fence(fx + half, fy - half, fx + half, fy + half, cam, rail);
            fence(fx + half, fy + half, fx - half, fy + half, cam, rail);
            fence(fx - half, fy + half, fx - half, fy - half, cam, rail);
        } else {
            furrowed_field(fx, fy, half, crop, cam, rail);
        }
        if (trees) {
            // Trees in planted rows.
            const float leaf[3] = {0.2f, 0.42f, 0.17f}, bark[3] = {0.35f, 0.25f, 0.16f};
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    const float tx = fx - half * 0.75f + half * 0.5f * static_cast<float>(c);
                    const float ty = fy - half * 0.75f + half * 0.5f * static_cast<float>(r);
                    const float tz = cam.ground_z(tx, ty);
                    beam(tx, ty, tz, tx, ty, tz + 0.05f, 0.012f, bark);
                    cylinder(tx, ty, tz + 0.04f, 0.035f, 0.04f, leaf, leaf, 0.045f);
                    cylinder(tx, ty, tz + 0.08f, 0.045f, 0.035f, leaf, leaf, 0.01f);
                }
        }
        if (pasture) {
            // The herd: cattle (black and white for dairy) or sheep.
            const bool sheep = key == "sheep_farm";
            for (int i = 0; i < 9; ++i) {
                const std::uint32_t h = hash(seed, static_cast<std::uint32_t>(i), 21);
                const float ax = fx + (unit(h) - 0.5f) * half * 1.6f, ay = fy + (unit(h >> 16) - 0.5f) * half * 1.6f;
                const float az = cam.ground_z(ax, ay), turn = unit(h >> 8) * 6.2831853f;
                const float cdx = std::cos(turn), cdy = std::sin(turn);
                float coat[3] = {0.42f, 0.26f, 0.16f};
                if (sheep) { coat[0] = 0.9f; coat[1] = 0.88f; coat[2] = 0.82f; }
                else if (key == "dairy_farm") { const float v = unit(h >> 4) < 0.5f ? 0.92f : 0.12f; coat[0] = coat[1] = coat[2] = v; }
                const float leg[3] = {0.15f, 0.12f, 0.1f};
                const float sz = (sheep ? 0.8f : 1.0f) * 2.6f;
                box(ax, ay, az + 0.012f * sz, cdx, cdy, 0.022f * sz, 0.011f * sz, 0.018f * sz, coat, coat);
                box(ax + cdx * 0.026f * sz, ay + cdy * 0.026f * sz, az + 0.02f * sz, cdx, cdy, 0.008f * sz, 0.007f * sz, 0.012f * sz,
                    sheep ? leg : coat, sheep ? leg : coat);
                box(ax, ay, az, cdx, cdy, 0.016f * sz, 0.008f * sz, 0.012f * sz, leg, leg);
            }
        }
        // The farmstead: a red gambrel barn and a domed silo.
        const float bx = x - 0.2f * k, by = y - 0.16f * k;
        const float shingle[3] = {0.5f, 0.46f, 0.42f};
        gambrel(bx, by, cam.ground_z(bx, by), dx, dy, 0.1f * k, 0.06f * k, 0.13f, red_barn, shingle, white);
        // The farmhouse, white clapboard, by the barn.
        const float hx = x - 0.3f * k, hy = y + 0.05f * k;
        barn(hx, hy, cam.ground_z(hx, hy), dy, -dx, 0.06f * k, 0.04f * k, 0.1f, white, slate);
        const float sx = x - 0.04f * k, sy = y - 0.24f * k, sz = cam.ground_z(sx, sy);
        cylinder(sx, sy, sz, 0.035f * k, 0.26f, white, metal);
        cylinder(sx, sy, sz + 0.26f, 0.035f * k, 0.03f, metal, metal, 0.02f * k);
        cylinder(sx, sy, sz + 0.29f, 0.02f * k, 0.015f, metal, metal, 0.002f);
        return;
    }
    if (key == "oil_well") {
        // A lattice derrick tapering over the well: four legs, girts at
        // each stage and braces across every face, a crown block on top.
        const float base = 0.09f, crown = 0.018f, height = 0.62f;
        const auto leg = [&](int i, float f, float& lx, float& ly) {
            const float r = base + (crown - base) * f;
            lx = x + (i == 0 || i == 3 ? -r : r);
            ly = y + (i < 2 ? -r : r);
        };
        for (int i = 0; i < 4; ++i) {
            float ax, ay, bx, by;
            leg(i, 0, ax, ay);
            leg(i, 1, bx, by);
            beam(ax, ay, z, bx, by, z + height, 0.01f, timber);
        }
        constexpr int kStages = 5;
        for (int st = 0; st < kStages; ++st) {
            const float f0 = static_cast<float>(st) / kStages, f1 = static_cast<float>(st + 1) / kStages;
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                float ax, ay, bx, by, cx, cy, ex, ey;
                leg(i, f1, ax, ay);
                leg(j, f1, bx, by);
                beam(ax, ay, z + height * f1, bx, by, z + height * f1, 0.006f, timber);
                leg(i, f0, cx, cy);
                leg(j, f0, ex, ey);
                beam(cx, cy, z + height * f0, bx, by, z + height * f1, 0.004f, timber); // brace
            }
        }
        box(x, y, z + height, 1, 0, crown * 1.4f, crown * 1.4f, 0.02f, dark, dark);
        // The engine shed and the storage tanks.
        barn(x + 0.17f * k, y - 0.04f * k, z, dx, dy, 0.07f, 0.045f, 0.06f, timber, slate);
        const float tank[3] = {0.25f, 0.22f, 0.2f};
        cylinder(x - 0.16f * k, y + 0.14f * k, z, 0.06f * k, 0.1f, tank, dark);
        cylinder(x - 0.02f * k, y + 0.2f * k, z, 0.05f * k, 0.08f, tank, dark);
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
        // Which way the water lies: the wharf runs out that way.
        const auto wet = [&](float wx, float wy) {
            if (!cam.terrain) return false;
            const auto tx = static_cast<std::int32_t>(std::floor(wx)), ty = static_cast<std::int32_t>(std::floor(wy));
            if (tx < 0 || ty < 0 || tx >= cam.terrain->width() || ty >= cam.terrain->height()) return false;
            return cam.terrain->ground(tx, ty) == sim::GroundType::Water;
        };
        float wx = 1, wy = 0;
        int best = -1;
        for (int i = 0; i < 8; ++i) {
            const float an = 0.7853982f * static_cast<float>(i), cx = std::cos(an), cy = std::sin(an);
            int n = 0;
            for (const float r : {0.4f, 0.7f, 1.0f, 1.4f}) n += wet(x + cx * r * k, y + cy * r * k) ? 1 : 0;
            if (n > best) {
                best = n;
                wx = cx;
                wy = cy;
            }
        }
        const float qx = -wy, qy = wx; // along the shore
        const float stone[3] = {0.58f, 0.56f, 0.52f}, plank[3] = {0.5f, 0.38f, 0.25f}, pile[3] = {0.3f, 0.22f, 0.15f};
        const float water_z = std::min(z, cam.ground_z(x + wx * 0.8f * k, y + wy * 0.8f * k));
        const float deck = z + 0.04f;
        // A stone quay along the shore and a timber wharf out over the water on piles.
        box(x, y, z - 0.1f, wx, wy, 0.12f * k, 0.4f * k, deck - z + 0.1f, stone, stone);
        const float ox = x + wx * 0.42f * k, oy = y + wy * 0.42f * k;
        box(ox, oy, deck - 0.015f, wx, wy, 0.3f * k, 0.13f * k, 0.015f, plank, plank);
        for (float a2 = -0.25f; a2 <= 0.26f; a2 += 0.125f)
            for (const float side : {-1.0f, 1.0f}) {
                const float pxp = ox + wx * a2 * k + qx * 0.12f * k * side, pyp = oy + wy * a2 * k + qy * 0.12f * k * side;
                beam(pxp, pyp, water_z - 0.15f, pxp, pyp, deck + 0.02f, 0.016f, pile);
            }
        // A warehouse on the quay, crates and a capstan.
        barn(x - wx * 0.22f * k, y - wy * 0.22f * k, z, qx, qy, 0.28f * k, 0.09f * k, 0.14f, timber, slate);
        // A crane on the wharf: mast, raking jib, its rope and a hanging load.
        const float cx0 = ox + wx * 0.1f * k + qx * 0.08f * k, cy0 = oy + wy * 0.1f * k + qy * 0.08f * k;
        beam(cx0, cy0, deck, cx0, cy0, deck + 0.38f, 0.024f, dark);
        const float jx = cx0 + wx * 0.3f * k, jy = cy0 + wy * 0.3f * k;
        beam(cx0, cy0, deck + 0.1f, jx, jy, deck + 0.4f, 0.016f, dark);
        beam(cx0, cy0, deck + 0.38f, jx, jy, deck + 0.4f, 0.006f, dark);
        beam(jx, jy, deck + 0.4f, jx, jy, deck + 0.18f, 0.003f, dark);
        box(jx, jy, deck + 0.13f, qx, qy, 0.025f, 0.025f, 0.05f, plank, plank);
        // A schooner moored alongside: hull, deck, two masts with sails.
        const float sx = ox + wx * 0.05f * k - qx * 0.32f * k, sy = oy + wy * 0.05f * k - qy * 0.32f * k;
        const float hull[3] = {0.22f, 0.16f, 0.12f}, deck_c[3] = {0.62f, 0.5f, 0.34f}, sail[3] = {0.92f, 0.9f, 0.82f};
        const float hl = 0.36f * k, hw = 0.07f * k;
        box(sx, sy, water_z - 0.02f, wx, wy, hl, hw, 0.07f, hull, deck_c);
        // A pointed bow.
        glColor3fv(hull);
        for (const float side : {-1.0f, 1.0f}) {
            glNormal3f(qx * side + wx, qy * side + wy, 0);
            const float bx0 = sx + wx * hl, by0 = sy + wy * hl;
            glVertex3f(bx0 + qx * hw * side, by0 + qy * hw * side, water_z - 0.02f);
            glVertex3f(bx0 + wx * 0.12f * k, by0 + wy * 0.12f * k, water_z - 0.02f);
            glVertex3f(bx0 + wx * 0.14f * k, by0 + wy * 0.14f * k, water_z + 0.06f);
            glVertex3f(bx0 + qx * hw * side, by0 + qy * hw * side, water_z + 0.05f);
        }
        for (const float m : {-0.4f, 0.35f}) {
            const float mx = sx + wx * hl * m, my = sy + wy * hl * m, mz = water_z + 0.05f;
            beam(mx, my, mz, mx, my, mz + 0.55f, 0.012f, pile);
            // A gaff sail aft of the mast.
            glColor3fv(sail);
            glNormal3f(qx * 0.3f, qy * 0.3f, 1.0f); // canvas catches the light from either side
            glVertex3f(mx - wx * 0.01f, my - wy * 0.01f, mz + 0.08f);
            glVertex3f(mx - wx * 0.26f * k, my - wy * 0.26f * k, mz + 0.08f);
            glVertex3f(mx - wx * 0.24f * k, my - wy * 0.24f * k, mz + 0.42f);
            glVertex3f(mx - wx * 0.01f, my - wy * 0.01f, mz + 0.5f);
        }
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

// Each bridge edge's span: the run of bridge edges it belongs to, from
// one bank to the other, so suspension bridges can hang their cables over
// the whole crossing.
struct Span {
    float x0, y0, z0, x1, y1, z1; // the two ends (tiles), at deck height
};
struct BridgeSpans {
    std::vector<int> of_edge; // index into spans, or -1
    std::vector<Span> spans;
};

BridgeSpans bridge_spans(const sim::TrackNetwork& net, const Camera& cam) {
    BridgeSpans out;
    const auto& edges = net.edges();
    out.of_edge.assign(edges.size(), -1);
    const auto is_bridge = [&](std::size_t i) { return !edges[i].removed && edges[i].kind == sim::TrackKind::Bridge; };
    const auto tile = [&](std::int64_t mm) { return static_cast<float>(mm) / cam.mm_per_tile; };
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (!is_bridge(i) || out.of_edge[i] >= 0) continue;
        // Gather the run through nodes joining exactly two bridge edges.
        std::vector<std::size_t> run{i};
        std::vector<sim::NodeId> ends;
        out.of_edge[i] = static_cast<int>(out.spans.size());
        for (std::size_t r = 0; r < run.size(); ++r) {
            for (const sim::NodeId n : {edges[run[r]].a, edges[run[r]].b}) {
                std::vector<std::size_t> here;
                for (const sim::EdgeId id : net.edges_at(n)) {
                    if (is_bridge(id)) here.push_back(id);
                }
                if (here.size() != 2) {
                    ends.push_back(n);
                    continue;
                }
                for (const std::size_t id : here) {
                    if (out.of_edge[id] < 0) {
                        out.of_edge[id] = static_cast<int>(out.spans.size());
                        run.push_back(id);
                    }
                }
            }
        }
        if (ends.size() < 2) ends = {edges[i].a, edges[i].b};
        // The two ends furthest apart.
        std::size_t ea = 0, eb = 1;
        float best = -1;
        for (std::size_t p = 0; p < ends.size(); ++p)
            for (std::size_t q = p + 1; q < ends.size(); ++q) {
                const sim::MapPoint pa = net.node(ends[p]).pos, pb = net.node(ends[q]).pos;
                const float d = std::hypot(tile(pa.x_mm - pb.x_mm), tile(pa.y_mm - pb.y_mm));
                if (d > best) {
                    best = d;
                    ea = p;
                    eb = q;
                }
            }
        const sim::TrackNode& na = net.node(ends[ea]);
        const sim::TrackNode& nb = net.node(ends[eb]);
        out.spans.push_back({tile(na.pos.x_mm), tile(na.pos.y_mm), cam.z_of(na.z_mm), tile(nb.pos.x_mm), tile(nb.pos.y_mm),
                             cam.z_of(nb.z_mm)});
    }
    return out;
}

// One bridge edge's structure below and around its deck (ends a and b,
// rail heights za and zb, tiles). Wood: timber trestle bents, braced.
// Stone: piers with round arches between, under solid spandrel walls.
// Steel: a through truss either side, chords, posts and diagonals, braced
// overhead. Suspension: towers at the span's ends, main cables sagging
// between them and hangers down to the deck. Inside GL_QUADS.
void bridge_structure(const sim::TrackEdge& e, float ax, float ay, float za, float bx, float by, float zb, const Span* span,
                      const Camera& cam) {
    const float len = std::hypot(bx - ax, by - ay);
    if (len <= 0) return;
    const float dx = (bx - ax) / len, dy = (by - ay) / len, px = -dy, py = dx;
    const float under = 0.05f; // the deck's underside below the rail
    const auto at = [&](float t, float& x, float& y, float& z) {
        x = ax + (bx - ax) * t;
        y = ay + (by - ay) * t;
        z = za + (zb - za) * t;
    };
    const auto ground = [&](float x, float y) { return cam.ground_z(x, y) - 0.15f; };
    switch (e.bridge) {
    case sim::BridgeType::Wood:
    case sim::BridgeType::None: {
        const float wood[3] = {0.42f, 0.3f, 0.19f}, dark[3] = {0.3f, 0.21f, 0.13f};
        const int n = std::max(1, static_cast<int>(std::ceil(len / 0.28f)));
        for (int i = 0; i <= n; ++i) {
            float x, y, z;
            at(static_cast<float>(i) / static_cast<float>(n), x, y, z);
            const float top = z - under, g = ground(x, y);
            if (top - g < 0.04f) continue;
            const float splay = 0.06f + (top - g) * 0.12f;
            // Two outer legs splayed out, two plumb, a cap beam.
            for (const float sgn : {-1.0f, 1.0f}) {
                beam(x + px * 0.06f * sgn, y + py * 0.06f * sgn, top, x + px * (0.06f + splay) * sgn, y + py * (0.06f + splay) * sgn, g,
                     0.014f, wood);
                beam(x + px * 0.025f * sgn, y + py * 0.025f * sgn, top, x + px * 0.025f * sgn, y + py * 0.025f * sgn, g, 0.012f, wood);
            }
            beam(x - px * 0.09f, y - py * 0.09f, top, x + px * 0.09f, y + py * 0.09f, top, 0.016f, dark);
            // Girts and X braces down the bent.
            const float h = top - g;
            const int storeys = std::max(1, static_cast<int>(h / 0.22f));
            for (int st = 0; st < storeys; ++st) {
                const float z0 = top - h * static_cast<float>(st) / static_cast<float>(storeys);
                const float z1 = top - h * static_cast<float>(st + 1) / static_cast<float>(storeys);
                const float w0 = 0.06f + splay * (top - z0) / h, w1 = 0.06f + splay * (top - z1) / h;
                beam(x - px * w0, y - py * w0, z0, x + px * w1, y + py * w1, z1, 0.007f, dark);
                beam(x + px * w0, y + py * w0, z0, x - px * w1, y - py * w1, z1, 0.007f, dark);
                beam(x - px * w1, y - py * w1, z1, x + px * w1, y + py * w1, z1, 0.008f, dark);
            }
        }
        // Stringers along under the deck.
        for (const float sgn : {-1.0f, 1.0f}) {
            beam(ax + px * 0.05f * sgn, ay + py * 0.05f * sgn, za - under - 0.01f, bx + px * 0.05f * sgn, by + py * 0.05f * sgn,
                 zb - under - 0.01f, 0.014f, dark);
        }
        break;
    }
    case sim::BridgeType::Stone: {
        const float stone[3] = {0.66f, 0.63f, 0.58f}, coping[3] = {0.55f, 0.52f, 0.48f};
        const float hw = 0.09f; // half the viaduct's width
        const int bays = std::max(1, static_cast<int>(std::round(len / 0.45f)));
        // Piers between the arches, battered out at the foot.
        for (int i = 0; i <= bays; ++i) {
            float x, y, z;
            at(static_cast<float>(i) / static_cast<float>(bays), x, y, z);
            const float g = ground(x, y), top = z - under;
            if (top - g < 0.02f) continue;
            box(x, y, g, dx, dy, 0.045f, hw, top - g, stone, stone);
            box(x, y, g, dx, dy, 0.065f, hw * 1.15f, std::min(0.12f, (top - g) * 0.4f), coping, coping);
        }
        // Each bay: spandrel walls on both faces down to a round arch, and
        // the arch's soffit beneath.
        constexpr int kSteps = 8;
        for (int i = 0; i < bays; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(bays), t1 = static_cast<float>(i + 1) / static_cast<float>(bays);
            float x0, y0, z0, x1, y1, z1;
            at(t0, x0, y0, z0);
            at(t1, x1, y1, z1);
            const float bay = len / static_cast<float>(bays) - 0.09f, rise = std::min(bay * 0.5f, 0.3f);
            for (int k2 = 0; k2 < kSteps; ++k2) {
                const float u0 = static_cast<float>(k2) / kSteps, u1 = static_cast<float>(k2 + 1) / kSteps;
                // From pier face to pier face; the arch springs at the deck less the rise.
                const auto pos = [&](float u, float& x, float& y, float& zt, float& za2) {
                    const float tt = t0 + (t1 - t0) * (0.045f / (len / static_cast<float>(bays)) +
                                                        u * bay / (len / static_cast<float>(bays)));
                    at(tt, x, y, zt);
                    zt -= under;
                    const float a = (u - 0.5f) * 2.0f; // -1 .. 1 across the bay
                    za2 = zt - 0.04f - rise * (1.0f - std::sqrt(std::max(0.0f, 1.0f - a * a)));
                };
                float xa, ya, ta, aa, xb, yb, tb, ab;
                pos(u0, xa, ya, ta, aa);
                pos(u1, xb, yb, tb, ab);
                glColor3fv(stone);
                for (const float sgn : {-1.0f, 1.0f}) {
                    glNormal3f(px * sgn, py * sgn, 0);
                    glVertex3f(xa + px * hw * sgn, ya + py * hw * sgn, aa);
                    glVertex3f(xb + px * hw * sgn, yb + py * hw * sgn, ab);
                    glVertex3f(xb + px * hw * sgn, yb + py * hw * sgn, tb);
                    glVertex3f(xa + px * hw * sgn, ya + py * hw * sgn, ta);
                }
                glColor3f(stone[0] * 0.6f, stone[1] * 0.6f, stone[2] * 0.6f);
                glNormal3f(0, 0, -1);
                glVertex3f(xa - px * hw, ya - py * hw, aa);
                glVertex3f(xa + px * hw, ya + py * hw, aa);
                glVertex3f(xb + px * hw, yb + py * hw, ab);
                glVertex3f(xb - px * hw, yb - py * hw, ab);
            }
            (void)x0; (void)y0; (void)z0; (void)x1; (void)y1; (void)z1;
        }
        // A parapet along both sides of the deck.
        for (const float sgn : {-1.0f, 1.0f}) {
            beam(ax + px * hw * sgn, ay + py * hw * sgn, za + 0.03f, bx + px * hw * sgn, by + py * hw * sgn, zb + 0.03f, 0.022f, coping);
        }
        break;
    }
    case sim::BridgeType::Steel: {
        const float iron[3] = {0.27f, 0.3f, 0.34f};
        const float hw = 0.1f, high = 0.17f;
        const int panels = std::max(1, static_cast<int>(std::ceil(len / 0.16f)));
        for (const float sgn : {-1.0f, 1.0f}) {
            const float ox = px * hw * sgn, oy = py * hw * sgn;
            beam(ax + ox, ay + oy, za, bx + ox, by + oy, zb, 0.016f, iron);                // bottom chord
            beam(ax + ox, ay + oy, za + high, bx + ox, by + oy, zb + high, 0.016f, iron);  // top chord
            for (int i = 0; i <= panels; ++i) {
                float x, y, z;
                at(static_cast<float>(i) / static_cast<float>(panels), x, y, z);
                beam(x + ox, y + oy, z, x + ox, y + oy, z + high, 0.01f, iron); // post
                if (i < panels) {
                    float x2, y2, z2;
                    at(static_cast<float>(i + 1) / static_cast<float>(panels), x2, y2, z2);
                    // Diagonals leaning alternately, a Warren pattern.
                    if (i % 2 == 0) beam(x + ox, y + oy, z, x2 + ox, y2 + oy, z2 + high, 0.008f, iron);
                    else beam(x + ox, y + oy, z + high, x2 + ox, y2 + oy, z2, 0.008f, iron);
                }
            }
        }
        // Overhead lateral bracing between the top chords.
        for (int i = 0; i <= panels; i += 2) {
            float x, y, z;
            at(static_cast<float>(i) / static_cast<float>(panels), x, y, z);
            beam(x - px * hw, y - py * hw, z + high, x + px * hw, y + py * hw, z + high, 0.008f, iron);
        }
        // Piers down to the ground at each end.
        for (const float t : {0.0f, 1.0f}) {
            float x, y, z;
            at(t, x, y, z);
            const float g = ground(x, y);
            if (z - under - g > 0.03f) {
                const float pier[3] = {0.55f, 0.53f, 0.5f};
                box(x, y, g, dx, dy, 0.035f, hw * 1.1f, z - under - g, pier, pier);
            }
        }
        break;
    }
    case sim::BridgeType::Suspension: {
        const float steel[3] = {0.82f, 0.82f, 0.8f}, tower[3] = {0.72f, 0.7f, 0.66f}, cable[3] = {0.3f, 0.3f, 0.33f};
        const float hw = 0.1f;
        const Span sp = span ? *span : Span{ax, ay, za, bx, by, zb};
        const float slen = std::max(1e-3f, std::hypot(sp.x1 - sp.x0, sp.y1 - sp.y0));
        const float high = std::clamp(slen * 0.12f, 0.25f, 0.7f); // the towers above the deck
        // Where along the span (0 to 1) a point lies, and the cable's height there.
        const auto along = [&](float x, float y) {
            return std::clamp(((x - sp.x0) * (sp.x1 - sp.x0) + (y - sp.y0) * (sp.y1 - sp.y0)) / (slen * slen), 0.0f, 1.0f);
        };
        const auto cable_z = [&](float u, float deck) {
            const float c = 2.0f * u - 1.0f;
            return deck + 0.04f + (high - 0.04f) * c * c;
        };
        // Towers, where this edge meets an end of the span.
        for (const float t : {0.0f, 1.0f}) {
            float x, y, z;
            at(t, x, y, z);
            const float u = along(x, y);
            if (u > 0.02f && u < 0.98f) continue;
            const float g = ground(x, y);
            for (const float sgn : {-1.0f, 1.0f}) {
                box(x + px * hw * 1.2f * sgn, y + py * hw * 1.2f * sgn, g, dx, dy, 0.025f, 0.02f, z + high + 0.05f - g, tower, tower);
            }
            beam(x - px * hw * 1.2f, y - py * hw * 1.2f, z + high + 0.03f, x + px * hw * 1.2f, y + py * hw * 1.2f, z + high + 0.03f, 0.03f,
                 tower);
            beam(x - px * hw * 1.2f, y - py * hw * 1.2f, z + 0.12f, x + px * hw * 1.2f, y + py * hw * 1.2f, z + 0.12f, 0.02f, tower);
        }
        // The cables and hangers over this edge, a few pieces for the curve.
        const int pieces = std::max(2, static_cast<int>(std::ceil(len / 0.12f)));
        for (const float sgn : {-1.0f, 1.0f}) {
            const float ox = px * hw * 1.2f * sgn, oy = py * hw * 1.2f * sgn;
            for (int i = 0; i < pieces; ++i) {
                float x0, y0, z0, x1, y1, z1;
                at(static_cast<float>(i) / static_cast<float>(pieces), x0, y0, z0);
                at(static_cast<float>(i + 1) / static_cast<float>(pieces), x1, y1, z1);
                const float c0 = cable_z(along(x0, y0), z0), c1 = cable_z(along(x1, y1), z1);
                beam(x0 + ox, y0 + oy, c0, x1 + ox, y1 + oy, c1, 0.012f, cable);
                beam(x0 + ox * 0.85f, y0 + oy * 0.85f, z0, x0 + ox * 0.85f, y0 + oy * 0.85f, c0, 0.004f, cable);
            }
            // The stiffening girder along the deck edge.
            beam(ax + ox * 0.85f, ay + oy * 0.85f, za + 0.02f, bx + ox * 0.85f, by + oy * 0.85f, zb + 0.02f, 0.02f, steel);
        }
        break;
    }
    }
}

// A tunnel portal at (x, y), rail height z, the tunnel running along
// (dx, dy): a dressed-stone headwall with a round-arched mouth, voussoirs
// round the arch, a coping, and wing walls splayed back into the hill.
// Inside GL_QUADS.
void tunnel_portal(float x, float y, float z, float dx, float dy, bool double_track, const Camera& cam) {
    const float stone[3] = {0.52f, 0.49f, 0.45f}, dressed[3] = {0.66f, 0.63f, 0.58f}, dark[3] = {0.04f, 0.04f, 0.05f};
    const float px = -dy, py = dx;
    const float half = double_track ? 0.24f : 0.15f, open = double_track ? 0.16f : 0.08f;
    const float height = 0.26f, spring = 0.1f; // the headwall's height; where the arch springs
    const float ground = std::min(z, cam.ground_z(x, y)) - 0.05f;
    // The headwall, set into the hill behind the mouth.
    box(x + dx * 0.04f, y + dy * 0.04f, ground, dx, dy, 0.04f, half, z + height - ground, stone, stone);
    box(x + dx * 0.04f, y + dy * 0.04f, z + height, dx, dy, 0.05f, half * 1.06f, 0.02f, dressed, dressed);
    // The mouth: dark, a rectangle to the springing then a half-round arch,
    // just proud of the wall's face; a ring of lighter voussoirs round it.
    const float fx = x - dx * 0.003f, fy = y - dy * 0.003f;
    const auto pt = [&](float across, float up) { glVertex3f(fx + px * across, fy + py * across, z + up); };
    glNormal3f(-dx, -dy, 0);
    glColor3fv(dark);
    pt(-open, 0);
    pt(open, 0);
    pt(open, spring);
    pt(-open, spring);
    constexpr int kSteps = 10;
    for (int i = 0; i < kSteps; ++i) {
        const float a0 = 3.1415927f * static_cast<float>(i) / kSteps, a1 = 3.1415927f * static_cast<float>(i + 1) / kSteps;
        glColor3fv(dark);
        pt(0, spring);
        pt(open * std::cos(a0), spring + open * std::sin(a0));
        pt(open * std::cos(a1), spring + open * std::sin(a1));
        pt(0, spring);
        glColor3fv(i % 2 == 0 ? dressed : stone);
        const float r0 = open, r1 = open * 1.28f;
        glVertex3f(fx - dx * 0.001f + px * r0 * std::cos(a0), fy - dy * 0.001f + py * r0 * std::cos(a0), z + spring + r0 * std::sin(a0));
        glVertex3f(fx - dx * 0.001f + px * r1 * std::cos(a0), fy - dy * 0.001f + py * r1 * std::cos(a0), z + spring + r1 * std::sin(a0));
        glVertex3f(fx - dx * 0.001f + px * r1 * std::cos(a1), fy - dy * 0.001f + py * r1 * std::cos(a1), z + spring + r1 * std::sin(a1));
        glVertex3f(fx - dx * 0.001f + px * r0 * std::cos(a1), fy - dy * 0.001f + py * r0 * std::cos(a1), z + spring + r0 * std::sin(a1));
    }
    // Wing walls, splayed out and stepping down.
    for (const float side : {-1.0f, 1.0f}) {
        const float wx0 = x + px * half * side, wy0 = y + py * half * side;
        const float wx1 = wx0 - dx * 0.16f + px * 0.08f * side, wy1 = wy0 - dy * 0.16f + py * 0.08f * side;
        beam(wx0, wy0, z + height * 0.55f, wx1, wy1, z + height * 0.2f, 0.035f, stone);
        beam(wx0, wy0, z + height * 0.2f, wx1, wy1, ground + 0.04f, 0.04f, stone);
    }
}

// Track close up (rt3-clone-spec §15.6 views): a ballast bed with sleepers
// and two rails on each track, following the ground in cuttings; bridges as
// decks on piers; tunnels unseen but for their mouths.
void track_body(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player, bool sleepers) {
    const sim::TrackNetwork& net = rw.track();
    const auto tile = [&](std::int64_t mm) { return static_cast<float>(mm) / cam.mm_per_tile; };
    struct Strip {
        float x0, y0, z0, x1, y1, z1; // a straight bit of one edge
    };
    const BridgeSpans spans = bridge_spans(net, cam);
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
        // The bridge's structure, by type, from the deck down to the ground.
        if (e.kind == sim::TrackKind::Bridge) {
            const int si = spans.of_edge[static_cast<std::size_t>(&e - net.edges().data())];
            bridge_structure(e, ax, ay, za, bx, by, zb, si >= 0 ? &spans.spans[static_cast<std::size_t>(si)] : nullptr, cam);
            glNormal3f(0, 0, 1);
        }
    }
    // Tunnel mouths, where a tunnel meets track in the open.
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
            tunnel_portal(hx, hy, z, dx, dy, e.double_track, cam);
        }
    }
    glEnd();
}

// The rails themselves: thin steel lines on each track.
void track_rails(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player) {
    const sim::TrackNetwork& net = rw.track();
    const auto tile = [&](std::int64_t mm) { return static_cast<float>(mm) / cam.mm_per_tile; };
    {
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

// What the drawn track depends on: every edge, its shape and owner.
std::uint64_t railway_key(const sim::Railway& rw, std::optional<sim::CompanyId> player, const Camera& cam) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    const sim::TrackNetwork& net = rw.track();
    for (const sim::TrackEdge& e : net.edges()) {
        mix(e.a);
        mix(e.b);
        mix((e.removed ? 1u : 0u) | (e.double_track ? 2u : 0u) | (static_cast<unsigned>(e.kind) << 2) |
            (static_cast<unsigned>(e.bridge) << 6));
        mix(e.owner);
    }
    for (const sim::TrackNode& n : net.nodes()) {
        mix(static_cast<std::uint64_t>(n.pos.x_mm));
        mix(static_cast<std::uint64_t>(n.pos.y_mm));
        mix(static_cast<std::uint64_t>(n.z_mm));
    }
    mix(player ? *player + 1u : 0u);
    mix(static_cast<std::uint64_t>(cam.height_exaggeration * 1000));
    mix(reinterpret_cast<std::uintptr_t>(cam.terrain));
    return h;
}

// Track close up, as display lists rebuilt only when the railway changes:
// the beds (with or without sleepers, by zoom) and the rails.
void track_3d(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player) {
    struct Cached {
        GLuint list = 0;
        std::uint64_t key = 0;
    };
    static Cached beds[2], rails;
    const std::uint64_t key = railway_key(rw, player, cam);
    Cached& bed = beds[cam.zoom >= 26.0f ? 1 : 0];
    if (bed.list == 0 || bed.key != key) {
        if (bed.list == 0) bed.list = glGenLists(1);
        glNewList(bed.list, GL_COMPILE);
        track_body(rw, cam, player, cam.zoom >= 26.0f);
        glEndList();
        bed.key = key;
    }
    begin_lit();
    glCallList(bed.list);
    end_lit();
    if (cam.zoom >= 14.0f) {
        if (rails.list == 0 || rails.key != key) {
            if (rails.list == 0) rails.list = glGenLists(1);
            glNewList(rails.list, GL_COMPILE);
            track_rails(rw, cam, player);
            glEndList();
            rails.key = key;
        }
        glLineWidth(cam.zoom >= 40.0f ? 2.0f : 1.0f);
        // Lit from above like the ground, so they darken after dark.
        begin_lit();
        glNormal3f(0, 0, 1);
        glCallList(rails.list);
        end_lit();
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

// How far a point (tiles) is from the nearest track, in tiles.
float track_distance(const sim::TrackNetwork& net, const Camera& cam, float x, float y) {
    float best = 1e9f;
    for (const sim::TrackEdge& e : net.edges()) {
        if (e.removed) continue;
        const sim::TrackNode& a = net.node(e.a);
        const sim::TrackNode& b = net.node(e.b);
        const float ax = tiles(a.pos.x_mm, cam), ay = tiles(a.pos.y_mm, cam), bx = tiles(b.pos.x_mm, cam), by = tiles(b.pos.y_mm, cam);
        const float ex = bx - ax, ey = by - ay, len2 = ex * ex + ey * ey;
        const float t = len2 > 0 ? std::clamp(((x - ax) * ex + (y - ay) * ey) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, std::hypot(x - ax - ex * t, y - ay - ey * t));
    }
    return best;
}

// Where a water tower or engine shed stands by its node (tiles): set back
// from the track (direction (dx, dy)) on the side away from the station
// house if that is clear of other track, else the other side, or along.
// `slot` counts the buildings already placed at the node: each takes the
// next clear spot, so a tower and a shed stand apart.
void service_spot(const sim::TrackNetwork& net, const Camera& cam, sim::NodeId node, float dx, float dy, float s, int slot,
                  float& x, float& y) {
    const sim::TrackNode& n = net.node(node);
    const float nx = tiles(n.pos.x_mm, cam), ny = tiles(n.pos.y_mm, cam);
    const float back = 0.2f + 0.08f * s, clearance = 0.12f * s + 0.08f;
    x = nx - dy * back + dx * 0.35f * s * static_cast<float>(slot);
    y = ny + dx * back + dy * 0.35f * s * static_cast<float>(slot);
    int skip = slot;
    for (const float side : {1.0f, -1.0f}) {
        for (const float shift : {0.0f, 0.35f, -0.35f, 0.7f, -0.7f}) {
            const float cx = nx - dy * back * side + dx * shift * s, cy = ny + dx * back * side + dy * shift * s;
            if (track_distance(net, cam, cx, cy) < clearance) continue;
            if (skip-- > 0) continue;
            x = cx;
            y = cy;
            return;
        }
    }
}

// Which of the buildings at its node a service building is (0 for the first).
int service_slot(const sim::Railway& rw, const sim::ServiceBuilding& b) {
    int slot = 0;
    for (const sim::ServiceBuilding& other : rw.service_buildings()) {
        if (&other == &b) break;
        if (other.node == b.node) ++slot;
    }
    return slot;
}

// The direction of the track at a node, for lining buildings up with it.
void track_along(const sim::TrackNetwork& net, sim::NodeId node, float& dx, float& dy) {
    dx = 1;
    dy = 0;
    for (sim::EdgeId id : net.edges_at(node)) {
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
            yards_.push_back({tile(n.pos.x_mm), tile(n.pos.y_mm), reach});
        }
        for (const sim::StationBuilding& b : rw.station_buildings()) {
            mark(tile(b.pos.x_mm), tile(b.pos.y_mm), 0.12f * s);
            yards_.push_back({tile(b.pos.x_mm), tile(b.pos.y_mm), 0.12f * s});
        }
        for (const sim::ServiceBuilding& b : rw.service_buildings()) {
            float dx, dy, x, y;
            track_along(net, b.node, dx, dy);
            service_spot(net, cam, b.node, dx, dy, s, service_slot(rw, b), x, y);
            mark(x, y, 0.1f + 0.2f * s);
            yards_.push_back({x, y, 0.1f + 0.2f * s});
        }
    }
    // Is the point (tiles) within a station's grounds or a railway
    // building's? Streets stop short of them (they may cross plain track).
    bool in_yard(float x, float y) const {
        return std::any_of(yards_.begin(), yards_.end(), [&](const std::array<float, 3>& d) {
            return std::hypot(x - d[0], y - d[1]) <= d[2] + 0.06f;
        });
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
    std::vector<std::array<float, 3>> yards_; // stations and railway buildings: centre and reach, tiles
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
            // In pieces, leaving out what would run through a station's
            // grounds or a railway building.
            const int steps = std::max(1, static_cast<int>(std::hypot(x1 - x0, y1 - y0) / 0.04f));
            std::optional<float> from;
            const auto emit = [&](float t0, float t1) {
                if ((t1 - t0) * std::hypot(x1 - x0, y1 - y0) < 0.08f) return; // a stub: leave it out
                out.streets.insert(out.streets.end(), {x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1});
            };
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const bool clear = !taken.in_yard(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
                if (clear && !from) from = t;
                if (!clear && from) {
                    emit(*from, std::max(*from, t - 1.0f / static_cast<float>(steps)));
                    from.reset();
                }
            }
            if (from) emit(*from, 1.0f);
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
// What waits on a station's platform, drawn each frame: a figure for every
// quarter load of passengers (up to two dozen) in coats of many colours,
// mail sacks at one end and freight crates at the other, so a busy station
// looks busy. Inside GL_QUADS; (x, y, z) and (dx, dy) as draw_station.
void platform_life(const sim::Station& st, const sim::CargoRegistry& cargo, float x, float y, float z, float dx, float dy,
                   float s) {
    const std::optional<sim::CargoId> passengers = cargo.find("passengers"), mail = cargo.find("mail");
    std::int64_t pax = 0, post = 0, freight = 0;
    for (const sim::ExpressWaiting& e : st.express) {
        if (e.cargo == passengers) pax += e.milli;
        else if (e.cargo == mail) post += e.milli;
        else freight += e.milli; // troops wait with the freight, in crates of kit
    }
    for (const sim::WaitingCargo& c : st.waiting) freight += c.milli;
    const int people = static_cast<int>(std::min<std::int64_t>(24, pax / 250));
    const int sacks = static_cast<int>(std::min<std::int64_t>(10, post / 250));
    const int crates = static_cast<int>(std::min<std::int64_t>(12, freight / 400));
    if (people + sacks + crates == 0) return;
    // As draw_station lays out its near platform.
    const bool small = st.size == sim::StationSize::Small, large = st.size == sim::StationSize::Large;
    const float len = (large ? 0.5f : small ? 0.24f : 0.34f) * s, pw = 0.03f * s, ph = 0.03f, off = 0.15f + pw;
    const float qx = -dy, qy = dx, top = z + ph;
    const auto at = [&](float along, float across, float& ox, float& oy) {
        ox = x + dx * along - qx * across;
        oy = y + dy * along - qy * across;
    };
    static constexpr float kCoats[][3] = {{0.2f, 0.22f, 0.35f}, {0.45f, 0.12f, 0.1f}, {0.18f, 0.3f, 0.2f}, {0.3f, 0.26f, 0.22f},
                                         {0.55f, 0.5f, 0.42f},  {0.12f, 0.12f, 0.13f}, {0.5f, 0.35f, 0.15f}, {0.35f, 0.2f, 0.4f}};
    const float skin[3] = {0.9f, 0.72f, 0.58f}, hat[3] = {0.1f, 0.09f, 0.08f};
    float cx, cy;
    for (int i = 0; i < people; ++i) {
        // Spread along the middle of the platform, in loose groups.
        const std::uint32_t h = hash(st.id, static_cast<std::uint32_t>(i), 41);
        const float along = (unit(h) * 1.4f - 0.7f) * len, across = off + (unit(h >> 16) - 0.5f) * pw * 1.2f;
        at(along, across, cx, cy);
        const float* coat = kCoats[(h >> 8) % 8];
        // A little larger than life, like the lamps, so they read at a distance.
        const float tall = (0.024f + 0.006f * unit(h >> 4)) * s;
        box(cx, cy, top, dx, dy, 0.0052f * s, 0.0045f * s, tall, coat, coat);
        box(cx, cy, top + tall, dx, dy, 0.0037f * s, 0.0037f * s, 0.0075f * s, skin, skin);
        if ((h >> 12) % 3 == 0) box(cx, cy, top + tall + 0.0075f * s, dx, dy, 0.0048f * s, 0.0048f * s, 0.003f * s, hat, hat);
    }
    const float sack[3] = {0.72f, 0.66f, 0.5f}, sack_top[3] = {0.8f, 0.74f, 0.58f};
    for (int i = 0; i < sacks; ++i) {
        // Heaped at the far end, two deep.
        at(len * 0.82f - static_cast<float>(i / 2) * 0.013f * s, off + (i % 2 == 0 ? -0.3f : 0.3f) * pw, cx, cy);
        box(cx, cy, top, dx, dy, 0.006f * s, 0.006f * s, 0.009f * s, sack, sack_top);
    }
    const float wood[3] = {0.55f, 0.4f, 0.22f}, wood_top[3] = {0.62f, 0.47f, 0.27f};
    for (int i = 0; i < crates; ++i) {
        // Stacked three high at the near end.
        const int column = i / 3, level = i % 3;
        at(-len * 0.8f + static_cast<float>(column) * 0.022f * s, off, cx, cy);
        box(cx, cy, top + static_cast<float>(level) * 0.017f * s, dx, dy, 0.009f * s, 0.009f * s, 0.017f * s, wood, wood_top);
    }
}

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
            add_light(cx, cy, z + ph + 0.075f * s, 0.05f * s, 1.0f, 0.78f, 0.4f);
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

// `art`: trees from the models where there are some (near the camera), or
// always the light ones drawn in code (far off).
// The trees standing on a tile: `f(i, hash, x, y, size)` for each (x, y
// in tiles), woods by noise and forest ground, clear of what `wooded` holds.
template <typename Ground, typename F> void tile_trees(const Ground& t, const Occupancy& wooded, int tx, int ty, F f) {
    const sim::GroundType g = t.ground(tx, ty);
    if (g == sim::GroundType::Water || g == sim::GroundType::Desert) return;
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
        f(i, hh, x, y, 0.65f + 0.4f * unit(hh >> 8));
    }
}

void build_tree_chunk(const sim::Terrain& t, const Occupancy& wooded, const Camera& cam, int cx0, int cy0, bool art_trees) {
    const float trunk[3] = {0.36f, 0.26f, 0.17f};
    struct ArtTree {
        const models::Model* model;
        float x, y, z, fx, fy, height;
    };
    std::vector<ArtTree> art;
    glBegin(GL_QUADS);
    for (int ty = cy0; ty < std::min(cy0 + kTreeChunk, t.height()); ++ty) {
        for (int tx = cx0; tx < std::min(cx0 + kTreeChunk, t.width()); ++tx) {
            tile_trees(t, wooded, tx, ty, [&](int i, std::uint32_t hh, float x, float y, float size) {
                const float base = cam.ground_z(x, y) - 0.01f;
                const float shade = 0.82f + 0.3f * unit(hh >> 4);
                // Stands of conifers among broadleaf woods, and the odd one mixed in.
                const float pick = unit(hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), static_cast<std::uint32_t>(i + 211)));
                const bool conifer = map_noise(x, y, 18.0f, 63) > 0.58f ? pick < 0.85f : pick < 0.12f;
                // A tree from the art where there is some, drawn after this batch.
                if (const models::Model* m = art_trees ? models::pick(conifer ? "tree_conifer" : "tree_broadleaf", hh >> 3) : nullptr) {
                    const float yaw = unit(hh >> 6) * 6.2831853f;
                    art.push_back({m, x, y, base + 0.01f, std::cos(yaw), std::sin(yaw), 0.42f * size});
                    return;
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
            });
        }
    }
    glEnd();
    for (const ArtTree& a : art) models::draw_to_height(*a.model, a.x, a.y, a.z, a.fx, a.fy, a.height);
}

// The chunks near the camera, each a display list built when first seen or
// after the scene changes (a few a frame, so building track never stalls).
void draw_trees(const sim::Terrain& t, const Scene& sc, const Camera& cam) {
    // Each chunk keeps two lists: near the camera its trees come from the
    // art, further off the light code-drawn ones (a few hundred triangles
    // a tree against a few dozen), so wide views stay quick.
    struct Lod {
        GLuint list = 0;
        std::uint64_t version = 0;
        std::uint64_t terrain = 0;
    };
    struct Chunk {
        Lod near, far;
    };
    static std::vector<Chunk> chunks;
    const int across = (t.width() + kTreeChunk - 1) / kTreeChunk;
    const int down = (t.height() + kTreeChunk - 1) / kTreeChunk;
    if (chunks.size() != static_cast<std::size_t>(across * down)) {
        for (Chunk& c : chunks) {
            for (Lod* l : {&c.near, &c.far}) if (l->list != 0) glDeleteLists(l->list, 1);
        }
        chunks.assign(static_cast<std::size_t>(across * down), Chunk{});
    }
    static std::uint64_t terrain = 0;
    static const sim::Terrain* terrain_of = nullptr;
    if (terrain_of != &t || terrain == 0) {
        terrain = terrain_key(t, cam);
        terrain_of = &t;
    }
    const bool art = models::has("tree_broadleaf") || models::has("tree_conifer");
    const float reach = std::min(cam.distance * 3.0f + 30.0f, 220.0f);
    const float detail = std::min(cam.distance * 1.4f + 18.0f, 60.0f);
    int builds = 0;
    for (int cy = 0; cy < down; ++cy) {
        for (int cx = 0; cx < across; ++cx) {
            const float mx = (static_cast<float>(cx) + 0.5f) * kTreeChunk, my = (static_cast<float>(cy) + 0.5f) * kTreeChunk;
            const float d = std::hypot(mx - cam.target_x, my - cam.target_y);
            if (d > reach + kTreeChunk) continue;
            const bool near = art && d < detail + kTreeChunk * 0.5f;
            Chunk& c = chunks[static_cast<std::size_t>(cy * across + cx)];
            Lod& l = near ? c.near : c.far;
            const bool stale = l.list == 0 || l.version != sc.version || l.terrain != terrain;
            // A few rebuilt a frame, so building track never stalls; one
            // never drawn yet is built at once.
            if (stale && (builds < 6 || l.list == 0)) {
                if (l.list == 0) l.list = glGenLists(1);
                glNewList(l.list, GL_COMPILE);
                build_tree_chunk(t, *sc.wooded, cam, cx * kTreeChunk, cy * kTreeChunk, near);
                glEndList();
                l.version = sc.version;
                l.terrain = terrain;
                ++builds;
            }
            glCallList(l.list);
        }
    }
}

// After dark, lit windows on the street fronts of the town's buildings:
// a few on each, by the plot, warm yellow.
void town_lights(const TownLayout& towns, const Camera& cam) {
    if (shading::night() <= 0.05f) return;
    for (const Plot& p : towns.plots) {
        const float fx = -p.dy * p.front, fy = p.dx * p.front;
        const float cell = p.hl * 0.7f;
        const float z = cam.ground_z(p.x, p.y);
        const int floors = std::max(1, p.floors);
        for (int f = 0; f < floors; ++f)
            for (const float along : {-0.6f, 0.0f, 0.6f}) {
                const std::uint32_t h = hash(p.seed, static_cast<std::uint32_t>(f * 3 + static_cast<int>(along * 10 + 10)), 41);
                if (unit(h) < 0.45f) continue; // a dark room
                const float wx = p.x + fx * (p.hw * 1.1f) + p.dx * p.hl * along, wy = p.y + fy * (p.hw * 1.1f) + p.dy * p.hl * along;
                add_light(wx, wy, z + cell * (static_cast<float>(f) + 0.5f), cell * 0.35f, 1.0f, 0.72f, 0.35f);
            }
    }
}

} // namespace

// The land's height at every map corner (tiles, as the mesh draws it), for
// the shader to tell how far above the ground a point stands.
void upload_heights(const sim::Terrain& t, const Camera& cam) {
    if (!shading::active()) return;
    const int cw = t.width() + 1, ch = t.height() + 1;
    const float scale = cam.height_exaggeration / static_cast<float>(t.tile_size_m());
    std::vector<float> z(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch));
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) z[static_cast<std::size_t>(y) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(x)] = static_cast<float>(t.corner_height(x, y)) * scale;
    const auto [lo, hi] = std::minmax_element(z.begin(), z.end());
    const float z_min = *lo, range = std::max(1e-4f, *hi - *lo);
    std::vector<std::uint16_t> texels(z.size());
    for (std::size_t i = 0; i < z.size(); ++i) texels[i] = static_cast<std::uint16_t>(std::lround((z[i] - z_min) / range * 65535.0f));
    static GLuint tex = 0;
    if (tex == 0) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE16, cw, ch, 0, GL_LUMINANCE, GL_UNSIGNED_SHORT, texels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, shading::white_texture());
    shading::set_heights(tex, cw, ch, z_min, range);
}

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
    g_terrain_key = key;
    if (mesh.list == 0 || mesh.key != key) {
        // Made now: a texture upload inside the list would only be recorded.
        ground_texture();
        water_texture();
        if (mesh.list == 0) mesh.list = glGenLists(1);
        glNewList(mesh.list, GL_COMPILE);
        build_terrain_mesh(t, cam);
        glEndList();
        mesh.key = key;
        upload_heights(t, cam);
    }
    begin_lit();
    shading::set_ground(true);
    glCallList(mesh.list);
    shading::set_ground(false);
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
    // From afar, track lines lit from above (they darken after dark).
    if (cam.three_d && !detailed) {
        begin_lit();
        glNormal3f(0, 0, 1);
    }
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
    if (cam.three_d && !detailed) end_lit();
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
            if (std::hypot(x - cam.target_x, y - cam.target_y) < cam.distance * 2.5f + 10.0f) {
                platform_life(st, w.data().cargo, x, y, z, dx, dy, s);
            }
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
            const float nx = tiles(n.pos.x_mm, cam), ny = tiles(n.pos.y_mm, cam);
            float x, y;
            service_spot(net, cam, b.node, dx, dy, s, service_slot(rw, b), x, y);
            const float z = cam.ground_z(x, y);
            // From the art where there is some, facing the track.
            const bool tower = b.type == sim::ServiceType::ServiceTower;
            if (const models::Model* m = models::pick(tower ? "service_tower" : "engine_shed", b.node)) {
                // Facing the track it serves.
                const float tl = std::max(1e-4f, std::hypot(nx - x, ny - y));
                const float fx = (nx - x) / tl, fy = (ny - y) / tl;
                glEnd();
                if (tower) models::draw_to_height(*m, x, y, z - 0.005f, fx, fy, 0.26f * s);
                else models::draw(*m, x, y, z - 0.005f, fx, fy, 0.34f * s);
                glBegin(GL_QUADS);
            } else if (tower) {
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
            // Engines from the art are told apart by their paint: the
            // player's always the first model, rivals' the others; a
            // pennant in the owner's colour flies over the cab.
            const std::uint32_t seed = hash(t.id, 17, 5);
            const std::uint32_t engine_seed = t.owner == player ? 0u : 1u + t.owner % 2u;
            const bool art_engine = models::has(stock_role(engine));
            float back = stock(engine, x, y, z, p.dx, p.dy, s, c, engine_seed) + gap;
            if (art_engine) {
                float flag[3];
                owner_rgb(t.owner, player, flag[0], flag[1], flag[2]);
                if (t.owner == player) {
                    flag[0] = 0.95f; flag[1] = 0.85f; flag[2] = 0.2f; // the player's in gold
                }
                const float pole[3] = {0.2f, 0.2f, 0.2f};
                const float fx = x - p.dx * back * 0.75f, fy = y - p.dy * back * 0.75f, top = z + 0.2f * s;
                box(fx, fy, top - 0.04f * s, p.dx, p.dy, 0.003f * s, 0.003f * s, 0.07f * s, pole, pole);
                box(fx - p.dx * 0.02f * s, fy - p.dy * 0.02f * s, top + 0.01f * s, p.dx, p.dy, 0.02f * s, 0.002f * s, 0.018f * s,
                    flag, flag);
            }
            add_light(x + p.dx * 0.01f * s, y + p.dy * 0.01f * s, z + 0.1f * s, 0.06f * s, 1.0f, 0.9f, 0.6f, p.dx, p.dy);
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

// Ambient occlusion on the ground (shaders): how much of the sky each spot
// sees past what stands round it. What covers the land (trees, houses,
// industries, stations) is drawn into a grid a few texels a tile, blurred
// so it spreads a little way out, and the shader dims the ground's ambient
// light (and some of the sun) by it: woods get dark floors, and buildings
// stand in a soft pool instead of floating. Rebuilt with the scene, on a
// worker thread from a copy of what it needs, so building never stalls.
struct OcclusionInput {
    struct Ground {
        int w = 0, h = 0;
        std::vector<sim::GroundType> g;
        sim::GroundType ground(int x, int y) const { return g[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]; }
    } ground;
    std::optional<Occupancy> wooded; // empty: no trees
    std::vector<Plot> plots;
    std::vector<std::array<float, 3>> squares; // industries: centre and half side, tiles
    std::vector<std::array<float, 3>> discs;   // stations: centre and radius, tiles
};

struct Occlusion {
    int w = 0, h = 0;             // texels
    int tiles_w = 0, tiles_h = 0; // the map it covers
    std::vector<unsigned char> sky;
};

Occlusion compute_occlusion(const OcclusionInput& in) {
    const int w = in.ground.w, h = in.ground.h;
    const int r = std::clamp(1536 / std::max(w, h), 2, 6); // texels a tile
    const int tw = w * r, th = h * r;
    std::vector<float> cover(static_cast<std::size_t>(tw) * static_cast<std::size_t>(th), 0.0f);
    const auto fr = static_cast<float>(r);
    // Cover `v` over the points (tiles) within `reach` of the centre that are `inside`.
    const auto stamp = [&](float x, float y, float reach, float v, auto inside) {
        const int x0 = std::max(0, static_cast<int>((x - reach) * fr)), x1 = std::min(tw - 1, static_cast<int>((x + reach) * fr));
        const int y0 = std::max(0, static_cast<int>((y - reach) * fr)), y1 = std::min(th - 1, static_cast<int>((y + reach) * fr));
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx) {
                const float px = (static_cast<float>(xx) + 0.5f) / fr - x, py = (static_cast<float>(yy) + 0.5f) / fr - y;
                float& c = cover[static_cast<std::size_t>(yy) * static_cast<std::size_t>(tw) + static_cast<std::size_t>(xx)];
                if (inside(px, py)) c = std::max(c, v);
            }
    };
    const auto disc = [&](float x, float y, float rad, float v) {
        // At least a texel, so small trees still count.
        const float rr = std::max(rad, 0.5f / fr);
        stamp(x, y, rr, v, [&](float px, float py) { return px * px + py * py <= rr * rr; });
    };
    if (in.wooded) {
        for (int ty = 0; ty < h; ++ty)
            for (int tx = 0; tx < w; ++tx)
                tile_trees(in.ground, *in.wooded, tx, ty, [&](int, std::uint32_t, float x, float y, float size) { disc(x, y, 0.15f * size, 0.9f); });
    }
    for (const Plot& p : in.plots) {
        stamp(p.x, p.y, p.hl + p.hw, 1.0f, [&](float px, float py) {
            return std::fabs(px * p.dx + py * p.dy) <= p.hl && std::fabs(-px * p.dy + py * p.dx) <= p.hw;
        });
    }
    for (const auto& [x, y, half] : in.squares) {
        stamp(x, y, half, 0.6f, [&](float px, float py) { return std::fabs(px) <= half && std::fabs(py) <= half; });
    }
    for (const auto& [x, y, rad] : in.discs) disc(x, y, rad, 0.6f);
    // Two box blurs, each about half a tile each way: roughly a Gaussian,
    // so the dark fades out softly.
    const int br = std::max(1, r / 2);
    const float n = 1.0f / static_cast<float>(2 * br + 1);
    std::vector<float> tmp(cover.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < th; ++y) {
            const float* src = &cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(tw)];
            float* out = &tmp[static_cast<std::size_t>(y) * static_cast<std::size_t>(tw)];
            float sum = 0;
            for (int x = -br; x <= br; ++x) sum += src[std::clamp(x, 0, tw - 1)];
            for (int x = 0; x < tw; ++x) {
                out[x] = sum * n;
                sum += src[std::min(x + br + 1, tw - 1)] - src[std::max(x - br, 0)];
            }
        }
        // Down the columns a row at a time, keeping each column's running
        // sum, so memory is read in order.
        const auto row = [&](int y) { return &tmp[static_cast<std::size_t>(std::clamp(y, 0, th - 1)) * static_cast<std::size_t>(tw)]; };
        std::vector<float> sums(static_cast<std::size_t>(tw), 0.0f);
        for (int y = -br; y <= br; ++y) {
            const float* r0 = row(y);
            for (int x = 0; x < tw; ++x) sums[static_cast<std::size_t>(x)] += r0[x];
        }
        for (int y = 0; y < th; ++y) {
            float* out = &cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(tw)];
            const float *add = row(y + br + 1), *drop = row(y - br);
            for (int x = 0; x < tw; ++x) {
                out[x] = sums[static_cast<std::size_t>(x)] * n;
                sums[static_cast<std::size_t>(x)] += add[x] - drop[x];
            }
        }
    }
    Occlusion o{tw, th, w, h, std::vector<unsigned char>(cover.size())};
    for (std::size_t i = 0; i < cover.size(); ++i) o.sky[i] = static_cast<unsigned char>(255.0f * (1.0f - 0.65f * std::min(1.0f, cover[i] * 2.2f)));
    return o;
}

void update_occlusion(const sim::Economy& eco, const sim::IndustryRegistry& industries, const sim::Railway& rw,
                      const Scene& sc, const Camera& cam) {
    if (!shading::active() || !cam.terrain) return;
    const sim::Terrain& t = *cam.terrain;
    static std::uint64_t wanted = 0;
    static std::future<Occlusion> job;
    static GLuint tex = 0;
    const auto upload = [&](const Occlusion& o) {
        if (tex == 0) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Already blurred, so it needs no mipmaps to look smooth far off.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, o.w, o.h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, o.sky.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, shading::white_texture()); // what untextured drawing expects
        shading::set_occlusion(tex, 1.0f / static_cast<float>(o.tiles_w), 1.0f / static_cast<float>(o.tiles_h));
    };
    // A finished job goes up as soon as it is ready.
    if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) upload(job.get());
    const std::uint64_t key = (sc.version * 1099511628211ull ^ g_terrain_key) * 2 + (cam.show_trees ? 1 : 0);
    if (wanted == key || job.valid()) return; // up to date, or one is under way (the next frame starts the newer)
    const bool first = wanted == 0;
    wanted = key;
    OcclusionInput in;
    in.ground.w = t.width();
    in.ground.h = t.height();
    in.ground.g.resize(static_cast<std::size_t>(t.width()) * static_cast<std::size_t>(t.height()));
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x) in.ground.g[static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width()) + static_cast<std::size_t>(x)] = t.ground(x, y);
    if (cam.show_trees) in.wooded = *sc.wooded;
    in.plots = sc.towns.plots;
    const auto k = static_cast<float>(eco.cells_per_node());
    for (const sim::Site& s : eco.sites()) {
        if (s.closed || industries.get(s.type).kind == sim::IndustryKind::House) continue;
        in.squares.push_back({(static_cast<float>(s.cx) + 0.5f) * k, (static_cast<float>(s.cy) + 0.5f) * k, 0.25f * k});
    }
    const float s = object_scale(cam);
    for (const sim::Station& st : rw.stations()) {
        const sim::TrackNode& node = rw.track().node(st.node);
        const float reach = (st.size == sim::StationSize::Large ? 0.4f : st.size == sim::StationSize::Small ? 0.2f : 0.3f) * s;
        in.discs.push_back({static_cast<float>(node.pos.x_mm) / cam.mm_per_tile, static_cast<float>(node.pos.y_mm) / cam.mm_per_tile, reach});
    }
    // The first is made at once, so the view never opens without it.
    if (first) upload(compute_occlusion(in));
    else job = std::async(std::launch::async, [in = std::move(in)] { return compute_occlusion(in); });
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
        update_occlusion(eco, industries, rw, sc, cam);
        const TownLayout& towns = sc.towns;
        // Towns and industries change only with the scene (and the streets
        // with the era), so they are compiled into a display list; the
        // chimneys they register for smoke are kept and replayed each frame.
        static GLuint list = 0;
        static std::uint64_t list_key = 0;
        static std::vector<SmokeSource> list_smoke;
        static std::vector<Turning> list_turning;
        const auto draw_turning = [&] {
            for (const Turning& t : list_turning) {
                if (t.to_height) models::draw_spinners_to_height(*t.model, t.x, t.y, t.z, t.fx, t.fy, t.size, g_animation_seconds);
                else models::draw_spinners(*t.model, t.x, t.y, t.z, t.fx, t.fy, t.size, g_animation_seconds);
            }
        };
        const std::uint64_t key = sc.version * 4 + (year < 1890 ? 0u : year < 1930 ? 1u : 2u);
        begin_lit();
        if (list != 0 && list_key == key) {
            glCallList(list);
            draw_turning();
            g_smoke.insert(g_smoke.end(), list_smoke.begin(), list_smoke.end());
            town_lights(towns, cam);
            if (cam.terrain && cam.show_trees) {
                shading::set_foliage(true);
                draw_trees(*cam.terrain, sc, cam);
                shading::set_foliage(false);
            }
            end_lit();
            return;
        }
        if (list == 0) list = glGenLists(1);
        const std::size_t smoke_before = g_smoke.size();
        g_turning_build.clear();
        glNewList(list, GL_COMPILE_AND_EXECUTE);
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
                // In place of the shapes drawn in code, in proportion.
                glEnd();
                models::draw(*m, x0, y0, cam.ground_z(x0, y0) - 0.01f, 0, 1, 0.7f * k);
                if (!m->spinners.empty()) g_turning_build.push_back({m, x0, y0, cam.ground_z(x0, y0) - 0.01f, 0, 1, 0.7f * k, false});
                glBegin(GL_QUADS);
            } else {
                industry_3d(t, x0, y0, k, seed, cam);
            }
            if (const models::Model* m = models::pick("props:" + t.key, seed)) {
                // Props in the opposite corner (log piles, ore heaps, crates).
                const float bx = x0 - 0.28f * k, by = y0 + 0.28f * k;
                glEnd();
                models::draw(*m, bx, by, cam.ground_z(bx, by) - 0.005f, 0, 1, 0.45f * k);
                glBegin(GL_QUADS);
            }
            if (const models::Model* m = models::pick("beside:" + t.key, seed)) {
                // Beside them (a windmill by the fields).
                const float bx = x0 + 0.3f * k, by = y0 - 0.3f * k;
                glEnd();
                models::draw_to_height(*m, bx, by, cam.ground_z(bx, by) - 0.01f, 1, 0, 0.55f * k);
                if (!m->spinners.empty()) g_turning_build.push_back({m, bx, by, cam.ground_z(bx, by) - 0.01f, 1, 0, 0.55f * k, true});
                glBegin(GL_QUADS);
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
        glEndList();
        list_key = key;
        list_smoke.assign(g_smoke.begin() + static_cast<std::ptrdiff_t>(smoke_before), g_smoke.end());
        list_turning = g_turning_build;
        draw_turning();
        town_lights(towns, cam);
        if (cam.terrain && cam.show_trees) {
                shading::set_foliage(true);
                draw_trees(*cam.terrain, sc, cam);
                shading::set_foliage(false);
            }
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
    // Nearest towns first; a label that would overlap one already placed
    // is left out, and far ones fade, so the horizon does not fill up.
    struct Label {
        std::size_t town;
        float sx, sy, dist;
    };
    std::vector<Label> labels;
    for (std::size_t i = 0; i < eco.towns().size(); ++i) {
        const sim::Town& t = eco.towns()[i];
        float sx = 0, sy = 0;
        const sim::MapPoint c = eco.node_centre(t.cx, t.cy);
        // Over the town: above it in 3D, north of it in 2D.
        const bool shown = cam.three_d ? cam.to_screen(c, sx, sy)
                                       : cam.to_screen({c.x_mm, c.y_mm - static_cast<std::int64_t>(3.0f * cell_mm)}, sx, sy);
        if (!shown) continue;
        if (cam.three_d) sy -= 30;
        const float tx = static_cast<float>(c.x_mm) / cell_mm, ty = static_cast<float>(c.y_mm) / cell_mm;
        labels.push_back({i, sx, sy, std::hypot(tx - cam.target_x, ty - cam.target_y)});
    }
    std::sort(labels.begin(), labels.end(), [](const Label& l, const Label& r) { return l.dist < r.dist; });
    struct Box {
        float x0, y0, x1, y1;
    };
    std::vector<Box> placed;
    const float fade_from = cam.distance * 2.0f, fade_to = cam.distance * 5.0f;
    for (const Label& l : labels) {
        const sim::Town& t = eco.towns()[l.town];
        const float alpha = cam.three_d ? std::clamp((fade_to - l.dist) / std::max(1.0f, fade_to - fade_from), 0.0f, 1.0f) : 1.0f;
        if (alpha <= 0.02f) continue;
        const std::string& label = t.name;
        const int stars = sim::town_stars(eco.town_houses(l.town), towns);
        // The name, then its rating as gold stars.
        constexpr float kStar = 12.0f;
        const auto tw = static_cast<float>(text_width(label, 2));
        const float w = tw + (stars > 0 ? 4.0f + kStar * static_cast<float>(stars) : 0.0f);
        const Box box{l.sx - w / 2 - 7, l.sy - 5, l.sx + w / 2 + 7, l.sy + 19};
        const bool clash = std::any_of(placed.begin(), placed.end(), [&](const Box& o) {
            return box.x0 < o.x1 + 4 && box.x1 > o.x0 - 4 && box.y0 < o.y1 + 2 && box.y1 > o.y0 - 2;
        });
        if (clash) continue;
        placed.push_back(box);
        ui::chip(box.x0, box.y0, box.x1, box.y1, 0.62f * alpha);
        glColor4f(1.0f, 1.0f, 0.95f, alpha);
        draw_text(l.sx - w / 2, l.sy, label, 2);
        glColor4f(0.98f, 0.8f, 0.25f, alpha);
        for (int k = 0; k < stars; ++k) {
            const float cx = l.sx - w / 2 + tw + 4.0f + kStar * (static_cast<float>(k) + 0.5f), cy = l.sy + 7.0f;
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(cx, cy);
            for (int j = 0; j <= 10; ++j) {
                const float r = j % 2 == 0 ? kStar * 0.48f : kStar * 0.2f;
                const float an = -1.5707963f + 3.1415927f * static_cast<float>(j) / 5.0f;
                glVertex2f(cx + r * std::cos(an), cy + r * std::sin(an));
            }
            glEnd();
        }
    }
}

void draw_station_names(const sim::World& w, const Camera& cam) {
    const sim::Railway& rw = w.railway();
    const auto& cargo = w.data().cargo;
    const std::optional<sim::CargoId> passengers = cargo.find("passengers"), mail = cargo.find("mail");
    struct Label {
        sim::StationId id;
        float sx, sy, dist;
    };
    std::vector<Label> labels;
    for (const sim::Station& s : rw.stations()) {
        const sim::MapPoint p = rw.track().node(s.node).pos;
        float sx = 0, sy = 0;
        if (!cam.to_screen(p, sx, sy)) continue;
        const float tx = static_cast<float>(p.x_mm) / cam.mm_per_tile, ty = static_cast<float>(p.y_mm) / cam.mm_per_tile;
        labels.push_back({s.id, sx, sy + 16, std::hypot(tx - cam.target_x, ty - cam.target_y)});
    }
    std::sort(labels.begin(), labels.end(), [](const Label& l, const Label& r) { return l.dist < r.dist; });
    struct Box {
        float x0, y0, x1, y1;
    };
    std::vector<Box> placed;
    // Closer in than the towns' names: stations crowd a wide view.
    const float fade_from = cam.distance * 1.2f, fade_to = cam.distance * 2.6f;
    const std::optional<sim::CompanyId> player = w.player_company();
    for (const Label& l : labels) {
        const float alpha = cam.three_d ? std::clamp((fade_to - l.dist) / std::max(1.0f, fade_to - fade_from), 0.0f, 1.0f) : 1.0f;
        if (alpha <= 0.02f) continue;
        const sim::Station& s = rw.station(l.id);
        // Loads waiting: passengers, mail, and everything else.
        std::int64_t pax = 0, post = 0, freight = 0;
        for (const sim::ExpressWaiting& e : s.express) {
            if (e.cargo == passengers) pax += e.milli;
            else if (e.cargo == mail) post += e.milli;
            else freight += e.milli;
        }
        for (const sim::WaitingCargo& c : s.waiting) freight += c.milli;
        struct Pip {
            std::int64_t loads;
            float r, g, b;
        };
        // Whole loads, as the station's panel counts them.
        const Pip pips[] = {{pax / sim::kMilli, 0.45f, 0.8f, 1.0f},
                            {post / sim::kMilli, 1.0f, 0.88f, 0.45f},
                            {freight / sim::kMilli, 0.85f, 0.6f, 0.35f}};
        float pips_w = 0;
        for (const Pip& p : pips) {
            if (p.loads <= 0) continue;
            pips_w += 14.0f + static_cast<float>(text_width(std::to_string(p.loads), 2)) + 8.0f;
        }
        const auto name_w = static_cast<float>(text_width(s.name, 2));
        const float w_box = std::max(name_w, pips_w) + 20.0f;
        const float h_box = pips_w > 0 ? 44.0f : 24.0f;
        const Box box{l.sx - w_box / 2, l.sy, l.sx + w_box / 2, l.sy + h_box};
        const bool clash = std::any_of(placed.begin(), placed.end(), [&](const Box& o) {
            return box.x0 < o.x1 + 3 && box.x1 > o.x0 - 3 && box.y0 < o.y1 + 2 && box.y1 > o.y0 - 2;
        });
        if (clash) continue;
        placed.push_back(box);
        ui::chip(box.x0, box.y0, box.x1, box.y1, 0.7f * alpha);
        float r, g, b;
        owner_rgb(s.owner, player, r, g, b);
        glColor4f(r, g, b, alpha);
        glBegin(GL_QUADS);
        glVertex2f(box.x0 + 3, box.y0 + 4);
        glVertex2f(box.x0 + 6, box.y0 + 4);
        glVertex2f(box.x0 + 6, box.y1 - 4);
        glVertex2f(box.x0 + 3, box.y1 - 4);
        glEnd();
        glColor4f(1.0f, 1.0f, 0.95f, alpha);
        draw_text(box.x0 + 12, box.y0 + 5, s.name, 2);
        float x = box.x0 + 12;
        for (const Pip& p : pips) {
            if (p.loads <= 0) continue;
            const float cy = box.y0 + 32;
            glColor4f(p.r, p.g, p.b, alpha);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(x + 5, cy);
            for (int j = 0; j <= 12; ++j) {
                const float an = 6.2831853f * static_cast<float>(j) / 12.0f;
                glVertex2f(x + 5 + 5 * std::cos(an), cy + 5 * std::sin(an));
            }
            glEnd();
            const std::string n = std::to_string(p.loads);
            glColor4f(0.92f, 0.92f, 0.88f, alpha);
            draw_text(x + 14, box.y0 + 25, n, 2);
            x += 14.0f + static_cast<float>(text_width(n, 2)) + 8.0f;
        }
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
        if (company && company->has_access(static_cast<sim::TerritoryId>(t))) label += " (yours)";
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
    ui::panel(x0 - 7, y0 - 7, x0 + size + 7, y0 + size + 7);
    glBegin(GL_QUADS);
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

void clear_smoke() {
    g_smoke.clear();
    g_lights.clear();
}

void draw_lights(const Camera& cam) {
    const float night = shading::night();
    if (!cam.three_d || night <= 0.05f || g_lights.empty()) {
        g_lights.clear();
        return;
    }
    // Glows face the camera: its right and up, in map axes.
    const auto& vi = cam.view_inverse();
    const float rx = vi[0], ry = vi[1], rz = vi[2], ux = vi[4], uy = vi[5], uz = vi[6];
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // light adds
    glDepthMask(GL_FALSE);
    constexpr int kRim = 12;
    for (const LightSource& l : g_lights) {
        const float a = night;
        // The halo, fading to nothing at its rim, and a bright core.
        for (const float scale : {1.0f, 0.35f}) {
            const float rad = l.size * scale, core = scale < 1.0f ? 0.9f : 0.45f;
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(l.r, l.g, l.b, a * core);
            glVertex3f(l.x, l.y, l.z);
            glColor4f(l.r, l.g, l.b, 0.0f);
            for (int i = 0; i <= kRim; ++i) {
                const float an = 6.2831853f * static_cast<float>(i) / kRim, c = std::cos(an) * rad, sn = std::sin(an) * rad;
                glVertex3f(l.x + rx * c + ux * sn, l.y + ry * c + uy * sn, l.z + rz * c + uz * sn);
            }
            glEnd();
        }
        // A headlamp lights the track ahead: a soft oval on the ground.
        if (l.ahead_x != 0 || l.ahead_y != 0) {
            const float dist = l.size * 3.5f, cx = l.x + l.ahead_x * dist, cy = l.y + l.ahead_y * dist;
            const float px = -l.ahead_y, py = l.ahead_x;
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(l.r, l.g, l.b, a * 0.35f);
            glVertex3f(cx, cy, cam.ground_z(cx, cy) + 0.05f);
            glColor4f(l.r, l.g, l.b, 0.0f);
            for (int i = 0; i <= kRim; ++i) {
                const float an = 6.2831853f * static_cast<float>(i) / kRim;
                const float ex = l.ahead_x * std::cos(an) * dist * 1.1f + px * std::sin(an) * l.size * 1.4f;
                const float ey = l.ahead_y * std::cos(an) * dist * 1.1f + py * std::sin(an) * l.size * 1.4f;
                glVertex3f(cx + ex, cy + ey, cam.ground_z(cx + ex, cy + ey) + 0.05f);
            }
            glEnd();
        }
    }
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_lights.clear();
}

void set_animation_clock(float seconds) { g_animation_seconds = seconds; }

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
        const float grey = (src.steam ? 0.95f : 0.5f) * (1.0f - 0.75f * shading::night()); // darker after dark
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

void draw_weather(const Camera& cam, float amount, bool snow, float seconds) {
    if (amount <= 0.01f) return;
    const auto w = static_cast<float>(cam.width_px), h = static_cast<float>(cam.height_px);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (snow) {
        // Flakes drifting down, each swaying on its own.
        const int n = static_cast<int>(420.0f * amount);
        glBegin(GL_QUADS);
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::uint32_t>(i);
            const float speed = 0.05f + 0.07f * unit(hash(k, 1, 77));
            const float y = std::fmod(unit(hash(k, 2, 77)) + seconds * speed, 1.0f) * (h + 20) - 10;
            const float sway = std::sin(seconds * (0.8f + unit(hash(k, 3, 77))) + static_cast<float>(i)) * 10.0f;
            const float x = std::fmod(unit(hash(k, 4, 77)) * w + seconds * 6.0f + sway + w, w);
            const float r = 1.0f + 1.6f * unit(hash(k, 5, 77));
            glColor4f(1.0f, 1.0f, 1.0f, 0.55f + 0.35f * unit(hash(k, 6, 77)));
            glVertex2f(x - r, y - r);
            glVertex2f(x + r, y - r);
            glVertex2f(x + r, y + r);
            glVertex2f(x - r, y + r);
        }
        glEnd();
    } else {
        // Streaks slanting with the wind.
        const int n = static_cast<int>(650.0f * amount);
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::uint32_t>(i);
            const float speed = 1.4f + 0.8f * unit(hash(k, 1, 78));
            const float y = std::fmod(unit(hash(k, 2, 78)) + seconds * speed, 1.0f) * (h + 60) - 30;
            const float x = std::fmod(unit(hash(k, 4, 78)) * w + y * 0.25f + w, w);
            const float len = 12.0f + 10.0f * unit(hash(k, 5, 78));
            glColor4f(0.78f, 0.82f, 0.9f, 0.0f);
            glVertex2f(x - len * 0.25f, y - len);
            glColor4f(0.78f, 0.82f, 0.9f, 0.4f);
            glVertex2f(x, y);
        }
        glEnd();
    }
}

} // namespace railmaster::client
