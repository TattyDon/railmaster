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
        if (scen_text_ == 3 && scen_text_event_ < scen_.events.size() && scen_.events[scen_text_event_].text.size() < 80) {
            scen_.events[scen_text_event_].text += typed;
        }
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

// The goals a scenario made in the editor can set, two a medal at most.
constexpr const char* kGoalNames[] = {"Connect towns",    "Book value",         "Revenue in a year",  "Company cash",
                                      "Your net worth",   "Average train speed", "Only railroad left", "Highest value",
                                      "Industry profit",  "Stations in territories", "Deliver goods to a town"};
constexpr int kGoalKinds = 11;
constexpr std::int64_t kLoadLadder[] = {10, 25, 50, 100, 200}; // deliver goals, loads
constexpr int kLoadSteps = 5;
constexpr std::int64_t kMoneyLadder[] = {1, 2, 3, 5, 8, 10, 15, 20, 30, 50, 75, 100}; // millions
constexpr int kMoneySteps = 12;
constexpr int kSpeedSteps = 14; // 15 to 80 mph

bool money_goal(int kind) { return (kind >= 1 && kind <= 4) || kind == 8; }

int ladder_size(int kind, int towns, int territories) {
    switch (kind) {
    case 0: return std::max(1, towns - 1); // 2 towns up to all of them
    case 5: return kSpeedSteps;
    case 6:
    case 7: return 1;
    case 9: return std::max(1, territories - 1); // 2 territories up to all of them
    case 10: return std::max(1, towns) * kLoadSteps; // each town, each number of loads
    default: return kMoneySteps;
    }
}

// Founding money: each founder's fortune, what they put in, and what
// outside investors match it with. The middle step is balance.json's.
constexpr const char* kMoneyNames[] = {"Lean: $1,500,000 each founder", "Usual: $3,500,000 each founder",
                                       "Rich: $7,000,000 each founder"};
constexpr std::int64_t kMoneyFortune[] = {1'500'000, 0, 7'000'000};
constexpr std::int64_t kMoneyInvest[] = {1'000'000, 0, 6'000'000};

// Track budgets in map cells: to start with, and more each year.
constexpr int kBudgetStart[] = {0, 300, 500, 800, 1200};
constexpr int kBudgetYear[] = {0, 50, 80, 100, 150};
constexpr int kBudgetSteps = 5;

// The events the editor makes, each a template (rt3-clone-spec §14.4).
enum EventKind { kNews, kCargoPrice, kOutput, kBuildCost, kGrant, kQuake, kEngine, kEventKinds };
constexpr const char* kEventNames[] = {"Headline", "Cargo price", "Industry output", "Building costs",
                                       "Grant to your company", "Earthquake", "Engine arrives early"};
