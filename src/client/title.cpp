#include "title.hpp"

#include "font.hpp"
#include "ui.hpp"

#include "railmaster/sim/scenario.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace railmaster::client {

namespace {

constexpr int kScale = 2;
constexpr const char* kSizes[] = {"Small", "Medium", "Large"};
constexpr int kTerritories[] = {0, 4, 8};
constexpr const char* kDifficulties[] = {"Easy", "Medium", "Hard", "Expert"};
constexpr int kMaxRivals = 5;

void fill(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

std::vector<std::string> wrap(const std::string& text, int scale, float width) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        const std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && static_cast<float>(text_width(longer, scale)) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = longer;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

Title::Title(std::string data_dir, std::string maps_dir, std::string scenarios_dir,
             std::vector<std::pair<std::string, std::string>> slots)
    : scenarios_dir_(std::move(scenarios_dir)), maps_dir_(std::move(maps_dir)), data_dir_(std::move(data_dir)), slots_(std::move(slots)) {
    scan_scenarios();
    scan_maps();
    seed_ = SDL_GetPerformanceCounter() % 9000 + 1000;
}

void Title::scan_maps() {
    // Hand-made landscapes: the images in data/maps and the player's own
    // maps folder, named after their files.
    maps_.clear();
    std::error_code ec;
    for (const std::string& dir : {data_dir_ + "/maps", maps_dir_}) {
        if (dir.empty()) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".png") continue;
            std::string name = entry.path().stem().string();
            std::replace(name.begin(), name.end(), '_', ' ');
            if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            maps_.emplace_back(entry.path().string(), name);
        }
    }
    std::sort(maps_.begin(), maps_.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    landscape_ = std::min(landscape_, static_cast<int>(maps_.size()));
    editor_from_ = std::min(editor_from_, static_cast<int>(maps_.size()) + 1);
}

void Title::scan_scenarios() {
    // The scenarios shipped in data/scenarios and the player's own, in order of their start.
    scenarios_.clear();
    std::error_code ec;
    for (const std::string& dir : {data_dir_ + "/scenarios", scenarios_dir_}) {
        if (dir.empty()) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".json") continue;
            try {
                const sim::Scenario s = sim::Scenario::from_json(read_file(entry.path().string()));
                scenarios_.push_back({entry.path().string(), s.name, s.briefing, s.start.year()});
            } catch (const std::exception&) {
                // A broken file is left off the list; the game says why when started from the command line.
            }
        }
    }
    std::sort(scenarios_.begin(), scenarios_.end(),
              [](const ScenarioInfo& a, const ScenarioInfo& b) { return a.year != b.year ? a.year < b.year : a.name < b.name; });
    sel_ = page_ == 2 ? 0 : sel_;
}

void Title::show() {
    scan_maps();
    scan_scenarios();
    open_ = true;
    page_ = 0;
    sel_ = 0;
}

int Title::items() const {
    switch (page_) {
    case 0: return 6;
    case 4: return 3; // map size, start from, open
    case 1: return 7; // map size, rivals, territories, difficulty, landscape, map number, start
    case 2: return static_cast<int>(scenarios_.size());
    default: return static_cast<int>(slots_.size());
    }
}

void Title::change(int step) {
    if (page_ == 4) {
        if (sel_ == 0) map_size_ = (map_size_ + step + 3) % 3;
        if (sel_ == 1) {
            const int n = static_cast<int>(maps_.size()) + 2;
            editor_from_ = (editor_from_ + step + n) % n;
        }
        return;
    }
    switch (sel_) {
    case 0: map_size_ = (map_size_ + step + 3) % 3; break;
    case 1: rivals_ = std::clamp(rivals_ + step, 0, kMaxRivals); break;
    case 2: territories_ = (territories_ + step + 3) % 3; break;
    case 3: difficulty_ = (difficulty_ + step + 4) % 4; break;
    case 4: {
        const int n = static_cast<int>(maps_.size()) + 1;
        landscape_ = (landscape_ + step + n) % n;
        break;
    }
    case 5: seed_ = seed_ + static_cast<std::uint64_t>(step + 1) * 7919 % 9000 + 1; break;
    default: break;
    }
}

