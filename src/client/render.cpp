#include "render.hpp"

#include "font.hpp"

#include <SDL_opengl.h>

#include <algorithm>

namespace railmaster::client {

namespace {

float tiles(std::int64_t mm, const Camera& cam) { return static_cast<float>(mm) / cam.mm_per_tile; }

void vertex(sim::MapPoint p, const Camera& cam) { glVertex2f(tiles(p.x_mm, cam), tiles(p.y_mm, cam)); }

void square(sim::MapPoint p, const Camera& cam, float half_tiles) {
    const float x = tiles(p.x_mm, cam), y = tiles(p.y_mm, cam);
    glVertex2f(x - half_tiles, y - half_tiles);
    glVertex2f(x + half_tiles, y - half_tiles);
    glVertex2f(x + half_tiles, y + half_tiles);
    glVertex2f(x - half_tiles, y + half_tiles);
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

} // namespace

sim::MapPoint Camera::to_map(float sx, float sy) const {
    return {static_cast<std::int64_t>((sx / zoom + pan_x) * mm_per_tile),
            static_cast<std::int64_t>((sy / zoom + pan_y) * mm_per_tile)};
}

void Camera::to_screen(sim::MapPoint p, float& sx, float& sy) const {
    sx = (static_cast<float>(p.x_mm) / mm_per_tile - pan_x) * zoom;
    sy = (static_cast<float>(p.y_mm) / mm_per_tile - pan_y) * zoom;
}

void Camera::zoom_at(float sx, float sy, float factor) {
    const float tx = sx / zoom + pan_x, ty = sy / zoom + pan_y;
    zoom = std::clamp(zoom * factor, 1.0f, 200.0f);
    pan_x = tx - sx / zoom;
    pan_y = ty - sy / zoom;
}

void Camera::apply() const {
    glViewport(0, 0, width_px, height_px);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width_px, height_px, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glScalef(zoom, zoom, 1.0f);
    glTranslatef(-pan_x, -pan_y, 0.0f);
}

void Camera::apply_screen() const {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void draw_terrain(const sim::Terrain& t) {
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
}

void draw_railway(const sim::Railway& rw, const Camera& cam) {
    const sim::TrackNetwork& net = rw.track();
    for (const sim::TrackEdge& e : net.edges()) {
        glLineWidth(e.double_track ? 4.0f : 2.0f);
        glBegin(GL_LINES);
        track_colour(e.kind, 1.0f);
        vertex(net.node(e.a).pos, cam);
        vertex(net.node(e.b).pos, cam);
        glEnd();
    }

    // Markers stay the same size on screen at any zoom.
    const float px = 1.0f / cam.zoom;
    glBegin(GL_QUADS);
    glColor3f(0.95f, 0.95f, 0.9f);
    for (const sim::Station& s : rw.stations()) square(net.node(s.node).pos, cam, 6 * px);
    for (const sim::ServiceBuilding& b : rw.service_buildings()) {
        if (b.type == sim::ServiceType::ServiceTower) glColor3f(0.3f, 0.6f, 0.95f);
        else glColor3f(0.95f, 0.75f, 0.2f);
        square(net.node(b.node).pos, cam, 3 * px);
    }
    for (const sim::Train& t : rw.trains()) {
        if (t.state == sim::TrainState::Crashed) continue;
        if (t.state == sim::TrainState::BrokenDown) glColor3f(1.0f, 0.55f, 0.0f);
        else if (t.state == sim::TrainState::Servicing) glColor3f(0.2f, 0.4f, 1.0f);
        else if (t.yielding) glColor3f(0.9f, 0.15f, 0.1f);
        else glColor3f(0.1f, 0.1f, 0.1f);
        square(rw.train_position(t.id), cam, 4 * px);
    }
    glEnd();
}

void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Camera& cam,
                std::optional<sim::CargoId> cargo) {
    const float px = 1.0f / cam.zoom;
    const auto cell_square = [&](std::int32_t cx, std::int32_t cy, float half) {
        const float x = static_cast<float>(cx) + 0.5f, y = static_cast<float>(cy) + 0.5f;
        glVertex2f(x - half, y - half);
        glVertex2f(x + half, y - half);
        glVertex2f(x + half, y + half);
        glVertex2f(x - half, y + half);
    };
    glBegin(GL_QUADS);
    for (const sim::Site& s : eco.sites()) {
        const sim::IndustryType& t = industries.get(s.type);
        if (cargo) {
            const bool makes = std::find(t.outputs.begin(), t.outputs.end(), *cargo) != t.outputs.end();
            const bool uses = std::any_of(t.inputs.begin(), t.inputs.end(),
                                          [&](const sim::IndustryInput& i) { return i.cargo == *cargo; });
            if (makes || uses) {
                if (makes) glColor3f(0.9f, 0.1f, 0.1f);
                else glColor3f(0.1f, 0.85f, 0.2f);
                cell_square(s.cx, s.cy, std::max(0.45f, 6 * px));
            }
        }
        switch (t.kind) {
        case sim::IndustryKind::Raw: glColor3f(0.55f, 0.35f, 0.15f); break;
        case sim::IndustryKind::Processor: glColor3f(0.55f, 0.25f, 0.65f); break;
        case sim::IndustryKind::Sink: glColor3f(0.45f, 0.45f, 0.5f); break;
        case sim::IndustryKind::House: glColor3f(0.92f, 0.85f, 0.7f); break;
        }
        const float half = t.kind == sim::IndustryKind::House ? 0.2f + 0.02f * static_cast<float>(std::min(s.level, 10))
                                                                : 0.3f;
        cell_square(s.cx, s.cy, std::max(half, 2 * px));
    }
    glEnd();
}

void draw_price_overlay(const sim::Economy& eco, const sim::CargoType& cargo) {
    const auto base = static_cast<float>(std::max<std::int64_t>(1, cargo.base_price.whole_dollars()));
    glBegin(GL_QUADS);
    for (std::int32_t y = 0; y < eco.height(); ++y) {
        for (std::int32_t x = 0; x < eco.width(); ++x) {
            // 0 at half the base price (red), 1 at one and a half times (green).
            const float t = std::clamp(static_cast<float>(eco.price(cargo.id, x, y)) / base - 0.5f, 0.0f, 1.0f);
            glColor4f(1.0f - t, t, 0.1f, 0.55f);
            const auto fx = static_cast<float>(x), fy = static_cast<float>(y);
            glVertex2f(fx, fy);
            glVertex2f(fx + 1, fy);
            glVertex2f(fx + 1, fy + 1);
            glVertex2f(fx, fy + 1);
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
            const float half = std::min(0.4f, 0.08f + 0.04f * static_cast<float>(s) / sim::kMilli);
            const float cx = static_cast<float>(x) + 0.5f, cy = static_cast<float>(y) + 0.5f;
            glVertex2f(cx - half, cy - half);
            glVertex2f(cx + half, cy - half);
            glVertex2f(cx + half, cy + half);
            glVertex2f(cx - half, cy + half);
        }
    }
    glEnd();
}

void draw_town_names(const sim::Economy& eco, const Camera& cam) {
    const float cell_mm = cam.mm_per_tile;
    for (const sim::Town& t : eco.towns()) {
        float sx = 0, sy = 0;
        cam.to_screen({static_cast<std::int64_t>((static_cast<float>(t.cx) + 0.5f) * cell_mm),
                       static_cast<std::int64_t>((static_cast<float>(t.cy) - 2.5f) * cell_mm)},
                      sx, sy);
        const auto w = static_cast<float>(text_width(t.name, 2));
        glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
        glBegin(GL_QUADS);
        glVertex2f(sx - w / 2 - 3, sy - 3);
        glVertex2f(sx + w / 2 + 3, sy - 3);
        glVertex2f(sx + w / 2 + 3, sy + 17);
        glVertex2f(sx - w / 2 - 3, sy + 17);
        glEnd();
        glColor3f(1.0f, 1.0f, 0.95f);
        draw_text(sx - w / 2, sy, t.name, 2);
    }
}

void draw_plan(sim::MapPoint start, const sim::TrackPlan& plan, const Camera& cam) {
    glLineWidth(plan.double_track ? 5.0f : 3.0f);
    glBegin(GL_LINES);
    sim::MapPoint prev = start;
    for (std::size_t i = 0; i < plan.points.size(); ++i) {
        track_colour(plan.pieces[i].kind, 0.85f);
        vertex(prev, cam);
        vertex(plan.points[i], cam);
        prev = plan.points[i];
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

} // namespace railmaster::client