constexpr std::int32_t kEventPercent[] = {50, 75, 125, 150, 200};
constexpr std::int64_t kEventGrant[] = {100'000, 250'000, 500'000, 1'000'000, 2'000'000};
constexpr std::int32_t kEventMonths[] = {6, 12, 24, 36, 0}; // 0: for good
constexpr const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
constexpr int kMaxEvents = 8;

std::string with_commas(std::int64_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

const char* map_size_name(std::int32_t width) {
    // The presets' widths: small 256, medium 384, large 768 cells.
    return width >= 700 ? "large" : width >= 350 ? "medium" : "small";
}

} // namespace

int Editor::territory_count() const {
    // Only the territories that hold a cell count.
    int n = 0;
    for (std::size_t t = 0; t < terr_.territories.size(); ++t) {
        n += std::find(terr_.cells.begin(), terr_.cells.end(), static_cast<sim::TerritoryId>(t)) != terr_.cells.end();
    }
    return n;
}

std::string Editor::goal_text(const ScenarioDraft::Goal& g) const {
    const auto towns = static_cast<int>(towns_.size());
    switch (g.kind) {
    case -1: return "";
    case 0: {
        if (towns < 2) return "Connect towns (place two towns or more first)";
        const int n = std::min(g.level + 2, towns);
        std::string names;
        for (int i = 0; i < n; ++i) names += (i ? (i + 1 == n ? " and " : ", ") : "") + towns_[static_cast<std::size_t>(i)].name;
        return "Connect " + names;
    }
    case 5: return "Average train speed " + std::to_string(15 + 5 * std::min(g.level, kSpeedSteps - 1)) + " mph";
    case 6:
    case 7: return kGoalNames[g.kind];
    case 9: {
        const int terr = territory_count();
        if (terr < 2) return "Stations in territories (paint two territories or more first)";
        return "Stations in " + std::to_string(std::min(g.level + 2, terr)) + " territories";
    }
    case 10: {
        if (towns < 1) return "Deliver goods to a town (place a town first)";
        const int level = std::min(g.level, towns * kLoadSteps - 1);
        return "Deliver " + std::to_string(kLoadLadder[level % kLoadSteps]) + " loads of goods to " +
               towns_[static_cast<std::size_t>(level / kLoadSteps)].name;
    }
    default:
        return std::string(kGoalNames[g.kind]) + " $" + std::to_string(kMoneyLadder[std::min(g.level, kMoneySteps - 1)]) + ",000,000";
    }
}

nlohmann::json Editor::goal_json(const ScenarioDraft::Goal& g) const {
    nlohmann::json goal;
    switch (g.kind) {
    case 0: {
        nlohmann::json names = nlohmann::json::array();
        const int n = std::min(g.level + 2, static_cast<int>(towns_.size()));
        for (int i = 0; i < n; ++i) names.push_back(towns_[static_cast<std::size_t>(i)].name);
        goal = {{"kind", "connect_towns"}, {"towns", names}};
        break;
    }
    case 1: goal = {{"kind", "book_value"}}; break;
    case 2: goal = {{"kind", "revenue"}}; break;
    case 3: goal = {{"kind", "company_cash"}}; break;
    case 4: goal = {{"kind", "personal_net_worth"}}; break;
    case 5: goal = {{"kind", "average_speed"}, {"count", 15 + 5 * std::min(g.level, kSpeedSteps - 1)}}; break;
    case 6: goal = {{"kind", "only_railroad"}}; break;
    case 7: goal = {{"kind", "highest_value"}}; break;
    case 8: goal = {{"kind", "industry_profit"}}; break;
    case 10: {
        const int level = std::min(g.level, std::max(1, static_cast<int>(towns_.size())) * kLoadSteps - 1);
        goal = {{"kind", "deliver"}, {"cargo", "goods"}, {"count", kLoadLadder[level % kLoadSteps]}};
        if (!towns_.empty()) goal["to"] = towns_[static_cast<std::size_t>(level / kLoadSteps)].name;
        break;
    }
    default: goal = {{"kind", "territories"}, {"count", std::min(g.level + 2, territory_count())}}; break;
    }
    if (money_goal(g.kind)) goal["amount"] = kMoneyLadder[std::min(g.level, kMoneySteps - 1)] * 1'000'000;
    return goal;
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
        nlohmann::json goals = nlohmann::json::array({goal_json(md.goal)});
        if (md.also.kind >= 0) goals.push_back(goal_json(md.also));
        medals[tiers[m]] = {{"by", std::to_string(scen_.start_year + years) + "-12-31"}, {"goals", goals}};
    }
    nlohmann::json j = {{"key", key},
                        {"name", scen_.name},
                        {"briefing", scen_.briefing},
                        {"start", std::to_string(scen_.start_year) + "-01-01"},
                        {"seed", 1},
                        {"map", map_size.empty() ? std::string(map_size_name(world_.terrain().width())) : map_size},
                        {"rivals", scen_.rivals},
                        {"landscape", landscape},
                        {"towns", towns},
                        {"medals", medals}};
    if (kMoneyFortune[scen_.money] != 0) {
        j["founder_fortune"] = kMoneyFortune[scen_.money];
        j["founder_investment"] = kMoneyInvest[scen_.money];
        j["outside_investment"] = kMoneyInvest[scen_.money];
    }
    if (scen_.chair_locked) {
        j["chairman_can_resign"] = false;
        j["chairman_can_be_fired"] = false;
    }
    nlohmann::json rules = nlohmann::json::object();
    if (scen_.track_budget > 0) {
        rules["track_budget"] = {{"initial", kBudgetStart[scen_.track_budget]}, {"per_year", kBudgetYear[scen_.track_budget]}};
    }
    if (scen_.connected_track) rules["no_unconnected_track"] = true;
    if (scen_.one_company) rules["one_company_only"] = true;
    if (!rules.empty()) j["restrictions"] = rules;
    if (!scen_.events.empty()) j["events"] = events_json();
    return j;
}

