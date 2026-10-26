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
     "Drag with the right mouse button to turn and tilt the view. The mouse wheel, a two-finger scroll, "
     "Cmd and = or -, or the + and - by the minimap zoom in and out; the arrow keys move the view. Try one."},
    {"Lay some track",
     "Press F2 for the Track tool. Click in the middle of {A} (ringed in gold) to start, then click in {B}: each "
     "click builds track to the cursor and carries on from there. Right-click stops. The price shows by the cursor."},
    {"Build a station",
     "Press F3 for the Station tool, then click on your track in {A}. A station gathers the passengers, "
     "mail and goods of the houses and industries around it."},
    {"A second station",
     "Now click on your track in {B} (still the Station tool), so the line has a station at each end."},
    {"Water for the engines",
     "Steam engines need water and sand, or they crawl. Press F4 for the Service tower tool and click on your track "
     "by one of the stations: trains passing it fill up."},
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
     "You are running a railroad. Ctrl+G opens the Guide: who wants each cargo, and the best-paying links near you "
     "(Enter goes there). Connect more towns, carry freight where it sells for more, buy industries, and watch the "
     "share price. ? shows the keys; Esc opens the menu for saving. Press Enter to close this."},
};
constexpr int kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// A free game's first steps open with what it is for, then the tutorial's
// steps from the track on.
constexpr Step kAim = {
    "Your railroad",
    "You run {CO}, with {CASH} to spend. A free game has no deadline: your aim is a railroad that makes money, "
    "growing its profits, its worth and its share price (F10 shows how you stand; Scenarios on the title screen set "
    "goals and medals). A good first line joins {A} and {B}, ringed in gold. Press Enter to begin, or just start "
    "building."};
constexpr int kFirstGameSkip = 1; // "Look around"

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

void Tutorial::begin(const sim::World& world, Camera& cam, Mode mode) {
    mode_ = mode;
    company_.clear();
    cash_.clear();
    if (world.player_company()) {
        const sim::Company& co = world.company(*world.player_company());
        company_ = co.name();
        cash_ = format_money(co.cash());
    }
    // A first line that pays: towns 5 to 16 miles apart, the smaller as big
    // as can be (Economy::first_line).
    const sim::Economy& eco = world.economy();
    constexpr std::int64_t kMile = 1'609'344;
    std::int64_t best = -1;
    if (const auto pair = eco.first_line(5 * kMile, 16 * kMile)) {
        // The bigger town first: the line starts there.
        const auto [i, j] = eco.town_houses(pair->first) >= eco.town_houses(pair->second) ? *pair : std::pair{pair->second, pair->first};
        town_a_ = eco.towns()[i].name;
        town_b_ = eco.towns()[j].name;
        at_a_ = eco.node_centre(eco.towns()[i].cx, eco.towns()[i].cy);
        at_b_ = eco.node_centre(eco.towns()[j].cx, eco.towns()[j].cy);
        best = sim::distance_mm(at_a_, at_b_);
    }
    span_mm_ = std::max<std::int64_t>(0, best);
    if (best >= 0) frame(cam, false);
    open_ = true;
    // A free game's step 0 is its opening; the tutorial's own steps follow
    // from the track, so "Look around" is passed over.
    step_ = 0;
    start_distance_ = cam.distance;
    start_x_ = cam.target_x;
    start_y_ = cam.target_y;
    start_yaw_ = cam.yaw;
    step_since_ = SDL_GetTicks();
}

void Tutorial::frame(Camera& cam, bool glide) const {
    // From far enough off that the nearer town is not at the screen's foot
    // (the view looks down at a slant), looking a little beyond the middle.
    const sim::MapPoint mid{(at_a_.x_mm + at_b_.x_mm) / 2, (at_a_.y_mm + at_b_.y_mm) / 2};
    if (glide) cam.glide_to(mid);
    else cam.center_on(mid);
    const float tiles = static_cast<float>(span_mm_) / (cam.mm_per_tile > 0 ? cam.mm_per_tile : 805000.0f);
    cam.distance = std::max(14.0f, tiles * 1.7f);
}

void Tutorial::update(const sim::World& world, Camera& cam, const Tools& tools) {
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
    std::size_t towers = 0;
    for (const sim::ServiceBuilding& sb : rw.service_buildings()) towers += sb.owner == me && sb.type == sim::ServiceType::ServiceTower;
    bool done = false;
    if (mode_ == Mode::FirstGame && step_ == 0) {
        // The opening passes on Enter, or once building has begun.
        done = edges >= 1;
        if (done) {
            step_ = kFirstGameSkip;
            step_since_ = SDL_GetTicks();
        }
        return;
    }
    switch (step_) {
    case 0:
        done = std::fabs(cam.distance - start_distance_) > 0.5f || std::fabs(cam.yaw - start_yaw_) > 0.1f ||
               std::hypot(cam.target_x - start_x_, cam.target_y - start_y_) > 2.0f;
        break;
    case 1: done = edges >= 1; break;
    case 2: done = stations >= 1; break;
    case 3: done = stations >= 2; break;
    case 4: done = towers >= 1; break;
    case 5: done = trains >= 1; break;
    case 6: done = paid; break;
    case 7: done = tools.tool() == Tool::Ledger || tools.tool() == Tool::Finance; break;
    default: break;
    }
    if (done) {
        ++step_;
        step_since_ = SDL_GetTicks();
        if (step_ == 1 && !town_a_.empty()) frame(cam, true); // the towns back in view for the track
    }
}

