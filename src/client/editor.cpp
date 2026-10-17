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
constexpr const char* kBrushNames[] = {"1 Raise", "2 Lower", "3 Smooth", "4 Flatten", "5 Water", "6 Land", "7 Town", "8 Industry", "9 Territory"};
constexpr int kBrushes = 9;
// Town sizes, in houses: village to city.
constexpr std::int32_t kTownSizes[] = {8, 15, 25, 40, 60};

} // namespace

void Editor::begin(std::string name, std::int32_t max_height_m) {
    open_ = true;
    name_ = std::move(name);
    max_height_m_ = max_height_m;
    dirty_ = false;
    leave_armed_ = false;
    held_ = false;
    towns_.clear();
    industries_.clear();
    town_sel_.reset();
    terr_naming_ = false;
    terr_sel_ = 0;
    terr_used_.clear();
    terr_ = {};
    terr_.width = world_.terrain().width();
    terr_.height = world_.terrain().height();
    terr_.cells.assign(static_cast<std::size_t>(terr_.width) * static_cast<std::size_t>(terr_.height), sim::kNoTerritory);
}

void Editor::ensure_territory(int i) {
    // Territories come into being as they are chosen: the first free to all,
    // the rest priced as a generated map's are.
    sim::Random rng(static_cast<std::uint64_t>(SDL_GetTicks()) + static_cast<std::uint64_t>(terr_.territories.size()) * 7919);
    while (static_cast<int>(terr_.territories.size()) <= i) {
        terr_.territories.push_back(
            sim::random_territory(rng, world_.data().balance.map, terr_.territories.empty(), terr_used_));
    }
}

void Editor::set_territories(sim::TerritoryMap map) {
    if (map.width != terr_.width || map.height != terr_.height || map.cells.size() != terr_.cells.size()) return;
    terr_ = std::move(map);
    for (const sim::Territory& t : terr_.territories) terr_used_.push_back(t.name);
    world_.set_territories(terr_);
}

sim::TerritoryMap Editor::territories() const {
    // Only territories that hold cells, renumbered in order.
    std::vector<sim::TerritoryId> remap(terr_.territories.size(), sim::kNoTerritory);
    for (const sim::TerritoryId c : terr_.cells) {
        if (c != sim::kNoTerritory && c < remap.size()) remap[c] = 0;
    }
    sim::TerritoryMap out;
    for (std::size_t i = 0; i < remap.size(); ++i) {
        if (remap[i] == sim::kNoTerritory) continue;
        remap[i] = static_cast<sim::TerritoryId>(out.territories.size());
        out.territories.push_back(terr_.territories[i]);
    }
    if (out.territories.empty()) return {};
    out.width = terr_.width;
    out.height = terr_.height;
    out.cells.reserve(terr_.cells.size());
    for (const sim::TerritoryId c : terr_.cells) out.cells.push_back(c == sim::kNoTerritory || c >= remap.size() ? sim::kNoTerritory : remap[c]);
    return out;
}

void Editor::note(std::string message) {
    message_ = std::move(message);
    message_until_ = SDL_GetTicks() + 6000;
}

void Editor::saved(const std::string& path) {
    dirty_ = false;
    message_ = "Saved as " + path + ": it is on the title screen's list of landscapes";
    message_until_ = SDL_GetTicks() + 6000;
}