std::vector<std::string> Editor::event_choices(int kind) const {
    std::vector<std::string> out;
    switch (kind) {
    case kCargoPrice:
        for (const sim::CargoType& c : world_.data().cargo.all()) out.push_back(c.name);
        break;
    case kOutput:
        for (const sim::IndustryType& t : world_.data().industries.all())
            if (t.kind != sim::IndustryKind::Sink) out.push_back(t.name);
        break;
    case kQuake:
        for (const sim::ScenarioTown& t : towns_) out.push_back(t.name);
        break;
    case kEngine:
        for (const sim::LocomotiveType& l : world_.data().locomotives.all()) out.push_back(l.name);
        break;
    default: break;
    }
    return out;
}

std::string Editor::event_amount_text(const ScenarioDraft::Event& e) const {
    if (e.kind == kGrant) return "$" + with_commas(kEventGrant[e.amount]);
    const std::int32_t p = kEventPercent[e.amount];
    return (p > 100 ? "+" : "") + std::to_string(p - 100) + "%";
}

std::string Editor::event_headline(const ScenarioDraft::Event& e) const {
    const std::vector<std::string> what = event_choices(e.kind);
    const std::string name = what.empty() ? std::string() : what[static_cast<std::size_t>(e.what) % what.size()];
    const bool up = kEventPercent[e.amount] > 100;
    switch (e.kind) {
    case kNews: return e.text;
    case kCargoPrice: return name + (up ? " prices soar" : " prices slump");
    case kOutput: return name + (up ? " output booms" : " output falls");
    case kBuildCost: return up ? "Building costs rise" : "Building costs fall";
    case kGrant: return "The government grants the railroad " + event_amount_text(e);
    case kEngine: return "The " + name + " arrives early";
    default: return "";
    }
}

nlohmann::json Editor::events_json() const {
    nlohmann::json out = nlohmann::json::array();
    for (std::size_t i = 0; i < scen_.events.size(); ++i) {
        const ScenarioDraft::Event& e = scen_.events[i];
        const std::string date = std::to_string(scen_.start_year + e.year) + (e.month < 9 ? "-0" : "-") +
                                 std::to_string(e.month + 1) + "-01";
        nlohmann::json effects = nlohmann::json::array();
        // News first, so the player knows what happened (an earthquake reports itself).
        if (e.kind != kQuake) effects.push_back({{"kind", "news"}, {"text", event_headline(e)}});
        const std::int32_t percent = kEventPercent[e.amount], months = kEventMonths[e.months];
        const auto key_of = [&](auto&& list, auto&& keep) {
            std::vector<std::string> keys;
            for (const auto& x : list)
                if (keep(x)) keys.push_back(x.key);
            return keys.empty() ? std::string() : keys[static_cast<std::size_t>(e.what) % keys.size()];
        };
        const auto all = [](const auto&) { return true; };
        switch (e.kind) {
        case kCargoPrice:
            effects.push_back({{"kind", "cargo_price"}, {"target", key_of(world_.data().cargo.all(), all)}, {"percent", percent},
                               {"months", months}});
            break;
        case kOutput:
            effects.push_back({{"kind", "production"},
                               {"target", key_of(world_.data().industries.all(),
                                                 [](const sim::IndustryType& t) { return t.kind != sim::IndustryKind::Sink; })},
                               {"percent", percent},
                               {"months", months}});
            break;
        case kBuildCost: effects.push_back({{"kind", "build_cost"}, {"percent", percent}, {"months", months}}); break;
        case kGrant: effects.push_back({{"kind", "company_cash"}, {"amount", kEventGrant[e.amount]}}); break;
        case kQuake: {
            const std::vector<std::string> towns = event_choices(kQuake);
            if (!towns.empty()) {
                effects.push_back({{"kind", "earthquake"},
                                   {"target", towns[static_cast<std::size_t>(e.what) % towns.size()]},
                                   {"percent", 50}});
            }
            break;
        }
        case kEngine: effects.push_back({{"kind", "unlock_engine"}, {"target", key_of(world_.data().locomotives.all(), all)}}); break;
        default: break;
        }
        out.push_back({{"id", "event_" + std::to_string(i + 1)}, {"when", {{"after", date}}}, {"effects", effects}});
    }
    return out;
}

