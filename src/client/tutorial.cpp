#include "tutorial.hpp"

#include "font.hpp"
#include "render.hpp"
#include "ui.hpp"

#include <SDL_opengl.h>

#include <cmath>
#include <sstream>
#include <vector>

namespace railmaster::client {

namespace {

constexpr int kScale = 2;

struct Step {
    const char* title;
    const char* text;
};

constexpr Step kSteps[] = {
    {"Look around",
     "Drag with the right mouse button to turn and tilt the view. The mouse wheel, a pinch on the trackpad, or "
     "Option and Up/Down zoom in and out; the arrow keys move the view. Try one."},
    {"Lay some track",
     "Press F2 for the Track tool. Click in the middle of {A} (ringed in gold) to start, then click in {B}: each "
     "click builds track to the cursor and carries on from there. Right-click stops. The price shows by the cursor."},
    {"Build a station",
     "Press F3 for the Station tool, then click on your track in {A}. A station gathers the passengers, "
     "mail and goods of the houses and industries around it."},
    {"A second station",
     "Now click on your track in {B} (still the Station tool), so the line has a station at each end."},
    {"Buy a train",
     "Press F6 for the Train tool, click your two stations in turn, then press Enter to buy the train. It will run "
     "between them and back."},
    {"Start the clock",
     "Every game starts paused. Press Space to start it and + to go faster. Watch your train: when it reaches a "
     "station it is paid for what it carried."},
    {"Read the books",
     "Press L for the Ledger, or F7 for your Company's accounts. Click any train, station or industry in the Look "
     "tool (F1) for its details, and press M for the news."},
    {"Well done",
     "You are running a railroad. Connect more towns, carry coal to the steel mill, buy industries, and watch the "
     "share price. Esc opens the menu for saving. Press Enter to close this."},
};
constexpr int kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

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

} // namespace

void Tutorial::begin(const sim::World& world, Camera& cam) {
    // The two towns nearest each other: the first line joins them.
    const sim::Economy& eco = world.economy();
    std::int64_t best = -1;
    for (std::size_t i = 0; i < eco.towns().size(); ++i) {
        for (std::size_t j = i + 1; j < eco.towns().size(); ++j) {
            const sim::MapPoint a = eco.node_centre(eco.towns()[i].cx, eco.towns()[i].cy);
            const sim::MapPoint b = eco.node_centre(eco.towns()[j].cx, eco.towns()[j].cy);
            const std::int64_t d = sim::distance_mm(a, b);
            if (best >= 0 && d >= best) continue;
            best = d;
            town_a_ = eco.towns()[i].name;
            town_b_ = eco.towns()[j].name;
            at_a_ = a;
            at_b_ = b;
        }
    }
    if (best >= 0) {
        // Both in view, from a little way off.
        cam.center_on({(at_a_.x_mm + at_b_.x_mm) / 2, (at_a_.y_mm + at_b_.y_mm) / 2});
        const float tiles = static_cast<float>(best) / (cam.mm_per_tile > 0 ? cam.mm_per_tile : 805000.0f);
        cam.distance = std::max(12.0f, tiles * 1.1f);
    }
    open_ = true;
    step_ = 0;
    start_distance_ = cam.distance;
    start_x_ = cam.target_x;
    start_y_ = cam.target_y;
    start_yaw_ = cam.yaw;
    step_since_ = SDL_GetTicks();
}

void Tutorial::update(const sim::World& world, const Camera& cam, const Tools& tools) {
    if (!open_ || !world.player_company()) return;
    const sim::CompanyId me = *world.player_company();
    const sim::Railway& rw = world.railway();
    std::size_t stations = 0, trains = 0, edges = 0;
    bool paid = false;
    for (const sim::Station& s : rw.stations()) stations += s.owner == me;
    for (const sim::Train& t : rw.trains()) {
        if (!t.in_service() || t.owner != me) continue;
        ++trains;
        paid |= t.revenue > sim::Money{};
    }
    for (const sim::TrackEdge& e : rw.track().edges()) edges += !e.removed && e.owner == me;
    bool done = false;
    switch (step_) {
    case 0:
        done = std::fabs(cam.distance - start_distance_) > 0.5f || std::fabs(cam.yaw - start_yaw_) > 0.1f ||
               std::hypot(cam.target_x - start_x_, cam.target_y - start_y_) > 2.0f;
        break;
    case 1: done = edges >= 1; break;
    case 2: done = stations >= 1; break;
    case 3: done = stations >= 2; break;
    case 4: done = trains >= 1; break;
    case 5: done = paid; break;
    case 6: done = tools.tool() == Tool::Ledger || tools.tool() == Tool::Finance; break;
    default: break;
    }
    if (done) {
        ++step_;
        step_since_ = SDL_GetTicks();
    }
}

bool Tutorial::key(SDL_Keycode key, Uint16 mod) {
    if (!open_) return false;
    if (step_ == kStepCount - 1 && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
        open_ = false;
        return true;
    }
    if (key == SDLK_TAB && (mod & KMOD_SHIFT) != 0) {
        // Skip a step (or close at the end).
        if (++step_ >= kStepCount) open_ = false;
        step_since_ = SDL_GetTicks();
        return true;
    }
    return false;
}

void Tutorial::draw_world_overlay(const Camera& cam) const {
    if (!open_ || town_a_.empty() || step_ < 1 || step_ > 3) return;
    const float pulse = 10.0f + 3.0f * std::sin(static_cast<float>(SDL_GetTicks()) / 250.0f);
    draw_marker(at_a_, cam, pulse, 1.0f, 0.85f, 0.3f);
    draw_marker(at_b_, cam, pulse, 1.0f, 0.85f, 0.3f);
}

void Tutorial::draw(float width, float /*height*/) const {
    if (!open_) return;
    const Step& s = kSteps[std::min(step_, kStepCount - 1)];
    // The step's text, with the first line's towns named.
    std::string body = s.text;
    if (!town_a_.empty()) {
        const auto put = [&](const std::string& from, const std::string& to) {
            for (std::size_t at = body.find(from); at != std::string::npos; at = body.find(from, at + to.size())) body.replace(at, from.size(), to);
        };
        put("{A}", town_a_);
        put("{B}", town_b_);
    }
    const float w = 430, x0 = width - w - 16, y0 = 60;
    const auto lines = wrap(body, kScale, w - 32);
    const float h = 70 + 19.0f * static_cast<float>(lines.size()) + 30;
    ui::panel(x0, y0, x0 + w, y0 + h);
    // A brief glow when a step is newly reached.
    const float fresh = std::max(0.0f, 1.0f - static_cast<float>(SDL_GetTicks() - step_since_) / 900.0f);
    glColor3f(0.65f + 0.35f * fresh, 0.6f + 0.35f * fresh, 0.5f);
    const std::string count = "Tutorial  " + std::to_string(std::min(step_ + 1, kStepCount)) + " of " + std::to_string(kStepCount);
    draw_text(x0 + 16, y0 + 10, count, kScale);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 16, y0 + 30, s.title, kScale + 1);
    float y = y0 + 64;
    glColor3f(0.92f, 0.9f, 0.84f);
    for (const std::string& l : lines) {
        draw_text(x0 + 16, y, l, kScale);
        y += 19;
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 16, y0 + h - 24, step_ == kStepCount - 1 ? "Enter closes" : "Shift+Tab skips this step", kScale);
}

} // namespace railmaster::client