void Title::choose() {
    switch (page_) {
    case 0:
        if (sel_ == 4) {
            request_ = Request{Request::Kind::Tutorial, {}, {}};
            return;
        }
        if (sel_ == 5) {
            request_ = Request{Request::Kind::Quit, {}, {}};
            return;
        }
        page_ = sel_ + 1;
        sel_ = page_ == 1 ? 6 : page_ == 4 ? 2 : 0;
        return;
    case 4: {
        if (sel_ != 2) {
            change(+1);
            return;
        }
        Request r{Request::Kind::Editor, {}, {}};
        r.config.seed = seed_;
        r.config.set_map_size(static_cast<sim::MapSize>(map_size_));
        r.flat = editor_from_ == 0;
        if (editor_from_ >= 2) r.path = maps_[static_cast<std::size_t>(editor_from_ - 2)].first;
        request_ = r;
        return;
    }
    case 1: {
        if (sel_ != 6) {
            change(+1);
            return;
        }
        Request r{Request::Kind::NewGame, {}, {}};
        r.config.seed = seed_;
        r.config.set_map_size(static_cast<sim::MapSize>(map_size_));
        r.config.rivals = rivals_;
        r.config.territories = kTerritories[territories_];
        r.config.difficulty = static_cast<sim::Difficulty>(difficulty_);
        r.config.found_player_company = false; // the founding dialog sets the terms
        if (landscape_ > 0) r.path = maps_[static_cast<std::size_t>(landscape_ - 1)].first;
        request_ = r;
        return;
    }
    case 2:
        if (!scenarios_.empty()) request_ = Request{Request::Kind::Scenario, {}, scenarios_[static_cast<std::size_t>(sel_)].path};
        return;
    default:
        if (slots_.empty() || slots_[static_cast<std::size_t>(sel_)].second == "Empty") return;
        request_ = Request{Request::Kind::Load, {}, slots_[static_cast<std::size_t>(sel_)].first};
        return;
    }
}

void Title::key(SDL_Keycode key) {
    const int n = std::max(1, items());
    switch (key) {
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
        if (page_ != 0) {
            sel_ = page_ - 1;
            page_ = 0;
        }
        return;
    case SDLK_UP: sel_ = (sel_ + n - 1) % n; return;
    case SDLK_DOWN: sel_ = (sel_ + 1) % n; return;
    case SDLK_LEFT:
        if (page_ == 1 || page_ == 4) change(-1);
        return;
    case SDLK_RIGHT:
        if (page_ == 1 || page_ == 4) change(+1);
        return;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: choose(); return;
    default: return;
    }
}

void Title::click(float sx, float sy) {
    for (std::size_t i = 0; i < boxes_.size(); ++i) {
        const Box& b = boxes_[i];
        if (sx >= b.x0 && sx < b.x1 && sy >= b.y0 && sy < b.y1) {
            sel_ = static_cast<int>(i);
            choose();
            return;
        }
    }
}