std::vector<Editor::ScenLine> Editor::scen_lines() const {
    std::vector<ScenLine> lines;
    if (scen_page_ == 0) {
        for (ScenRow r : {ScenRow::Name, ScenRow::Briefing, ScenRow::Start, ScenRow::Rivals, ScenRow::Money, ScenRow::Budget,
                          ScenRow::Connected, ScenRow::OneCompany, ScenRow::ChairLocked}) {
            lines.push_back({r});
        }
    } else if (scen_page_ == 2) {
        for (std::size_t i = 0; i < scen_.events.size(); ++i) {
            const int e = static_cast<int>(i);
            const int kind = scen_.events[i].kind;
            lines.push_back({ScenRow::EventKind, e});
            lines.push_back({ScenRow::EventWhen, e});
            if (kind == kNews) lines.push_back({ScenRow::EventText, e});
            if (kind == kCargoPrice || kind == kOutput || kind == kQuake || kind == kEngine) lines.push_back({ScenRow::EventWhat, e});
            if (kind == kCargoPrice || kind == kOutput || kind == kBuildCost || kind == kGrant) lines.push_back({ScenRow::EventAmount, e});
            if (kind == kCargoPrice || kind == kOutput || kind == kBuildCost) lines.push_back({ScenRow::EventFor, e});
        }
        if (scen_.events.size() < kMaxEvents) lines.push_back({ScenRow::AddEvent});
    } else {
        for (int m = 0; m < 3; ++m) {
            lines.push_back({ScenRow::Goal, m});
            lines.push_back({ScenRow::Target, m});
            lines.push_back({ScenRow::Also, m});
            if (scen_.medals[m].also.kind >= 0) lines.push_back({ScenRow::AlsoTarget, m});
            lines.push_back({ScenRow::By, m});
        }
    }
    lines.push_back({ScenRow::Save});
    lines.push_back({ScenRow::Play});
    lines.push_back({ScenRow::Close});
    return lines;
}

void Editor::scen_change(int step) {
    const int towns = static_cast<int>(towns_.size());
    const int terr = territory_count();
    const std::vector<ScenLine> lines = scen_lines();
    const ScenLine at = lines[static_cast<std::size_t>(std::clamp(scen_sel_, 0, static_cast<int>(lines.size()) - 1))];
    ScenarioDraft::Medal& md = scen_.medals[std::clamp(at.medal, 0, 2)];
    ScenarioDraft::Event* ev = at.medal < static_cast<int>(scen_.events.size()) ? &scen_.events[static_cast<std::size_t>(at.medal)] : nullptr;
    const auto next_kind = [&](ScenarioDraft::Goal& g, bool allow_none) {
        const int first = allow_none ? -1 : 0;
        const int span = kGoalKinds - first;
        g.kind = (g.kind - first + step + span) % span + first;
        g.level = std::clamp(g.level, 0, ladder_size(g.kind, towns, terr) - 1);
    };
    switch (at.row) {
    case ScenRow::Start: scen_.start_year = std::clamp(scen_.start_year + step * 5, 1830, 2000); break;
    case ScenRow::Rivals: scen_.rivals = std::clamp(scen_.rivals + step, 0, 5); break;
    case ScenRow::Money: scen_.money = std::clamp(scen_.money + step, 0, 2); break;
    case ScenRow::Budget: scen_.track_budget = std::clamp(scen_.track_budget + step, 0, kBudgetSteps - 1); break;
    case ScenRow::Connected: scen_.connected_track = !scen_.connected_track; break;
    case ScenRow::OneCompany: scen_.one_company = !scen_.one_company; break;
    case ScenRow::ChairLocked: scen_.chair_locked = !scen_.chair_locked; break;
    case ScenRow::Goal: next_kind(md.goal, false); break;
    case ScenRow::Also: next_kind(md.also, true); break;
    case ScenRow::Target: md.goal.level = std::clamp(md.goal.level + step, 0, ladder_size(md.goal.kind, towns, terr) - 1); break;
    case ScenRow::AlsoTarget:
        md.also.level = std::clamp(md.also.level + step, 0, ladder_size(md.also.kind, towns, terr) - 1);
        break;
    case ScenRow::By: md.years = std::clamp(md.years + step, 1, 60); break;
    case ScenRow::EventKind:
        if (ev) {
            ev->kind = (ev->kind + step + kEventKinds) % kEventKinds;
            ev->what = 0;
        }
        break;
    case ScenRow::EventWhen:
        if (ev) {
            const int at_month = std::clamp(ev->year * 12 + ev->month + step, 0, 60 * 12 - 1);
            ev->year = at_month / 12;
            ev->month = at_month % 12;
        }
        break;
    case ScenRow::EventWhat:
        if (ev) {
            const auto n = static_cast<int>(event_choices(ev->kind).size());
            if (n > 0) ev->what = (ev->what + step + n) % n;
        }
        break;
    case ScenRow::EventAmount:
        if (ev) ev->amount = std::clamp(ev->amount + step, 0, 4);
        break;
    case ScenRow::EventFor:
        if (ev) ev->months = std::clamp(ev->months + step, 0, 4);
        break;
    default: break;
    }
}