bool Editor::key(SDL_Keycode key) {
    if (key != SDLK_ESCAPE) leave_armed_ = false;
    if (scen_open_) return scen_key(key);
    // Naming a territory: the keys type.
    if (terr_naming_) {
        if (terr_sel_ < 0 || terr_sel_ >= static_cast<int>(terr_.territories.size())) {
            terr_naming_ = false;
            return true;
        }
        std::string& name = terr_.territories[static_cast<std::size_t>(terr_sel_)].name;
        if (key == SDLK_BACKSPACE) {
            if (fresh_name_) name.clear();
            else if (!name.empty()) name.pop_back();
            fresh_name_ = false;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            if (name.empty()) name = "Unnamed";
            terr_naming_ = false;
        }
        world_.set_territories(terr_);
        dirty_ = true;
        return true;
    }
    // Naming a town: the keys type (letters arrive as text).
    if (town_sel_ && *town_sel_ < towns_.size()) {
        std::string& name = towns_[*town_sel_].name;
        switch (key) {
        case SDLK_BACKSPACE:
            if (fresh_name_) name.clear();
            else if (!name.empty()) name.pop_back();
            fresh_name_ = false;
            dirty_ = true;
            return true;
        case SDLK_DELETE:
            towns_.erase(towns_.begin() + static_cast<std::ptrdiff_t>(*town_sel_));
            town_sel_.reset();
            dirty_ = true;
            return true;
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET: {
            // A size smaller or larger.
            std::int32_t& houses = towns_[*town_sel_].houses;
            int at = 0;
            while (at < 4 && kTownSizes[at] < houses) ++at;
            at = std::clamp(at + (key == SDLK_RIGHTBRACKET ? 1 : -1), 0, 4);
            houses = kTownSizes[at];
            dirty_ = true;
            return true;
        }
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
            if (name.empty()) name = "Unnamed";
            town_sel_.reset();
            return true;
        default: return true; // letters come as text
        }
    }
    switch (key) {
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
    case SDLK_7:
    case SDLK_8: brush_ = static_cast<Brush>(key - SDLK_1); return true;
    case SDLK_g:
        scen_open_ = true;
        town_sel_.reset();
        return true;
    case SDLK_9:
        brush_ = Brush::Territory;
        if (terr_sel_ >= 0) ensure_territory(terr_sel_);
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        // Rename the territory being painted.
        if (brush_ == Brush::Territory && terr_sel_ >= 0) {
            ensure_territory(terr_sel_);
            terr_naming_ = true;
            fresh_name_ = true;
        }
        return true;
    case SDLK_o:
        // Free to all, or priced again.
        if (brush_ == Brush::Territory && terr_sel_ >= 0) {
            ensure_territory(terr_sel_);
            sim::Territory& t = terr_.territories[static_cast<std::size_t>(terr_sel_)];
            if (t.open()) {
                sim::Random rng(static_cast<std::uint64_t>(SDL_GetTicks()));
                std::vector<std::string> any;
                const sim::Territory priced = sim::random_territory(rng, world_.data().balance.map, false, any);
                t.access_cost = priced.access_cost;
                t.station_cost_percent = priced.station_cost_percent;
                t.overhead_percent = priced.overhead_percent;
                t.credit_grades = priced.credit_grades;
                t.closed_border = priced.closed_border;
            } else {
                t.access_cost = sim::Money{};
                t.station_cost_percent = t.overhead_percent = 100;
                t.credit_grades = 0;
                t.closed_border = false;
            }
            world_.set_territories(terr_);
            dirty_ = true;
        }
        return true;
    case SDLK_COMMA:
    case SDLK_PERIOD:
        if (brush_ == Brush::Territory) {
            // The territory painted: none (erase), then 1 to 8.
            terr_sel_ = (terr_sel_ + 1 + (key == SDLK_PERIOD ? 1 : kMaxTerritories)) % (kMaxTerritories + 1) - 1;
            if (terr_sel_ >= 0) ensure_territory(terr_sel_);
            return true;
        }
        {
        // The industry the brush puts down: the previous or next type.
        const std::size_t n = placeable().size();
        if (n > 0) industry_type_ = (industry_type_ + (key == SDLK_PERIOD ? 1 : n - 1)) % n;
        brush_ = Brush::Industry;
        return true;
        }
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
    if (scen_open_) return true;
    if (button != SDL_BUTTON_LEFT) return false;
    // The brush buttons along the top.
    if (sy >= kToolbarY && sy < kToolbarY + kButtonH) {
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            if (sx >= buttons_[i].first && sx < buttons_[i].second) brush_ = static_cast<Brush>(i);
        }
        if (brush_ == Brush::Territory && terr_sel_ >= 0) ensure_territory(terr_sel_);
        return true;
    }
    mouse_move(sx, sy);
    if (brush_ == Brush::Town) {
        town_click();
        return true;
    }
    if (brush_ == Brush::Industry) {
        industry_click();
        return true;
    }
    town_sel_.reset();
    held_ = true;
    pending_ = 0.05; // a click shapes a little at once
    const sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    const auto cx = std::clamp(static_cast<std::int32_t>(std::lround(static_cast<double>(hover_.x_mm) / tile_mm)), 0, t.width());
    const auto cy = std::clamp(static_cast<std::int32_t>(std::lround(static_cast<double>(hover_.y_mm) / tile_mm)), 0, t.height());
    flatten_to_ = t.corner_height(cx, cy);
    return true;
}