void Title::draw(float width, float height, const std::string& build) const {
    boxes_.clear();
    // Darken the running game towards the left, where the menu stands.
    glBegin(GL_QUADS);
    glColor4f(0.04f, 0.03f, 0.02f, 0.82f);
    glVertex2f(0, 0);
    glColor4f(0.04f, 0.03f, 0.02f, 0.05f);
    glVertex2f(width * 0.75f, 0);
    glVertex2f(width * 0.75f, height);
    glColor4f(0.04f, 0.03f, 0.02f, 0.82f);
    glVertex2f(0, height);
    glEnd();
    fill(0, height - 46, width, height, 0.04f, 0.03f, 0.02f, 0.6f);

    // The name, in brass, with a rule under it.
    const float x0 = 70;
    float y = std::max(40.0f, height * 0.12f);
    glColor4f(0, 0, 0, 0.6f);
    draw_text(x0 + 3, y + 3, "Railmaster", kScale + 5);
    glColor3f(1.0f, 0.84f, 0.48f);
    draw_text(x0, y, "Railmaster", kScale + 5);
    y += 74;
    fill(x0, y, x0 + 380, y + 2, 0.85f, 0.68f, 0.35f, 1.0f);
    y += 10;
    glColor3f(0.85f, 0.8f, 0.7f);
    draw_text(x0, y, "Build a railroad empire, 1830 to the present", kScale);
    y += 54;

    const auto item = [&](const std::string& text, bool sel, float w, int scale) {
        const float h = scale > kScale ? 36.0f : 26.0f;
        if (sel) ui::selection(x0 - 12, y - 4, x0 + w, y + h - 6);
        boxes_.push_back({x0 - 12, y - 4, x0 + w, y + h - 6});
        if (sel) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.92f, 0.9f, 0.84f);
        draw_text(x0, y, text, scale);
        y += h;
    };
    const auto heading = [&](const std::string& text) {
        glColor3f(1.0f, 0.84f, 0.48f);
        draw_text(x0, y, text, kScale + 1);
        y += 40;
    };
    std::string foot;
    switch (page_) {
    case 0: {
        const char* names[] = {"New railroad", "Scenarios", "Load a saved game", "Map editor", "Tutorial", "Quit"};
        for (int i = 0; i < 6; ++i) item(names[i], i == sel_, 380, kScale + 1);
        foot = "Up/Down and Enter, or click";
        break;
    }
    case 1: {
        heading("New railroad");
        const std::string values[] = {kSizes[map_size_], rivals_ == 0 ? std::string("None") : std::to_string(rivals_),
                                      territories_ == 0 ? std::string("None") : std::to_string(kTerritories[territories_]),
                                      kDifficulties[difficulty_],
                                      landscape_ == 0 ? std::string("Generated") : maps_[static_cast<std::size_t>(landscape_ - 1)].second,
                                      "#" + std::to_string(seed_)};
        const char* labels[] = {"Map size", "Rival railroads", "Territories", "Difficulty", "Landscape", "Towns and industries"};
        for (int i = 0; i < 6; ++i) {
            const float row = y;
            item(labels[i], i == sel_, 460, kScale);
            glColor3f(i == sel_ ? 1.0f : 0.85f, i == sel_ ? 0.9f : 0.8f, i == sel_ ? 0.6f : 0.7f);
            const std::string v = "<  " + values[i] + "  >";
            draw_text(x0 + 440 - static_cast<float>(text_width(v, kScale)), row, v, kScale);
        }
        y += 10;
        item("Start", sel_ == 6, 460, kScale + 1);
        foot = "Left/Right change   Enter starts   Esc back";
        break;
    }
    case 2: {
        heading("Scenarios");
        for (std::size_t i = 0; i < scenarios_.size(); ++i) {
            item(std::to_string(scenarios_[i].year) + "   " + scenarios_[i].name, static_cast<int>(i) == sel_, 520, kScale);
        }
        if (scenarios_.empty()) {
            glColor3f(0.6f, 0.58f, 0.54f);
            draw_text(x0, y, "No scenarios found in " + data_dir_ + "/scenarios", kScale);
            y += 26;
        } else {
            y += 14;
            glColor3f(0.82f, 0.78f, 0.7f);
            for (const std::string& l : wrap(scenarios_[static_cast<std::size_t>(sel_)].briefing, kScale, 520)) {
                if (y > height - 80) break;
                draw_text(x0, y, l, kScale);
                y += 19;
            }
        }
        foot = "Up/Down choose   Enter plays   Esc back";
        break;
    }
    case 4: {
        heading("Map editor");
        const std::string from = editor_from_ == 0   ? std::string("Flat land")
                                 : editor_from_ == 1 ? std::string("Generated hills")
                                                     : maps_[static_cast<std::size_t>(editor_from_ - 2)].second;
        const std::string values[] = {kSizes[map_size_], from};
        const char* labels[] = {"Map size", "Start from"};
        for (int i = 0; i < 2; ++i) {
            const float row = y;
            item(labels[i], i == sel_, 460, kScale);
            glColor3f(i == sel_ ? 1.0f : 0.85f, i == sel_ ? 0.9f : 0.8f, i == sel_ ? 0.6f : 0.7f);
            const std::string v = "<  " + values[i] + "  >";
            draw_text(x0 + 440 - static_cast<float>(text_width(v, kScale)), row, v, kScale);
        }
        y += 10;
        item("Open the editor", sel_ == 2, 460, kScale + 1);
        y += 14;
        glColor3f(0.82f, 0.78f, 0.7f);
        for (const std::string& l : wrap("Shape the land with brushes, paint water, then save the map to play on later, or play "
                                         "a new railroad on it at once.",
                                         kScale, 460)) {
            draw_text(x0, y, l, kScale);
            y += 19;
        }
        foot = "Left/Right change   Enter opens   Esc back";
        break;
    }
    default: {
        heading("Load a saved game");
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const float row = y;
            const std::string name = i == 0 ? std::string("Quick save")
                                     : i + 1 == slots_.size() ? std::string("Autosave")
                                                             : "Game " + std::to_string(i);
            item(name, static_cast<int>(i) == sel_, 620, kScale);
            if (slots_[i].second == "Empty") glColor3f(0.5f, 0.48f, 0.45f);
            else glColor3f(0.85f, 0.82f, 0.75f);
            draw_text(x0 + 140, row, slots_[i].second, kScale);
        }
        foot = "Up/Down choose   Enter loads   Esc back";
        break;
    }
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0, height - 32, foot, kScale);
    draw_text(width - static_cast<float>(text_width(build, kScale)) - 16, height - 32, build, kScale);
}

} // namespace railmaster::client