bool Editor::scen_key(SDL_Keycode key) {
    if (scen_text_ != 0) {
        if (scen_text_ == 3 && scen_text_event_ >= scen_.events.size()) {
            scen_text_ = 0;
            return true;
        }
        std::string& s = scen_text_ == 1 ? scen_.name : scen_text_ == 2 ? scen_.briefing : scen_.events[scen_text_event_].text;
        if (key == SDLK_BACKSPACE && !s.empty()) s.pop_back();
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            if (s.empty()) s = scen_text_ == 1 ? "My scenario" : scen_text_ == 2 ? "Build a railroad to be proud of." : "Big news on the line";
            scen_text_ = 0;
        }
        return true;
    }
    const auto rows = static_cast<int>(scen_lines().size());
    scen_sel_ = std::clamp(scen_sel_, 0, rows - 1);
    switch (key) {
    case SDLK_TAB:
        scen_page_ = (scen_page_ + 1) % 3;
        scen_sel_ = 0;
        return true;
    case SDLK_DELETE:
    case SDLK_BACKSPACE: {
        // Remove the event the chosen row belongs to.
        const ScenLine at = scen_lines()[static_cast<std::size_t>(scen_sel_)];
        if (scen_page_ == 2 && at.row != ScenRow::AddEvent && at.row != ScenRow::Save && at.row != ScenRow::Play &&
            at.row != ScenRow::Close && at.medal < static_cast<int>(scen_.events.size())) {
            scen_.events.erase(scen_.events.begin() + at.medal);
            scen_sel_ = std::min(scen_sel_, static_cast<int>(scen_lines().size()) - 1);
        }
        return true;
    }
    case SDLK_UP: scen_sel_ = (scen_sel_ + rows - 1) % rows; return true;
    case SDLK_DOWN: scen_sel_ = (scen_sel_ + 1) % rows; return true;
    case SDLK_LEFT: scen_change(-1); return true;
    case SDLK_RIGHT: scen_change(+1); return true;
    case SDLK_ESCAPE:
    case SDLK_g: scen_open_ = false; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
        const ScenRow row = scen_lines()[static_cast<std::size_t>(scen_sel_)].row;
        if (row == ScenRow::Name || row == ScenRow::Briefing) {
            scen_text_ = row == ScenRow::Name ? 1 : 2;
            return true;
        }
        if (row == ScenRow::EventText) {
            scen_text_ = 3;
            scen_text_event_ = static_cast<std::size_t>(scen_lines()[static_cast<std::size_t>(scen_sel_)].medal);
            return true;
        }
        if (row == ScenRow::AddEvent) {
            ScenarioDraft::Event e;
            if (!scen_.events.empty()) e.year = std::min(59, scen_.events.back().year + 1);
            scen_.events.push_back(e);
            return true;
        }
        if (row == ScenRow::Save || row == ScenRow::Play) {
            // Connecting towns needs towns to connect, and territories painted.
            for (const ScenarioDraft::Medal& md : scen_.medals) {
                for (const ScenarioDraft::Goal& g : {md.goal, md.also}) {
                    std::string why;
                    if (g.kind == 0 && towns_.size() < 2) {
                        why = "A goal connects towns, but the map has fewer than two: place towns with the Town brush";
                    }
                    if (g.kind == 10 && towns_.empty()) {
                        why = "A goal delivers goods to a town, but the map has none: place one with the Town brush";
                    }
                    if (g.kind == 9 && territory_count() < 2) {
                        why = "A goal counts territories, but the map has fewer than two: paint them with the Territory brush";
                    }
                    if (!why.empty()) {
                        message_ = why;
                        message_until_ = SDL_GetTicks() + 5000;
                        return true;
                    }
                }
            }
            for (const ScenarioDraft::Event& e : scen_.events) {
                if (e.kind == kQuake && towns_.empty()) {
                    message_ = "An earthquake needs a town to strike near: place towns with the Town brush";
                    message_until_ = SDL_GetTicks() + 5000;
                    return true;
                }
            }
            request_ = Request{row == ScenRow::Save ? Request::Kind::SaveScenario : Request::Kind::PlayScenario};
            if (row == ScenRow::Play) scen_open_ = false;
            return true;
        }
        if (row == ScenRow::Close) scen_open_ = false;
        else scen_change(+1);
        return true;
    }
    default: return true;
    }
}