void Editor::town_click() {
    const sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    const auto cx = static_cast<std::int32_t>(static_cast<double>(hover_.x_mm) / tile_mm);
    const auto cy = static_cast<std::int32_t>(static_cast<double>(hover_.y_mm) / tile_mm);
    if (cx < 0 || cy < 0 || cx >= t.width() || cy >= t.height()) return;
    // A click near a town picks it; elsewhere it founds one.
    for (std::size_t i = 0; i < towns_.size(); ++i) {
        if (std::abs(towns_[i].x - cx) <= 4 && std::abs(towns_[i].y - cy) <= 4) {
            town_sel_ = i;
            fresh_name_ = true;
            return;
        }
    }
    sim::Random rng(name_seed_++ * 2654435761u + static_cast<std::uint32_t>(towns_.size()));
    std::string name = sim::town_name(rng);
    towns_.push_back({name, cx, cy, kTownSizes[2]});
    town_sel_ = towns_.size() - 1;
    fresh_name_ = true;
    dirty_ = true;
}

std::vector<const sim::IndustryType*> Editor::placeable() const {
    // Every industry but houses (towns place those), in the data's order.
    std::vector<const sim::IndustryType*> out;
    for (const sim::IndustryType& t : world_.data().industries.all()) {
        if (t.kind != sim::IndustryKind::House) out.push_back(&t);
    }
    return out;
}

void Editor::industry_click() {
    const sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    const auto cx = static_cast<std::int32_t>(static_cast<double>(hover_.x_mm) / tile_mm);
    const auto cy = static_cast<std::int32_t>(static_cast<double>(hover_.y_mm) / tile_mm);
    if (cx < 0 || cy < 0 || cx >= t.width() || cy >= t.height()) return;
    town_sel_.reset();
    // A click on an industry removes it; elsewhere it puts one down.
    for (std::size_t i = 0; i < industries_.size(); ++i) {
        if (std::abs(industries_[i].x - cx) <= 2 && std::abs(industries_[i].y - cy) <= 2) {
            industries_.erase(industries_.begin() + static_cast<std::ptrdiff_t>(i));
            dirty_ = true;
            return;
        }
    }
    const auto types = placeable();
    if (types.empty()) return;
    if (t.ground(cx, cy) == sim::GroundType::Water) {
        message_ = "Industries stand on dry land";
        message_until_ = SDL_GetTicks() + 2500;
        return;
    }
    industries_.push_back({types[industry_type_ % types.size()]->key, cx, cy});
    dirty_ = true;
}

