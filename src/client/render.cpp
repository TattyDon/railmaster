#include "render.hpp"

#include "font.hpp"
#include "tools.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace railmaster::client {

namespace {

// Overlays and track sit this far above the ground (tiles), so they show
// over it without flickering.
constexpr float kLift = 0.04f;

float tiles(std::int64_t mm, const Camera& cam) { return static_cast<float>(mm) / cam.mm_per_tile; }

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
}
void end_lit() {
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
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

// A house: walls and a pitched roof.
void house(float x, float y, float z, float half, const Camera& cam) {
    static const float wall[] = {0.88f, 0.82f, 0.68f};
    static const float roof[] = {0.62f, 0.22f, 0.16f};
    (void)cam;
    box(x, y, z, 1, 0, half, half, half * 1.2f, wall, wall);
    glEnd();
    glBegin(GL_TRIANGLES);
    glColor3fv(roof);
    const float top = z + half * 1.2f, ridge = top + half * 0.9f;
    // Two slopes running east-west, and the gable ends.
    glNormal3f(0, -1, 1);
    glVertex3f(x - half, y - half, top);
    glVertex3f(x + half, y - half, top);
    glVertex3f(x + half, y, ridge);
    glVertex3f(x - half, y - half, top);
    glVertex3f(x + half, y, ridge);
    glVertex3f(x - half, y, ridge);
    glNormal3f(0, 1, 1);
    glVertex3f(x - half, y + half, top);
    glVertex3f(x + half, y, ridge);
    glVertex3f(x + half, y + half, top);
    glVertex3f(x - half, y + half, top);
    glVertex3f(x - half, y, ridge);
    glVertex3f(x + half, y, ridge);
    glColor3f(0.88f, 0.82f, 0.68f);
    glNormal3f(-1, 0, 0);
    glVertex3f(x - half, y - half, top);
    glVertex3f(x - half, y, ridge);
    glVertex3f(x - half, y + half, top);
    glNormal3f(1, 0, 0);
    glVertex3f(x + half, y - half, top);
    glVertex3f(x + half, y + half, top);
    glVertex3f(x + half, y, ridge);
    glEnd();
    glBegin(GL_QUADS);
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
        if (n == 0) glColor3f(0.45f, 0.42f, 0.3f);
        else glColor3f(sum[0] / static_cast<float>(n), sum[1] / static_cast<float>(n), sum[2] / static_cast<float>(n));
    };
    glBegin(GL_QUADS);
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const sim::GroundType g = t.ground(tx, ty);
            const auto fx = static_cast<float>(tx), fy = static_cast<float>(ty);
            if (g == sim::GroundType::Water) {
                // Water lies flat, at its lowest corner.
                const float wz = std::min({z(tx, ty), z(tx + 1, ty), z(tx, ty + 1), z(tx + 1, ty + 1)});
                const float* c = &tile_rgb[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(w) + static_cast<std::size_t>(tx)) * 3];
                glColor3f(c[0], c[1], c[2]);
                glNormal3f(0, 0, 1);
                glVertex3f(fx, fy, wz);
                glVertex3f(fx + 1, fy, wz);
                glVertex3f(fx + 1, fy + 1, wz);
                glVertex3f(fx, fy + 1, wz);
                continue;
            }
            corner_colour(tx, ty);
            normal(tx, ty);
            glVertex3f(fx, fy, z(tx, ty));
            corner_colour(tx + 1, ty);
            normal(tx + 1, ty);
            glVertex3f(fx + 1, fy, z(tx + 1, ty));
            corner_colour(tx + 1, ty + 1);
            normal(tx + 1, ty + 1);
            glVertex3f(fx + 1, fy + 1, z(tx + 1, ty + 1));
            corner_colour(tx, ty + 1);
            normal(tx, ty + 1);
            glVertex3f(fx, fy + 1, z(tx, ty + 1));
        }
    }
    glEnd();
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
    // Trees: thick in forest, a few in the grass.
    glBegin(GL_TRIANGLES);
    for (int ty = 0; ty < hgt; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const sim::GroundType g = t.ground(tx, ty);
            int n = 0;
            if (g == sim::GroundType::Forest) n = 4;
            else if (g == sim::GroundType::Grass && unit(hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), 7)) < 0.06f) n = 1;
            for (int i = 0; i < n; ++i) {
                const std::uint32_t hh = hash(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), static_cast<std::uint32_t>(i + 11));
                const float x = static_cast<float>(tx) + 0.15f + 0.7f * unit(hh);
                const float y = static_cast<float>(ty) + 0.15f + 0.7f * unit(hh >> 16);
                const float base = cam.ground_z(x, y);
                const float hgt_t = 0.30f + 0.18f * unit(hh >> 8), half = 0.11f;
                const float shade_k = 0.85f + 0.3f * unit(hh >> 4);
                glColor3f(0.13f * shade_k, 0.33f * shade_k, 0.12f * shade_k);
                const float cx[4] = {x - half, x + half, x + half, x - half};
                const float cy[4] = {y - half, y - half, y + half, y + half};
                for (int k = 0; k < 4; ++k) {
                    const int j = (k + 1) % 4;
                    const float ex = cx[j] - cx[k], ey = cy[j] - cy[k];
                    glNormal3f(ey, -ex, half * 0.8f);
                    glVertex3f(cx[k], cy[k], base);
                    glVertex3f(cx[j], cy[j], base);
                    glVertex3f(x, y, base + hgt_t);
                }
            }
        }
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

