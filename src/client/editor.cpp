#include "editor.hpp"

#include "font.hpp"
#include "render.hpp"
#include "ui.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace railmaster::client {

namespace {

constexpr int kScale = 2;
constexpr double kPi = 3.14159265358979;
constexpr float kBarH = 22.0f, kToolbarY = 26.0f, kButtonH = 22.0f, kBottomH = 44.0f;
constexpr const char* kBrushNames[] = {"1 Raise", "2 Lower", "3 Smooth", "4 Flatten", "5 Water", "6 Land"};

} // namespace

void Editor::begin(std::string name, std::int32_t max_height_m) {
    open_ = true;
    name_ = std::move(name);
    max_height_m_ = max_height_m;
    dirty_ = false;
    leave_armed_ = false;
    held_ = false;
}

void Editor::saved(const std::string& path) {
    dirty_ = false;
    message_ = "Saved as " + path + ": it is on the title screen's list of landscapes";
    message_until_ = SDL_GetTicks() + 6000;
}

bool Editor::key(SDL_Keycode key) {
    if (key != SDLK_ESCAPE) leave_armed_ = false;
    switch (key) {
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6: brush_ = static_cast<Brush>(key - SDLK_1); return true;
    case SDLK_LEFTBRACKET: radius_ = std::max(1, radius_ - 1); return true;
    case SDLK_RIGHTBRACKET: radius_ = std::min(40, radius_ + 1); return true;
    case SDLK_s: request_ = Request{Request::Kind::Save}; return true;
    case SDLK_p: request_ = Request{Request::Kind::Play}; return true;
    case SDLK_ESCAPE:
        if (dirty_ && !leave_armed_) {
            leave_armed_ = true;
            message_ = "Not saved: Esc again to leave without saving, S to save";
            message_until_ = SDL_GetTicks() + 4000;
            return true;
        }
        request_ = Request{Request::Kind::Leave};
        return true;
    default: return false;
    }
}

bool Editor::mouse_down(float sx, float sy, int button) {
    if (button != SDL_BUTTON_LEFT) return false;
    // The brush buttons along the top.
    if (sy >= kToolbarY && sy < kToolbarY + kButtonH) {
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            if (sx >= buttons_[i].first && sx < buttons_[i].second) brush_ = static_cast<Brush>(i);
        }
        return true;
    }
    mouse_move(sx, sy);
    held_ = true;
    pending_ = 0.05; // a click shapes a little at once
    const sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    const auto cx = std::clamp(static_cast<std::int32_t>(std::lround(static_cast<double>(hover_.x_mm) / tile_mm)), 0, t.width());
    const auto cy = std::clamp(static_cast<std::int32_t>(std::lround(static_cast<double>(hover_.y_mm) / tile_mm)), 0, t.height());
    flatten_to_ = t.corner_height(cx, cy);
    return true;
}

void Editor::mouse_up(int button) {
    if (button == SDL_BUTTON_LEFT) held_ = false;
}

void Editor::mouse_move(float sx, float sy) {
    hover_sx_ = sx;
    hover_sy_ = sy;
    hover_ = cam_.to_map(sx, sy);
}

void Editor::update(double dt) {
    if (!held_) return;
    pending_ += dt;
    // A few times a second, so the land is not rebuilt every frame.
    if (pending_ < 0.06) return;
    apply(static_cast<float>(std::min(pending_, 0.25)));
    pending_ = 0;
}

void Editor::apply(float seconds) {
    sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    const double px = static_cast<double>(hover_.x_mm) / tile_mm, py = static_cast<double>(hover_.y_mm) / tile_mm;
    const int r = radius_;
    const int x0 = std::max(0, static_cast<int>(px) - r - 1), x1 = std::min(t.width(), static_cast<int>(px) + r + 1);
    const int ya = std::max(0, static_cast<int>(py) - r - 1), yb = std::min(t.height(), static_cast<int>(py) + r + 1);
    // How hard the brush works at a point: full in the middle, nothing at the rim.
    const auto weight = [&](double x, double y) {
        const double d = std::hypot(x - px, y - py) / r;
        if (d >= 1) return 0.0;
        const double k = 1 - d * d;
        return k * k;
    };
    if (brush_ == Brush::Water || brush_ == Brush::Land) {
        for (int y = ya; y < yb; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (weight(x + 0.5, y + 0.5) <= 0.05) continue;
                t.set_ground(x, y, brush_ == Brush::Water ? sim::GroundType::Water : sim::GroundType::Grass);
            }
        }
        dirty_ = true;
        return;
    }
    // Heights change on a copy, so smoothing reads the land as it was.
    std::vector<std::int32_t> before;
    const int w = x1 - x0 + 1;
    for (int y = ya; y <= yb; ++y)
        for (int x = x0; x <= x1; ++x) before.push_back(t.corner_height(x, y));
    const auto old = [&](int x, int y) {
        x = std::clamp(x, x0, x1);
        y = std::clamp(y, ya, yb);
        return before[static_cast<std::size_t>((y - ya) * w + (x - x0))];
    };
    const double rate = max_height_m_ * 0.35; // metres a second at the middle
    for (int y = ya; y <= yb; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double k = weight(x, y);
            if (k <= 0) continue;
            const double h = old(x, y);
            double next = h;
            switch (brush_) {
            case Brush::Raise: next = h + rate * seconds * k; break;
            case Brush::Lower: next = h - rate * seconds * k; break;
            case Brush::Smooth: {
                const double around = (old(x - 1, y) + old(x + 1, y) + old(x, y - 1) + old(x, y + 1)) / 4.0;
                next = h + (around - h) * std::min(1.0, 8.0 * seconds * k);
                break;
            }
            case Brush::Flatten: next = h + (flatten_to_ - h) * std::min(1.0, 6.0 * seconds * k); break;
            default: break;
            }
            const auto m = static_cast<std::int32_t>(std::lround(std::clamp(next, 0.0, static_cast<double>(max_height_m_))));
            if (m != t.corner_height(x, y)) t.set_corner_height(x, y, m);
        }
    }
    dirty_ = true;
}