void Editor::text(const char* utf8) {
    const std::string typed = utf8;
    if (scen_open_) {
        if (scen_text_ == 1 && scen_.name.size() < 40) scen_.name += typed;
        if (scen_text_ == 2 && scen_.briefing.size() < 300) scen_.briefing += typed;
        return;
    }
    if (terr_naming_ && terr_sel_ >= 0 && terr_sel_ < static_cast<int>(terr_.territories.size())) {
        std::string& name = terr_.territories[static_cast<std::size_t>(terr_sel_)].name;
        if (fresh_name_) name.clear();
        fresh_name_ = false;
        if (name.size() < 28) name += typed;
        world_.set_territories(terr_);
        dirty_ = true;
        return;
    }
    if (!town_sel_ || *town_sel_ >= towns_.size()) return;
    // [ and ] size the town; they are not part of its name.
    if (typed == "[" || typed == "]") return;
    std::string& name = towns_[*town_sel_].name;
    if (fresh_name_) name.clear();
    fresh_name_ = false;
    if (name.size() < 28) name += typed;
    dirty_ = true;
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
    if (brush_ == Brush::Town || brush_ == Brush::Industry) return;
    if (brush_ == Brush::Territory) {
        if (terr_sel_ >= 0) ensure_territory(terr_sel_);
        const sim::TerritoryId id = terr_sel_ < 0 ? sim::kNoTerritory : static_cast<sim::TerritoryId>(terr_sel_);
        for (int y = ya; y < yb; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (weight(x + 0.5, y + 0.5) <= 0.05) continue;
                terr_.cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(terr_.width) + static_cast<std::size_t>(x)] = id;
            }
        }
        world_.set_territories(terr_);
        dirty_ = true;
        return;
    }
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
    const sim::Terrain& t = world_.terrain();
    const double tile_mm = t.tile_size_m() * 1000.0;
    // The towns' places.
    for (std::size_t i = 0; i < towns_.size(); ++i) {
        const sim::MapPoint p{static_cast<std::int64_t>((towns_[i].x + 0.5) * tile_mm), static_cast<std::int64_t>((towns_[i].y + 0.5) * tile_mm)};
        const bool sel = town_sel_ && *town_sel_ == i;
        draw_marker(p, cam_, sel ? 9.0f : 6.0f, 1.0f, sel ? 0.95f : 0.8f, sel ? 0.6f : 0.35f);
    }
    for (const sim::MapIndustry& m : industries_) {
        const sim::MapPoint p{static_cast<std::int64_t>((m.x + 0.5) * tile_mm), static_cast<std::int64_t>((m.y + 0.5) * tile_mm)};
        draw_marker(p, cam_, 5.0f, 0.55f, 0.8f, 1.0f);
    }
    if (brush_ == Brush::Town || brush_ == Brush::Industry) {
        draw_marker(hover_, cam_, 4, 1.0f, 0.85f, 0.4f);
        return;
    }
    // The brush's reach, a ring on the land.
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
    for (int i = 0; i < kBrushes; ++i) {
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
    std::string brush = "Brush " + std::to_string(radius_) + " cells  ([ ] to change)";
    if (brush_ == Brush::Town) brush = "Click land to found a town, or a town to pick it";
    if (brush_ == Brush::Territory) {
        if (terr_sel_ < 0 || terr_sel_ >= static_cast<int>(terr_.territories.size())) {
            brush = "< none: erase >  (, and . to choose)";
        } else {
            const sim::Territory& tr = terr_.territories[static_cast<std::size_t>(terr_sel_)];
            brush = "< " + std::to_string(terr_sel_ + 1) + " " + tr.name + (terr_naming_ && (SDL_GetTicks() / 500) % 2 == 0 ? "|" : "") +
                    ", " + (tr.open() ? std::string("free") : "$" + std::to_string(tr.access_cost.whole_dollars() / 1000) + "K") +
                    " >  , . choose   Enter rename   O free/priced   brush " + std::to_string(radius_);
        }
    }
    if (brush_ == Brush::Industry) {
        const auto types = placeable();
        brush = "< " + (types.empty() ? std::string("none") : types[industry_type_ % types.size()]->name) +
                " >  (, and . to choose)   click to place, click one to remove";
    }
    draw_text(x + 14, kToolbarY + 4, brush, kScale);

    ui::bar(0, height - kBottomH, width, height, false);
    glColor3f(0.85f, 0.85f, 0.85f);
    draw_text(8, height - kBottomH + 6,
              town_sel_ ? "Type the town's name   [ ] smaller or larger   Delete removes it   Enter done"
                        : "Left-drag shapes the land.  1-9 brush   [ ] size   S save   P play   G scenario   Esc leave",
              kScale);
    // The industries' names, close up.
    if (cam_.distance < 70.0f) {
        for (const sim::MapIndustry& m : industries_) {
            float sx = 0, sy = 0;
            cam_.to_screen({static_cast<std::int64_t>((m.x + 0.5) * t.tile_size_m() * 1000.0),
                            static_cast<std::int64_t>((m.y + 0.5) * t.tile_size_m() * 1000.0)},
                           sx, sy);
            if (sx < -100 || sy < 40 || sx > width + 100 || sy > height - kBottomH) continue;
            const auto type = world_.data().industries.find(m.type);
            const std::string label = type ? world_.data().industries.get(*type).name : m.type;
            const auto w = static_cast<float>(text_width(label, kScale));
            ui::chip(sx - w / 2 - 5, sy + 8, sx + w / 2 + 5, sy + 26, 0.5f);
            glColor3f(0.75f, 0.88f, 1.0f);
            draw_text(sx - w / 2, sy + 10, label, kScale);
        }
    }
    // The towns' names, on chips over their places.
    const double cell_mm = t.tile_size_m() * 1000.0;
    for (std::size_t i = 0; i < towns_.size(); ++i) {
        float sx = 0, sy = 0;
        cam_.to_screen({static_cast<std::int64_t>((towns_[i].x + 0.5) * cell_mm), static_cast<std::int64_t>((towns_[i].y + 0.5) * cell_mm)}, sx,
                       sy);
        if (sx < -100 || sy < 40 || sx > width + 100 || sy > height - kBottomH) continue;
        const bool sel = town_sel_ && *town_sel_ == i;
        const std::string label = towns_[i].name + (sel && (SDL_GetTicks() / 500) % 2 == 0 ? "|" : "") + "  (" +
                                  std::to_string(towns_[i].houses) + " houses)";
        const auto w = static_cast<float>(text_width(label, kScale));
        ui::chip(sx - w / 2 - 6, sy - 30, sx + w / 2 + 6, sy - 10, sel ? 0.85f : 0.62f);
        if (sel) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.95f, 0.92f, 0.85f);
        draw_text(sx - w / 2, sy - 28, label, kScale);
    }
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
    if (scen_open_) draw_scenario(width, height);
}