// One piece of rolling stock: its kind decides the shape and colours.
enum class Stock { Steam, Diesel, Electric, Coach, Mail, Diner, Boxcar, Hopper, Caboose };

// Draw it with its front at `front` tiles along (dx, dy) from (x, y); returns its length.
float rolling_stock(Stock kind, float x, float y, float z, float dx, float dy, float s, const float* livery) {
    const float wide = 0.05f * s;
    const float frame[3] = {0.12f, 0.11f, 0.11f};
    const auto at = [&](float along, float& ox, float& oy) {
        ox = x - dx * along;
        oy = y - dy * along;
    };
    float cx, cy;
    switch (kind) {
    case Stock::Steam: {
        const float len = 0.30f * s;
        const float black[3] = {0.14f, 0.14f, 0.15f};
        at(len / 2, cx, cy);
        box(cx, cy, z, dx, dy, len / 2, wide * 0.9f, 0.03f * s, frame, frame); // running gear
        at(0.10f * s, cx, cy);
        prism(cx, cy, z + 0.065f * s, dx, dy, 0.09f * s, 0.036f * s, black); // boiler
        at(0.03f * s, cx, cy);
        box(cx, cy, z + 0.03f * s, dx, dy, 0.012f * s, 0.012f * s, 0.085f * s, black, black); // chimney
        at(0.215f * s, cx, cy);
        box(cx, cy, z + 0.03f * s, dx, dy, 0.035f * s, wide, 0.085f * s, livery, black); // cab
        at(0.275f * s, cx, cy);
        box(cx, cy, z + 0.03f * s, dx, dy, 0.025f * s, wide * 0.9f, 0.05f * s, black, black); // tender
        return len;
    }
    case Stock::Diesel:
    case Stock::Electric: {
        const float len = 0.26f * s;
        const float roof[3] = {livery[0] * 0.6f, livery[1] * 0.6f, livery[2] * 0.6f};
        at(len / 2, cx, cy);
        box(cx, cy, z, dx, dy, len / 2, wide * 0.9f, 0.03f * s, frame, frame);
        box(cx, cy, z + 0.03f * s, dx, dy, len / 2 * 0.95f, wide, 0.065f * s, livery, roof);
        // A dark band of cab windows at the front.
        const float glass[3] = {0.15f, 0.2f, 0.25f};
        at(0.025f * s, cx, cy);
        box(cx, cy, z + 0.07f * s, dx, dy, 0.018f * s, wide * 1.02f, 0.022f * s, glass, roof);
        if (kind == Stock::Electric) {
            // A pantograph up to the wires.
            at(0.08f * s, cx, cy);
            const float bar[3] = {0.3f, 0.3f, 0.3f};
            box(cx, cy, z + 0.095f * s, dx, dy, 0.004f * s, wide * 0.6f, 0.05f * s, bar, bar);
        }
        return len;
    }
    case Stock::Coach:
    case Stock::Mail:
    case Stock::Diner: {
        const float len = 0.22f * s;
        const float body[3] = {kind == Stock::Mail ? 0.45f : 0.38f, kind == Stock::Mail ? 0.12f : 0.18f,
                               kind == Stock::Mail ? 0.1f : 0.12f};
        const float roof[3] = {0.32f, 0.32f, 0.34f};
        const float band[3] = {kind == Stock::Diner ? 0.85f : 0.8f, kind == Stock::Diner ? 0.7f : 0.72f,
                               kind == Stock::Diner ? 0.3f : 0.55f};
        at(len / 2, cx, cy);
        box(cx, cy, z, dx, dy, len / 2, wide * 0.85f, 0.025f * s, frame, frame);
        box(cx, cy, z + 0.025f * s, dx, dy, len / 2 * 0.97f, wide, 0.065f * s, body, roof);
        if (kind != Stock::Mail) {
            // A line of windows.
            box(cx, cy, z + 0.06f * s, dx, dy, len / 2 * 0.9f, wide * 1.02f, 0.016f * s, band, roof);
        }
        return len;
    }
    case Stock::Boxcar:
    case Stock::Hopper: {
        const float len = 0.18f * s;
        const float body[3] = {kind == Stock::Hopper ? 0.25f : 0.5f, kind == Stock::Hopper ? 0.24f : 0.32f,
                               kind == Stock::Hopper ? 0.23f : 0.2f};
        const float top[3] = {kind == Stock::Hopper ? 0.08f : 0.42f, kind == Stock::Hopper ? 0.08f : 0.27f,
                              kind == Stock::Hopper ? 0.08f : 0.17f};
        at(len / 2, cx, cy);
        box(cx, cy, z, dx, dy, len / 2, wide * 0.85f, 0.025f * s, frame, frame);
        box(cx, cy, z + 0.025f * s, dx, dy, len / 2 * 0.97f, wide, (kind == Stock::Hopper ? 0.045f : 0.06f) * s, body, top);
        return len;
    }
    case Stock::Caboose: {
        const float len = 0.12f * s;
        const float red[3] = {0.7f, 0.15f, 0.1f}, roof[3] = {0.3f, 0.1f, 0.08f};
        at(len / 2, cx, cy);
        box(cx, cy, z, dx, dy, len / 2, wide * 0.85f, 0.025f * s, frame, frame);
        box(cx, cy, z + 0.025f * s, dx, dy, len / 2 * 0.95f, wide, 0.055f * s, red, roof);
        box(cx, cy, z + 0.08f * s, dx, dy, len / 6, wide * 0.6f, 0.025f * s, red, roof); // cupola
        return len;
    }
    }
    return 0.0f;
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
        const float s = std::max(1.5f, 8.0f / (cam.zoom * 0.2f));
        begin_lit();
        glBegin(GL_QUADS);
        for (const sim::Station& st : rw.stations()) {
            float c[3];
            owner_rgb(st.owner, player, c[0], c[1], c[2]);
            const float roof[3] = {c[0] * 0.7f, c[1] * 0.7f, c[2] * 0.7f};
            const sim::TrackNode& n = net.node(st.node);
            const float x = tiles(n.pos.x_mm, cam), y = tiles(n.pos.y_mm, cam);
            const float half = 0.18f * s * (st.size == sim::StationSize::Large ? 1.4f : st.size == sim::StationSize::Small ? 0.8f : 1.0f);
            box(x, y, cam.z_of(n.z_mm), 1, 0, half, half * 0.6f, 0.12f * s, c, roof);
        }
        for (const sim::StationBuilding& b : rw.station_buildings()) {
            float c[3];
            switch (b.type) {
            case sim::StationBuildingType::PostOffice: c[0] = 0.3f; c[1] = 0.5f; c[2] = 0.95f; break;
            case sim::StationBuildingType::Hotel: c[0] = 0.95f; c[1] = 0.95f; c[2] = 0.95f; break;
            case sim::StationBuildingType::Restaurant: c[0] = 0.3f; c[1] = 0.85f; c[2] = 0.35f; break;
            case sim::StationBuildingType::Tavern: c[0] = 0.9f; c[1] = 0.3f; c[2] = 0.25f; break;
            }
            const float x = tiles(b.pos.x_mm, cam), y = tiles(b.pos.y_mm, cam);
            box(x, y, cam.ground_z(x, y), 1, 0, 0.09f * s, 0.09f * s, 0.16f * s, c, c);
        }
        for (const sim::ServiceBuilding& b : rw.service_buildings()) {
            const float tower[3] = {0.3f, 0.5f, 0.85f}, shed[3] = {0.85f, 0.65f, 0.2f};
            const sim::TrackNode& n = net.node(b.node);
            const float x = tiles(n.pos.x_mm, cam) + 0.25f * s, y = tiles(n.pos.y_mm, cam) + 0.25f * s;
            if (b.type == sim::ServiceType::ServiceTower) box(x, y, cam.ground_z(x, y), 1, 0, 0.05f * s, 0.05f * s, 0.3f * s, tower, tower);
            else box(x, y, cam.ground_z(x, y), 1, 0, 0.14f * s, 0.09f * s, 0.1f * s, shed, shed);
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
            float back = rolling_stock(engine, x, y, z, p.dx, p.dy, s, c) + gap;
            const auto behind = [&](Stock kind) {
                back += rolling_stock(kind, x - p.dx * back, y - p.dy * back, z, p.dx, p.dy, s, c) + gap;
            };
            for (const sim::Car& car : t.cars) {
                if (!car.cargo) continue; // empty cars are left behind
                const sim::CargoType& cargo = w.data().cargo.get(*car.cargo);
                if (cargo.cargo_class == sim::CargoClass::Express) behind(cargo.key == "mail" ? Stock::Mail : Stock::Coach);
                else behind(cargo.key == "coal" || cargo.key == "iron" || cargo.key == "bauxite" ? Stock::Hopper : Stock::Boxcar);
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

void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Camera& cam,
                std::optional<sim::CargoId> cargo) {
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
        begin_lit();
        glBegin(GL_QUADS);
        for (const sim::Site& s : eco.sites()) {
            if (s.closed) continue;
            const sim::IndustryType& t = industries.get(s.type);
            const float x0 = (static_cast<float>(s.cx) + 0.5f) * k, y0 = (static_cast<float>(s.cy) + 0.5f) * k;
            if (t.kind == sim::IndustryKind::House) {
                // A few houses round the spot, more as the level rises.
                const int n = std::clamp(1 + s.level / 2, 1, 4);
                for (int i = 0; i < n; ++i) {
                    const std::uint32_t h = hash(static_cast<std::uint32_t>(s.cx), static_cast<std::uint32_t>(s.cy),
                                                 static_cast<std::uint32_t>(i + 101));
                    const float x = x0 + (unit(h) - 0.5f) * 0.8f * k, y = y0 + (unit(h >> 16) - 0.5f) * 0.8f * k;
                    house(x, y, cam.ground_z(x, y), 0.13f + 0.04f * unit(h >> 8), cam);
                }
                continue;
            }
            float c[3];
            switch (t.kind) {
            case sim::IndustryKind::Raw: c[0] = 0.55f; c[1] = 0.35f; c[2] = 0.15f; break;
            case sim::IndustryKind::Processor: c[0] = 0.55f; c[1] = 0.28f; c[2] = 0.62f; break;
            case sim::IndustryKind::Sink: c[0] = 0.5f; c[1] = 0.5f; c[2] = 0.55f; break;
            case sim::IndustryKind::Port: c[0] = 0.95f; c[1] = 0.6f; c[2] = 0.15f; break;
            case sim::IndustryKind::Warehouse: c[0] = 0.75f; c[1] = 0.55f; c[2] = 0.3f; break;
            case sim::IndustryKind::House: c[0] = c[1] = c[2] = 0.8f; break;
            }
            const float roof[3] = {c[0] * 0.65f, c[1] * 0.65f, c[2] * 0.65f};
            const float z = cam.ground_z(x0, y0);
            box(x0, y0, z, 1, 0, 0.32f * k, 0.22f * k, 0.32f, c, roof);
            if (t.kind == sim::IndustryKind::Processor || t.kind == sim::IndustryKind::Sink) {
                // A chimney.
                const float stack[3] = {0.45f, 0.4f, 0.38f};
                box(x0 + 0.2f * k, y0 - 0.1f * k, z, 1, 0, 0.06f, 0.06f, 0.8f, stack, stack);
            }
        }
        glEnd();
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

} // namespace railmaster::client