void Editor::draw_world_overlay() const {
    // The brush's reach, a ring on the land.
    const sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    const double r = radius_ * tile_mm;
    const float colour[3] = {brush_ == Brush::Water ? 0.4f : 1.0f, brush_ == Brush::Water ? 0.7f : 0.95f,
                             brush_ == Brush::Water ? 1.0f : 0.6f};
    constexpr int kSegments = 40;
    for (int i = 0; i < kSegments; ++i) {
        const double a0 = 2 * kPi * i / kSegments, a1 = 2 * kPi * (i + 1) / kSegments;
        const sim::MapPoint p0{hover_.x_mm + static_cast<std::int64_t>(r * std::cos(a0)), hover_.y_mm + static_cast<std::int64_t>(r * std::sin(a0))};
        const sim::MapPoint p1{hover_.x_mm + static_cast<std::int64_t>(r * std::cos(a1)), hover_.y_mm + static_cast<std::int64_t>(r * std::sin(a1))};
        draw_line(p0, p1, cam_, colour[0], colour[1], colour[2], 0.9f);
    }
    draw_marker(hover_, cam_, 3, colour[0], colour[1], colour[2]);
}

void Editor::draw_ui(float width, float height) const {
    const sim::Terrain& t = world_.terrain();
    ui::bar(0, 0, width, kBarH, true);
    glColor3f(0.95f, 0.95f, 0.9f);
    draw_text(8, 4, "Map editor: " + name_ + (dirty_ ? " (not saved)" : ""), kScale);
    const std::string size = std::to_string(t.width()) + " x " + std::to_string(t.height()) + " cells";
    draw_text(width - static_cast<float>(text_width(size, kScale)) - 8, 4, size, kScale);

    buttons_.clear();
    float x = 6;
    for (int i = 0; i < 6; ++i) {
        const float w = static_cast<float>(text_width(kBrushNames[i], kScale)) + 12;
        const bool on = static_cast<int>(brush_) == i;
        ui::tab(x, kToolbarY, x + w, kToolbarY + kButtonH, on);
        if (on) glColor3f(0.12f, 0.08f, 0.03f);
        else glColor3f(0.92f, 0.88f, 0.8f);
        draw_text(x + 6, kToolbarY + 4, kBrushNames[i], kScale);
        buttons_.emplace_back(x, x + w);
        x += w + 3;
    }
    glColor3f(0.92f, 0.88f, 0.8f);
    const std::string brush = "Brush " + std::to_string(radius_) + " cells  ([ ] to change)";
    draw_text(x + 14, kToolbarY + 4, brush, kScale);

    ui::bar(0, height - kBottomH, width, height, false);
    glColor3f(0.85f, 0.85f, 0.85f);
    draw_text(8, height - kBottomH + 6,
              "Left-drag shapes the land.  1-6 brush   [ ] size   S save the map   P play a railroad on it   Esc leave", kScale);
    std::string line2;
    if (SDL_GetTicks() < message_until_) {
        line2 = message_;
    } else {
        const double tile_mm = t.tile_size_m() * 1000.0;
        const auto tx = static_cast<std::int32_t>(static_cast<double>(hover_.x_mm) / tile_mm);
        const auto ty = static_cast<std::int32_t>(static_cast<double>(hover_.y_mm) / tile_mm);
        if (tx >= 0 && ty >= 0 && tx < t.width() && ty < t.height()) {
            line2 = "Height " + std::to_string(t.height_at_mm(hover_) / 1000) + " m" +
                    (t.ground(tx, ty) == sim::GroundType::Water ? ", water" : "");
        }
    }
    glColor3f(0.6f, 0.95f, 0.6f);
    draw_text(8, height - kBottomH + 26, line2, kScale);
}

} // namespace railmaster::client