namespace {

// The goals a scenario made in the editor can set, one a medal.
constexpr const char* kGoalNames[] = {"Connect towns", "Book value",       "Revenue in a year", "Company cash",
                                      "Your net worth", "Average train speed", "Only railroad left", "Highest value"};
constexpr int kGoalKinds = 8;
constexpr std::int64_t kMoneyLadder[] = {1, 2, 3, 5, 8, 10, 15, 20, 30, 50, 75, 100}; // millions
constexpr int kMoneySteps = 12;
constexpr int kSpeedSteps = 14; // 15 to 80 mph

int ladder_size(int kind, int towns) {
    switch (kind) {
    case 0: return std::max(1, towns - 1); // 2 towns up to all of them
    case 5: return kSpeedSteps;
    case 6:
    case 7: return 1;
    default: return kMoneySteps;
    }
}

const char* map_size_name(std::int32_t width) {
    // The presets' widths: small 256, medium 384, large 768 cells.
    return width >= 700 ? "large" : width >= 350 ? "medium" : "small";
}

} // namespace

std::string Editor::goal_text(int m) const {
    const ScenarioDraft::Medal& md = scen_.medals[m];
    const auto towns = static_cast<int>(towns_.size());
    switch (md.kind) {
    case 0: {
        if (towns < 2) return "Connect towns (place two towns or more first)";
        const int n = std::min(md.level + 2, towns);
        std::string names;
        for (int i = 0; i < n; ++i) names += (i ? (i + 1 == n ? " and " : ", ") : "") + towns_[static_cast<std::size_t>(i)].name;
        return "Connect " + names;
    }
    case 5: return "Average train speed " + std::to_string(15 + 5 * std::min(md.level, kSpeedSteps - 1)) + " mph";
    case 6:
    case 7: return kGoalNames[md.kind];
    default:
        return std::string(kGoalNames[md.kind]) + " $" + std::to_string(kMoneyLadder[std::min(md.level, kMoneySteps - 1)]) + ",000,000";
    }
}