bool Tutorial::key(SDL_Keycode key, Uint16 mod) {
    if (!open_) return false;
    if (mode_ == Mode::FirstGame && step_ == 0 && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
        step_ = kFirstGameSkip;
        step_since_ = SDL_GetTicks();
        return true;
    }
    if (step_ == kStepCount - 1 && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
        open_ = false;
        return true;
    }
    if (key == SDLK_TAB && (mod & KMOD_SHIFT) != 0) {
        // Skip a step (or close at the end).
        step_ = mode_ == Mode::FirstGame && step_ == 0 ? kFirstGameSkip : step_ + 1;
        if (step_ >= kStepCount) open_ = false;
        step_since_ = SDL_GetTicks();
        return true;
    }
    return false;
}

void Tutorial::draw_rings(const Camera& cam) const {
    if (!open_ || town_a_.empty() || (step_ < 1 && mode_ != Mode::FirstGame) || step_ > 3) return;
    const float pulse = std::sin(static_cast<float>(SDL_GetTicks()) / 250.0f);
    const auto ring = [&](sim::MapPoint p, const std::string& label) {
        float sx, sy;
        if (!cam.to_screen(p, sx, sy)) return;
        const float r = 40.0f + 4.0f * pulse;
        glLineWidth(3.0f);
        glColor4f(1.0f, 0.82f, 0.3f, 0.95f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 48; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 48.0f;
            glVertex2f(sx + r * std::cos(a), sy + r * 0.6f * std::sin(a));
        }
        glEnd();
        glLineWidth(1.0f);
        // The label under the ring, on a dark backing.
        const auto tw = static_cast<float>(text_width(label, kScale));
        const float lx = sx - tw / 2, ly = sy + r * 0.6f + 6;
        ui::panel(lx - 8, ly - 4, lx + tw + 8, ly + 20);
        glColor3f(1.0f, 0.86f, 0.5f);
        draw_text(lx, ly, label, kScale);
    };
    ring(at_a_, "Start here: " + town_a_);
    ring(at_b_, "Then: " + town_b_);
}

bool Tutorial::click(float x, float y) {
    if (!open_ || mode_ != Mode::FirstGame) return false;
    if (x < hide_box_[0] || x > hide_box_[2] || y < hide_box_[1] || y > hide_box_[3]) return false;
    open_ = false;
    return true;
}

void Tutorial::draw(float width, float /*height*/) const {
    if (!open_) return;
    const bool first_game = mode_ == Mode::FirstGame;
    const Step& s = first_game && step_ == 0 ? kAim : kSteps[std::min(step_, kStepCount - 1)];
    // The step's text, with the first line's towns (and the company) named.
    std::string body = s.text;
    const auto put = [&](const std::string& from, const std::string& to) {
        for (std::size_t at = body.find(from); at != std::string::npos; at = body.find(from, at + to.size())) body.replace(at, from.size(), to);
    };
    if (!town_a_.empty()) {
        put("{A}", town_a_);
        put("{B}", town_b_);
    }
    put("{CO}", company_.empty() ? std::string("your railroad") : company_);
    put("{CASH}", cash_.empty() ? std::string("money") : cash_);
    const float w = 430, x0 = width - w - 16, y0 = 60;
    const auto lines = wrap(body, kScale, w - 32);
    const float h = 70 + 19.0f * static_cast<float>(lines.size()) + 30;
    ui::panel(x0, y0, x0 + w, y0 + h);
    // A brief glow when a step is newly reached.
    const float fresh = std::max(0.0f, 1.0f - static_cast<float>(SDL_GetTicks() - step_since_) / 900.0f);
    glColor3f(0.65f + 0.35f * fresh, 0.6f + 0.35f * fresh, 0.5f);
    // Counted from the opening in a free game (which passes over "Look around").
    const int shown = first_game ? (step_ == 0 ? 1 : step_ - kFirstGameSkip + 2) : std::min(step_ + 1, kStepCount);
    const int total = first_game ? kStepCount - kFirstGameSkip + 1 : kStepCount;
    const std::string count = (first_game ? "First steps  " : "Tutorial  ") + std::to_string(shown) + " of " + std::to_string(total);
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
    const char* foot = step_ == kStepCount - 1 ? "Enter closes" : first_game && step_ == 0 ? "Enter begins" : "Shift+Tab skips this step";
    draw_text(x0 + 16, y0 + h - 24, foot, kScale);
    if (first_game) {
        // A way to put the tips away for good.
        const std::string hide = "Hide these tips";
        const float tw = static_cast<float>(text_width(hide, kScale)), hx = x0 + w - 16 - tw, hy = y0 + h - 24;
        glColor3f(0.85f, 0.75f, 0.5f);
        draw_text(hx, hy, hide, kScale);
        hide_box_[0] = hx - 4;
        hide_box_[1] = hy - 4;
        hide_box_[2] = hx + tw + 4;
        hide_box_[3] = hy + 20;
    }
}

} // namespace railmaster::client