void Editor::draw_scenario(float width, float height) const {
    const float w = 720, h = 640, x0 = (width - w) / 2;
    const float y0 = std::max(40.0f, (height - h) / 2);
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 16, y0 + 10, "Scenario for this map", kScale + 1);
    // The two pages, as tabs.
    const char* pages[] = {"Scenario and rules", "Medals", "Events"};
    float tx = x0 + w - 16;
    for (int p = 2; p >= 0; --p) {
        const float tw = static_cast<float>(text_width(pages[p], kScale)) + 20;
        tx -= tw;
        if (p == scen_page_) ui::selection(tx, y0 + 10, tx + tw - 6, y0 + 34);
        if (p == scen_page_) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.65f, 0.63f, 0.58f);
        draw_text(tx + 7, y0 + 13, pages[p], kScale);
    }
    ui::rule(x0 + 10, x0 + w - 10, y0 + 40);
    float y = y0 + 50;
    const bool blink = (SDL_GetTicks() / 500) % 2 == 0;
    const std::vector<ScenLine> lines = scen_lines();
    const char* tiers[] = {"Bronze", "Silver", "Gold"};
    const auto onoff = [](bool b) { return std::string("<  ") + (b ? "Yes" : "No") + "  >"; };
    // The events page can hold more rows than fit: scroll to the chosen one.
    constexpr int kFit = 18;
    const std::size_t first = scen_page_ == 2 && scen_sel_ >= kFit ? static_cast<std::size_t>(scen_sel_ - kFit + 1) : 0;
    for (std::size_t i = first; i < lines.size(); ++i) {
        const ScenLine& ln = lines[i];
        const ScenarioDraft::Medal& md = scen_.medals[std::clamp(ln.medal, 0, 2)];
        const ScenarioDraft::Event* ev =
            ln.medal < static_cast<int>(scen_.events.size()) ? &scen_.events[static_cast<std::size_t>(ln.medal)] : nullptr;
        std::string label, value;
        std::vector<std::string> more; // further lines of a wrapped value
        switch (ln.row) {
        case ScenRow::Name: label = "Name", value = scen_.name + (scen_text_ == 1 && blink ? "|" : ""); break;
        case ScenRow::Briefing: {
            // The briefing, wrapped over a few lines.
            label = "Briefing";
            std::string b = scen_.briefing + (scen_text_ == 2 && blink ? "|" : "");
            while (!b.empty() && more.size() < 4) {
                std::size_t cut = b.size();
                while (cut > 0 && text_width(b.substr(0, cut), kScale) > 480) {
                    const std::size_t sp = b.rfind(' ', cut - 1);
                    cut = sp == std::string::npos || sp == 0 ? cut - 1 : sp;
                }
                more.push_back(b.substr(0, cut));
                b = cut < b.size() ? b.substr(cut + (b[cut] == ' ' ? 1 : 0)) : "";
            }
            if (!more.empty()) {
                value = more.front();
                more.erase(more.begin());
            }
            break;
        }
        case ScenRow::Start: label = "Starts", value = "<  " + std::to_string(scen_.start_year) + "  >"; break;
        case ScenRow::Rivals: label = "Rival railroads", value = "<  " + std::to_string(scen_.rivals) + "  >"; break;
        case ScenRow::Money: label = "Founding money", value = "<  " + std::string(kMoneyNames[scen_.money]) + "  >"; break;
        case ScenRow::Budget:
            label = "Track budget";
            value = "<  " +
                    (scen_.track_budget == 0 ? std::string("None")
                                             : std::to_string(kBudgetStart[scen_.track_budget]) + " cells, " +
                                                   std::to_string(kBudgetYear[scen_.track_budget]) + " more a year") +
                    "  >";
            break;
        case ScenRow::Connected: label = "Track must join", value = onoff(scen_.connected_track); break;
        case ScenRow::OneCompany: label = "One company only", value = onoff(scen_.one_company); break;
        case ScenRow::ChairLocked: label = "Chairman stays", value = onoff(scen_.chair_locked); break;
        case ScenRow::Goal:
            y += 6;
            label = std::string(tiers[ln.medal]) + " goal";
            value = "<  " + std::string(kGoalNames[md.goal.kind]) + "  >";
            break;
        case ScenRow::Target: label = "    target", value = "<  " + goal_text(md.goal) + "  >"; break;
        case ScenRow::Also:
            label = "    and also";
            value = "<  " + std::string(md.also.kind < 0 ? "Nothing more" : kGoalNames[md.also.kind]) + "  >";
            break;
        case ScenRow::AlsoTarget: label = "    target", value = "<  " + goal_text(md.also) + "  >"; break;
        case ScenRow::By: {
            int years = 0;
            for (int m = 0; m <= ln.medal; ++m) years = std::max(years, scen_.medals[m].years);
            label = "    by", value = "<  the end of " + std::to_string(scen_.start_year + years) + "  >";
            break;
        }
        case ScenRow::EventKind:
            if (i > first) y += 6;
            label = "Event " + std::to_string(ln.medal + 1);
            value = ev ? "<  " + std::string(kEventNames[ev->kind]) + "  >" : "";
            break;
        case ScenRow::EventWhen:
            label = "    when";
            if (ev) value = "<  " + std::string(kMonthNames[ev->month]) + " " + std::to_string(scen_.start_year + ev->year) + "  >";
            break;
        case ScenRow::EventWhat: {
            label = ev && ev->kind == kQuake ? "    near" : "    which";
            const std::vector<std::string> what = ev ? event_choices(ev->kind) : std::vector<std::string>{};
            value = what.empty() ? std::string("(place towns first)")
                                 : "<  " + what[static_cast<std::size_t>(ev->what) % what.size()] + "  >";
            break;
        }
        case ScenRow::EventAmount:
            label = "    by";
            if (ev) value = "<  " + event_amount_text(*ev) + "  >";
            break;
        case ScenRow::EventFor:
            label = "    for";
            if (ev) {
                const std::int32_t m = kEventMonths[ev->months];
                value = "<  " + (m == 0 ? std::string("good") : std::to_string(m) + " months") + "  >";
            }
            break;
        case ScenRow::EventText:
            label = "    headline";
            if (ev) value = ev->text + (scen_text_ == 3 && scen_text_event_ == static_cast<std::size_t>(ln.medal) && blink ? "|" : "");
            break;
        case ScenRow::AddEvent:
            y += 6;
            label = scen_.events.empty() ? "Add an event (none yet)" : "Add an event";
            break;
        case ScenRow::Save:
            y += 6;
            label = "Save the scenario";
            break;
        case ScenRow::Play: label = "Save it and play it"; break;
        case ScenRow::Close: label = "Close"; break;
        }
        const bool sel = scen_sel_ == static_cast<int>(i);
        if (sel) ui::selection(x0 + 8, y - 3, x0 + w - 8, y + 23);
        if (sel) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.9f, 0.88f, 0.82f);
        draw_text(x0 + 20, y, label, kScale);
        draw_text(x0 + 200, y, value, kScale);
        y += 28;
        for (const std::string& m : more) {
            glColor3f(0.85f, 0.83f, 0.77f);
            draw_text(x0 + 200, y - 6, m, kScale);
            y += 20;
        }
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 20, y0 + h - 26,
              scen_text_ ? "Type, then Enter"
                         : scen_page_ == 2 ? "Tab page   Up/Down choose   Left/Right change   Enter types a headline   Delete removes   G/Esc close"
                                           : "Tab page   Up/Down choose   Left/Right change   Enter edits the name or briefing   G or Esc close",
              kScale);
}

} // namespace railmaster::client