nlohmann::json Editor::scenario_json(const std::string& key, const std::string& landscape, const std::string& map_size) const {
    nlohmann::json towns = nlohmann::json::array();
    for (const sim::ScenarioTown& t : towns_) towns.push_back({{"name", t.name}, {"x", t.x}, {"y", t.y}, {"houses", t.houses}});
    nlohmann::json medals;
    const char* tiers[] = {"bronze", "silver", "gold"};
    int years = 0;
    for (int m = 0; m < 3; ++m) {
        const ScenarioDraft::Medal& md = scen_.medals[m];
        years = std::max(years, md.years); // each medal's deadline no earlier than the last
        nlohmann::json goal;
        switch (md.kind) {
        case 0: {
            nlohmann::json names = nlohmann::json::array();
            const int n = std::min(md.level + 2, static_cast<int>(towns_.size()));
            for (int i = 0; i < n; ++i) names.push_back(towns_[static_cast<std::size_t>(i)].name);
            goal = {{"kind", "connect_towns"}, {"towns", names}};
            break;
        }
        case 1: goal = {{"kind", "book_value"}}; break;
        case 2: goal = {{"kind", "revenue"}}; break;
        case 3: goal = {{"kind", "company_cash"}}; break;
        case 4: goal = {{"kind", "personal_net_worth"}}; break;
        case 5: goal = {{"kind", "average_speed"}, {"count", 15 + 5 * std::min(md.level, kSpeedSteps - 1)}}; break;
        case 6: goal = {{"kind", "only_railroad"}}; break;
        default: goal = {{"kind", "highest_value"}}; break;
        }
        if (md.kind >= 1 && md.kind <= 4) goal["amount"] = kMoneyLadder[std::min(md.level, kMoneySteps - 1)] * 1'000'000;
        medals[tiers[m]] = {{"by", std::to_string(scen_.start_year + years) + "-12-31"}, {"goals", nlohmann::json::array({goal})}};
    }
    return {{"key", key},
            {"name", scen_.name},
            {"briefing", scen_.briefing},
            {"start", std::to_string(scen_.start_year) + "-01-01"},
            {"seed", 1},
            {"map", map_size.empty() ? std::string(map_size_name(world_.terrain().width())) : map_size},
            {"rivals", scen_.rivals},
            {"landscape", landscape},
            {"towns", towns},
            {"medals", medals}};
}

void Editor::scen_change(int step) {
    const int towns = static_cast<int>(towns_.size());
    switch (scen_sel_) {
    case 2: scen_.start_year = std::clamp(scen_.start_year + step * 5, 1830, 2000); return;
    case 3: scen_.rivals = std::clamp(scen_.rivals + step, 0, 5); return;
    default: break;
    }
    if (scen_sel_ >= 4 && scen_sel_ <= 12) {
        ScenarioDraft::Medal& md = scen_.medals[(scen_sel_ - 4) / 3];
        switch ((scen_sel_ - 4) % 3) {
        case 0:
            md.kind = (md.kind + step + kGoalKinds) % kGoalKinds;
            md.level = std::min(md.level, ladder_size(md.kind, towns) - 1);
            break;
        case 1: md.level = std::clamp(md.level + step, 0, ladder_size(md.kind, towns) - 1); break;
        default: md.years = std::clamp(md.years + step, 1, 60); break;
        }
    }
}

