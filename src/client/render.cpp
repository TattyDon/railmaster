#include "render.hpp"

#include "font.hpp"
#include "tools.hpp"

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

void draw_railway(const sim::Railway& rw, const Camera& cam, std::optional<sim::CompanyId> player) {
    const sim::TrackNetwork& net = rw.track();
    for (const sim::TrackEdge& e : net.edges()) {
        glLineWidth(e.double_track ? 4.0f : 2.0f);
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
        vertex(net.node(e.a).pos, cam);
        vertex(net.node(e.b).pos, cam);
        glEnd();
    }
    // Overhead wires: a thin copper line along electrified track.
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor3f(0.95f, 0.6f, 0.2f);
    for (const sim::TrackEdge& e : net.edges()) {
        if (!e.electrified) continue;
        vertex(net.node(e.a).pos, cam);
        vertex(net.node(e.b).pos, cam);
    }
    glEnd();

    // Markers stay the same size on screen at any zoom.
    const float px = 1.0f / cam.zoom;
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

void draw_sites(const sim::Economy& eco, const sim::IndustryRegistry& industries, const Camera& cam,
                std::optional<sim::CargoId> cargo) {
    const float px = 1.0f / cam.zoom;
    // Drawing is in map cells; a site sits at the centre of its economy node.
    const auto k = static_cast<float>(eco.cells_per_node());
    const auto cell_square = [&](std::int32_t cx, std::int32_t cy, float half) {
        const float x = (static_cast<float>(cx) + 0.5f) * k, y = (static_cast<float>(cy) + 0.5f) * k;
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
                cell_square(s.cx, s.cy, std::max(0.45f * k, 6 * px));
            }
        }
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

void draw_price_overlay(const sim::Economy& eco, const sim::CargoType& cargo) {
    const auto base = static_cast<float>(std::max<std::int64_t>(1, cargo.base_price.whole_dollars()));
    const auto k = static_cast<float>(eco.cells_per_node()); // map cells per node
    glBegin(GL_QUADS);
    for (std::int32_t y = 0; y < eco.height(); ++y) {
        for (std::int32_t x = 0; x < eco.width(); ++x) {
            // 0 at half the base price (red), 1 at one and a half times (green).
            const float t = std::clamp(static_cast<float>(eco.price(cargo.id, x, y)) / base - 0.5f, 0.0f, 1.0f);
            glColor4f(1.0f - t, t, 0.1f, 0.55f);
            const auto fx = static_cast<float>(x) * k, fy = static_cast<float>(y) * k;
            glVertex2f(fx, fy);
            glVertex2f(fx + k, fy);
            glVertex2f(fx + k, fy + k);
            glVertex2f(fx, fy + k);
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
            glVertex2f(cx - half, cy - half);
            glVertex2f(cx + half, cy - half);
            glVertex2f(cx + half, cy + half);
            glVertex2f(cx - half, cy + half);
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
        cam.to_screen({c.x_mm, c.y_mm - static_cast<std::int64_t>(3.0f * cell_mm)}, sx, sy);
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

namespace railmaster::client {

void draw_territory_borders(const sim::TerritoryMap& map) {
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
                glVertex2f(x0, y0);
                glVertex2f(x1, y1);
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
        cam.to_screen({(sx[t] * cell_mm) / n[t] + cell_mm / 2, (sy[t] * cell_mm) / n[t] + cell_mm / 2}, px, py);
        const auto w = static_cast<float>(text_width(label, 2));
        glColor4f(0.25f, 0.15f, 0.4f, 0.9f);
        draw_text(px - w / 2, py, label, 2);
    }
}

} // namespace railmaster::client
