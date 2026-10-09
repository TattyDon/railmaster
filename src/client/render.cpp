#include "render.hpp"

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
        if (t.state == sim::TrainState::BrokenDown) glColor3f(1.0f, 0.55f, 0.0f);
        else if (t.state == sim::TrainState::Servicing) glColor3f(0.2f, 0.4f, 1.0f);
        else if (t.yielding) glColor3f(0.9f, 0.15f, 0.1f);
        else glColor3f(0.1f, 0.1f, 0.1f);
        square(rw.train_position(t.id), cam, 4 * px);
    }
    glEnd();
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