bool Editor::scen_key(SDL_Keycode key) {
    constexpr int kRows = 16; // name, briefing, start, rivals, 3 x (goal, target, by), save, play, close
    if (scen_text_ != 0) {
        std::string& s = scen_text_ == 1 ? scen_.name : scen_.briefing;
        if (key == SDLK_BACKSPACE && !s.empty()) s.pop_back();
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            if (s.empty()) s = scen_text_ == 1 ? "My scenario" : "Build a railroad to be proud of.";
            scen_text_ = 0;
        }
        return true;
    }
    switch (key) {
    case SDLK_UP: scen_sel_ = (scen_sel_ + kRows - 1) % kRows; return true;
    case SDLK_DOWN: scen_sel_ = (scen_sel_ + 1) % kRows; return true;
    case SDLK_LEFT: scen_change(-1); return true;
    case SDLK_RIGHT: scen_change(+1); return true;
    case SDLK_ESCAPE:
    case SDLK_g: scen_open_ = false; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (scen_sel_ <= 1) {
            scen_text_ = scen_sel_ + 1;
            return true;
        }
        if (scen_sel_ == 13 || scen_sel_ == 14) {
            // Connecting towns needs towns to connect.
            for (const ScenarioDraft::Medal& md : scen_.medals) {
                if (md.kind == 0 && towns_.size() < 2) {
                    message_ = "A goal connects towns, but the map has fewer than two: place towns with the Town brush";
                    message_until_ = SDL_GetTicks() + 5000;
                    return true;
                }
            }
            request_ = Request{scen_sel_ == 13 ? Request::Kind::SaveScenario : Request::Kind::PlayScenario};
            if (scen_sel_ == 14) scen_open_ = false;
            return true;
        }
        if (scen_sel_ == 15) scen_open_ = false;
        else scen_change(+1);
        return true;
    default: return true;
    }
}

void Editor::draw_scenario(float width, float height) const {
    const float w = 720, x0 = (width - w) / 2;
    const float y0 = std::max(50.0f, (height - 600) / 2);
    ui::panel(x0, y0, x0 + w, y0 + 590);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 16, y0 + 10, "Scenario for this map", kScale + 1);
    ui::rule(x0 + 10, x0 + w - 10, y0 + 40);
    float y = y0 + 50;
    const bool blink = (SDL_GetTicks() / 500) % 2 == 0;
    const auto row = [&](int i, const std::string& label, const std::string& value) {
        const bool sel = scen_sel_ == i;
        if (sel) ui::selection(x0 + 8, y - 3, x0 + w - 8, y + 23);
        if (sel) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.9f, 0.88f, 0.82f);
        draw_text(x0 + 20, y, label, kScale);
        draw_text(x0 + 200, y, value, kScale);
        y += 28;
    };
    row(0, "Name", scen_.name + (scen_text_ == 1 && blink ? "|" : ""));
    {
        // The briefing, wrapped over a few lines.
        std::string b = scen_.briefing + (scen_text_ == 2 && blink ? "|" : "");
        std::vector<std::string> lines;
        while (!b.empty() && lines.size() < 3) {
            std::size_t cut = b.size();
            while (cut > 0 && text_width(b.substr(0, cut), kScale) > 480) {
                const std::size_t sp = b.rfind(' ', cut - 1);
                cut = sp == std::string::npos || sp == 0 ? cut - 1 : sp;
            }
            lines.push_back(b.substr(0, cut));
            b = cut < b.size() ? b.substr(cut + (b[cut] == ' ' ? 1 : 0)) : "";
        }
        if (lines.empty()) lines.emplace_back();
        row(1, "Briefing", lines[0]);
        for (std::size_t i = 1; i < lines.size(); ++i) {
            glColor3f(0.85f, 0.83f, 0.77f);
            draw_text(x0 + 200, y - 6, lines[i], kScale);
            y += 20;
        }
    }
    row(2, "Starts", "<  " + std::to_string(scen_.start_year) + "  >");
    row(3, "Rival railroads", "<  " + std::to_string(scen_.rivals) + "  >");
    const char* tiers[] = {"Bronze", "Silver", "Gold"};
    int years = 0;
    for (int m = 0; m < 3; ++m) {
        y += 6;
        years = std::max(years, scen_.medals[m].years);
        row(4 + 3 * m, std::string(tiers[m]) + " goal", "<  " + std::string(kGoalNames[scen_.medals[m].kind]) + "  >");
        row(5 + 3 * m, "    target", "<  " + goal_text(m) + "  >");
        row(6 + 3 * m, "    by", "<  the end of " + std::to_string(scen_.start_year + years) + "  >");
    }
    y += 6;
    row(13, "Save the scenario", "");
    row(14, "Save it and play it", "");
    row(15, "Close", "");
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 20, y0 + 590 - 26,
              scen_text_ ? "Type, then Enter" : "Up/Down choose   Left/Right change   Enter edits the name or briefing   G or Esc close",
              kScale);
}

} // namespace railmaster::client
