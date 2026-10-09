#include "tools.hpp"

#include "font.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cstdlib>

namespace railmaster::client {

namespace {

constexpr int kScale = 2;
constexpr float kTopBarH = 22.0f;
constexpr float kToolbarY = 26.0f;
constexpr float kButtonH = 22.0f;
constexpr float kBottomBarH = 44.0f;

const char* size_name(sim::StationSize s) {
    switch (s) {
    case sim::StationSize::Small: return "SMALL";
    case sim::StationSize::Medium: return "MEDIUM";
    case sim::StationSize::Large: return "LARGE";
    }
    return "?";
}

const char* state_name(sim::TrainState s) {
    switch (s) {
    case sim::TrainState::Dwelling: return "AT STATION";
    case sim::TrainState::Moving: return "MOVING";
    case sim::TrainState::Servicing: return "SERVICING";
    case sim::TrainState::BrokenDown: return "BROKEN DOWN";
    case sim::TrainState::NoRoute: return "NO ROUTE";
    }
    return "?";
}

std::string percent(std::int32_t gauge) { return std::to_string(gauge * 100 / sim::kGaugeFull) + "%"; }

void fill_rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

} // namespace

std::string format_money(sim::Money m) {
    std::int64_t d = m.whole_dollars();
    const bool negative = d < 0;
    std::string digits = std::to_string(std::llabs(d));
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (negative ? "-$" : "$") + out;
}

void Tools::select(Tool t) {
    cancel();
    tool_ = t;
}

void Tools::cancel() {
    track_start_.reset();
    route_.clear();
}

void Tools::show(std::string message, bool good) {
    message_ = std::move(message);
    message_good_ = good;
    message_until_ = SDL_GetTicks() + 4000;
}

void Tools::on_mouse_move(float sx, float sy) {
    hover_sx_ = sx;
    hover_sy_ = sy;
    hover_ = cam_.to_map(sx, sy);
    hover_pick_ = sim::pick_track_end(world_.railway().track(), hover_, snap_mm());
}

std::optional<sim::BuildTrack> Tools::pending_track() const {
    if (!track_start_) return std::nullopt;
    sim::BuildTrack cmd;
    cmd.start = *track_start_;
    cmd.end = hover_pick_;
    cmd.double_track = double_track_;
    cmd.tunnel_preference = tunnel_preference_;
    if (curves_ && cmd.start.kind == sim::TrackEnd::Kind::Node) {
        cmd.curve_control = sim::continuing_control_point(world_.railway().track(), cmd.start.node, cmd.end.pos);
    }
    return cmd;
}

std::optional<sim::StationId> Tools::station_near(sim::MapPoint p) const {
    std::optional<sim::StationId> best;
    std::int64_t best_d = snap_mm() + 1;
    for (const sim::Station& s : world_.railway().stations()) {
        const std::int64_t d = sim::distance_mm(p, world_.railway().track().node(s.node).pos);
        if (d < best_d) {
            best = s.id;
            best_d = d;
        }
    }
    return best;
}

std::vector<sim::LocoTypeId> Tools::available_locos() const {
    std::vector<sim::LocoTypeId> out;
    for (const auto& l : world_.data().locomotives.all()) {
        if (l.available_in(world_.date().year())) out.push_back(l.id);
    }
    return out;
}

void Tools::on_click(float sx, float sy, bool right_button) {
    if (right_button) {
        cancel();
        return;
    }
    for (const Button& b : layout_buttons()) {
        if (sx >= b.x0 && sx < b.x1 && sy >= b.y0 && sy < b.y1) {
            select(b.tool);
            return;
        }
    }
    if (sy < kToolbarY + kButtonH || sy > static_cast<float>(cam_.height_px) - kBottomBarH) return;

    on_mouse_move(sx, sy);
    switch (tool_) {
    case Tool::Inspect: return;
    case Tool::Track: {
        if (!track_start_) {
            track_start_ = hover_pick_;
            return;
        }
        const sim::CommandResult r = world_.execute(*pending_track());
        if (!r.ok) {
            show("CANNOT BUILD: " + r.error, false);
            return;
        }
        show("BUILT TRACK FOR " + format_money(r.cost), true);
        // Carry on from where this run ended, as when dragging track in RT3.
        track_start_ = sim::TrackEnd{sim::TrackEnd::Kind::Node, r.created_id, 0,
                                     world_.railway().track().node(r.created_id).pos};
        on_mouse_move(sx, sy);
        return;
    }
    case Tool::Station: {
        const sim::CommandResult r = world_.execute(sim::BuildStation{.at = hover_pick_, .size = station_size_});
        if (r.ok) show("BUILT " + world_.railway().station(r.created_id).name + " FOR " + format_money(r.cost), true);
        else show("CANNOT BUILD: " + r.error, false);
        return;
    }
    case Tool::ServiceTower:
    case Tool::Maintenance: {
        const auto type = tool_ == Tool::ServiceTower ? sim::ServiceType::ServiceTower
                                                      : sim::ServiceType::MaintenanceFacility;
        const sim::CommandResult r = world_.execute(sim::BuildServiceBuilding{.at = hover_pick_, .type = type});
        if (r.ok) show("BUILT FOR " + format_money(r.cost), true);
        else show("CANNOT BUILD: " + r.error, false);
        return;
    }
    case Tool::Train: {
        const auto s = station_near(hover_);
        if (!s) {
            show("CLICK A STATION TO ADD IT TO THE ROUTE", false);
            return;
        }
        if (route_.empty() || route_.back() != *s) route_.push_back(*s);
        return;
    }
    }
}

bool Tools::on_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_F1: select(Tool::Inspect); return true;
    case SDLK_F2: select(Tool::Track); return true;
    case SDLK_F3: select(Tool::Station); return true;
    case SDLK_F4: select(Tool::ServiceTower); return true;
    case SDLK_F5: select(Tool::Maintenance); return true;
    case SDLK_F6: select(Tool::Train); return true;
    case SDLK_o: cycle_overlay(+1); return true;
    case SDLK_p: cycle_overlay(-1); return true;
    case SDLK_ESCAPE:
        if (track_start_ || !route_.empty()) cancel();
        else select(Tool::Inspect);
        return true;
    default: break;
    }

    switch (tool_) {
    case Tool::Track:
        if (key == SDLK_d) double_track_ = !double_track_;
        else if (key == SDLK_c) curves_ = !curves_;
        else if (key == SDLK_LEFTBRACKET) tunnel_preference_ = std::max(0, tunnel_preference_ - 10);
        else if (key == SDLK_RIGHTBRACKET) tunnel_preference_ = std::min(100, tunnel_preference_ + 10);
        else return false;
        return true;
    case Tool::Station:
        if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
            const int step = key == SDLK_LEFTBRACKET ? 2 : 1; // cycle backwards or forwards
            station_size_ = static_cast<sim::StationSize>((static_cast<int>(station_size_) + step) % 3);
            return true;
        }
        return false;
    case Tool::Train: {
        const auto locos = available_locos();
        if (key == SDLK_LEFTBRACKET && cars_ > 0) --cars_;
        else if (key == SDLK_RIGHTBRACKET && cars_ < sim::kMaxCarsPerTrain) ++cars_;
        else if (key == SDLK_l && !locos.empty()) loco_choice_ = (loco_choice_ + 1) % locos.size();
        else if (key == SDLK_BACKSPACE && !route_.empty()) route_.pop_back();
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (locos.empty()) {
                show("NO LOCOMOTIVES AVAILABLE THIS YEAR", false);
                return true;
            }
            const sim::LocoTypeId loco = locos[loco_choice_ % locos.size()];
            const sim::CommandResult r =
                world_.execute(sim::BuyTrain{.loco = loco, .cars = cars_, .route = route_});
            if (r.ok) {
                show("BOUGHT " + world_.data().locomotives.get(loco).name + " FOR " + format_money(r.cost), true);
                route_.clear();
            } else {
                show("CANNOT BUY: " + r.error, false);
            }
        } else {
            return false;
        }
        return true;
    }
    default: return false;
    }
}

void Tools::cycle_overlay(int direction) {
    // Off, then each cargo the economy is trading this year, then off again.
    std::vector<sim::CargoId> choices;
    for (const auto& c : world_.data().cargo.all()) {
        if (world_.economy().active(c.id)) choices.push_back(c.id);
    }
    if (choices.empty()) {
        overlay_.reset();
        return;
    }
    const auto n = static_cast<int>(choices.size());
    int at = n; // "off" sits between the last and the first
    if (overlay_) {
        const auto it = std::find(choices.begin(), choices.end(), *overlay_);
        if (it != choices.end()) at = static_cast<int>(it - choices.begin());
    }
    at = ((at + direction) % (n + 1) + (n + 1)) % (n + 1);
    if (at == n) overlay_.reset();
    else overlay_ = choices[static_cast<std::size_t>(at)];
}

std::string Tools::cell_text() const {
    const sim::Economy& eco = world_.economy();
    const std::int32_t cx = eco.cell_x(hover_), cy = eco.cell_y(hover_);
    std::string s;
    for (const sim::Site& site : eco.sites()) {
        if (site.cx != cx || site.cy != cy) continue;
        const sim::IndustryType& t = world_.data().industries.get(site.type);
        if (!s.empty()) s += ", ";
        s += t.kind == sim::IndustryKind::House ? std::to_string(site.level) + " HOUSES" : t.name;
        if (!t.outputs.empty() && t.kind != sim::IndustryKind::House) {
            s += " (MAKES";
            for (sim::CargoId o : t.outputs) s += " " + world_.data().cargo.get(o).name;
            s += ")";
        }
    }
    if (overlay_) {
        const sim::CargoType& c = world_.data().cargo.get(*overlay_);
        if (!s.empty()) s += ".  ";
        s += c.name + " " + format_money(sim::Money::dollars(eco.price(c.id, cx, cy))) + " A LOAD, " +
             std::to_string(eco.stock_milli(c.id, cx, cy) / sim::kMilli) + " WAITING";
    }
    return s;
}

void Tools::draw_world_overlay() const {
    const sim::TrackNetwork& net = world_.railway().track();
    switch (tool_) {
    case Tool::Track:
        if (const auto cmd = pending_track()) {
            const sim::PlanResult plan = world_.preview(*cmd);
            if (plan.plan) draw_plan(cmd->start.pos, *plan.plan, cam_);
            else draw_line(cmd->start.pos, cmd->end.pos, cam_, 0.9f, 0.1f, 0.1f, 0.8f);
            draw_marker(cmd->start.pos, cam_, 4, 1.0f, 1.0f, 0.3f);
        }
        [[fallthrough]];
    case Tool::Station:
    case Tool::ServiceTower:
    case Tool::Maintenance: {
        // Show what a click would attach to: a node, a point on track, or open ground.
        switch (hover_pick_.kind) {
        case sim::TrackEnd::Kind::Node: draw_marker(hover_pick_.pos, cam_, 4, 1.0f, 1.0f, 1.0f); break;
        case sim::TrackEnd::Kind::OnEdge: draw_marker(hover_pick_.pos, cam_, 4, 1.0f, 0.85f, 0.2f); break;
        case sim::TrackEnd::Kind::Free: draw_marker(hover_pick_.pos, cam_, 2, 0.6f, 0.6f, 0.6f); break;
        }
        return;
    }
    case Tool::Train: {
        for (std::size_t i = 0; i < route_.size(); ++i) {
            const sim::MapPoint p = net.node(world_.railway().station(route_[i]).node).pos;
            draw_marker(p, cam_, 7, 1.0f, 0.85f, 0.2f);
            if (i + 1 < route_.size()) {
                draw_line(p, net.node(world_.railway().station(route_[i + 1]).node).pos, cam_, 1.0f, 0.85f, 0.2f, 0.8f);
            }
        }
        if (const auto s = station_near(hover_)) draw_marker(net.node(world_.railway().station(*s).node).pos, cam_, 8, 1, 1, 1);
        return;
    }
    case Tool::Inspect: return;
    }
}

std::vector<Tools::Button> Tools::layout_buttons() const {
    static constexpr std::pair<Tool, const char*> kButtons[] = {
        {Tool::Inspect, "F1 INSPECT"},    {Tool::Track, "F2 TRACK"},           {Tool::Station, "F3 STATION"},
        {Tool::ServiceTower, "F4 TOWER"}, {Tool::Maintenance, "F5 MAINTENANCE"}, {Tool::Train, "F6 TRAIN"},
    };
    std::vector<Button> out;
    float x = 6.0f;
    for (const auto& [t, label] : kButtons) {
        const float w = static_cast<float>(text_width(label, kScale)) + 12.0f;
        out.push_back({t, label, x, kToolbarY, x + w, kToolbarY + kButtonH});
        x += w + 4.0f;
    }
    return out;
}

std::string Tools::hint() const {
    switch (tool_) {
    case Tool::Inspect: {
        std::string cargo_map = "O/P CARGO MAP: ";
        cargo_map += overlay_ ? world_.data().cargo.get(*overlay_).name + " (RED CHEAP, GREEN DEAR)" : "OFF";
        return "HOVER FOR DETAILS.  " + cargo_map;
    }
    case Tool::Track:
        return std::string(track_start_ ? "CLICK TO BUILD, RIGHT-CLICK TO STOP." : "CLICK TO START A LINE.") +
               "  D DOUBLE: " + (double_track_ ? "ON" : "OFF") + "  C CURVES: " + (curves_ ? "ON" : "OFF") +
               "  [ ] TUNNELS: " + std::to_string(tunnel_preference_) + "%";
    case Tool::Station:
        return std::string("CLICK ON TRACK TO BUILD A STATION.  [ ] SIZE: ") + size_name(station_size_) + " " +
               format_money(sim::station_cost(station_size_));
    case Tool::ServiceTower:
        return "CLICK ON TRACK: SERVICE TOWER (WATER, SAND) " +
               format_money(sim::service_building_cost(sim::ServiceType::ServiceTower));
    case Tool::Maintenance:
        return "CLICK ON TRACK: MAINTENANCE FACILITY (OIL) " +
               format_money(sim::service_building_cost(sim::ServiceType::MaintenanceFacility));
    case Tool::Train: {
        const auto locos = available_locos();
        std::string loco = "NONE AVAILABLE";
        if (!locos.empty()) {
            const auto& l = world_.data().locomotives.get(locos[loco_choice_ % locos.size()]);
            loco = l.name + " " + std::to_string(l.top_speed_mph) + "MPH " + format_money(l.cost);
        }
        return "CLICK STATIONS IN ORDER, ENTER TO BUY.  L ENGINE: " + loco + "  [ ] CARS: " +
               std::to_string(cars_) + "  STOPS: " + std::to_string(route_.size());
    }
    }
    return {};
}

std::string Tools::inspect_text() const {
    const sim::Railway& rw = world_.railway();
    const std::int64_t snap = snap_mm();
    for (const sim::Train& t : rw.trains()) {
        if (sim::distance_mm(rw.train_position(t.id), hover_) > snap) continue;
        const auto& loco = world_.data().locomotives.get(t.loco);
        std::string s = "TRAIN " + std::to_string(t.id + 1) + ": " + loco.name + ", " + state_name(t.state) + ", " +
                        std::to_string(t.cars.size()) + " CARS.";
        if (loco.fuel == sim::Fuel::Steam) s += " WATER " + percent(t.water);
        s += " SAND " + percent(t.sand) + " OIL " + percent(t.oil) + ". BREAKDOWNS " + std::to_string(t.breakdowns);
        return s;
    }
    if (const auto st = station_near(hover_)) {
        const sim::Station& s = rw.station(*st);
        return "STATION: " + s.name + " (" + size_name(s.size) + ")";
    }
    return cell_text();
}

void Tools::draw_ui(const std::string& status) const {
    const auto w = static_cast<float>(cam_.width_px);
    const auto h = static_cast<float>(cam_.height_px);

    // Top bar: status on the left, money spent on the right.
    fill_rect(0, 0, w, kTopBarH, 0.08f, 0.08f, 0.1f, 0.85f);
    glColor3f(0.95f, 0.95f, 0.9f);
    draw_text(8, 4, status, kScale);
    const std::string spent = "SPENT " + format_money(world_.total_spent());
    draw_text(w - static_cast<float>(text_width(spent, kScale)) - 8, 4, spent, kScale);

    for (const Button& b : layout_buttons()) {
        const bool on = b.tool == tool_;
        fill_rect(b.x0, b.y0, b.x1, b.y1, on ? 0.85f : 0.15f, on ? 0.7f : 0.15f, on ? 0.25f : 0.18f, 0.9f);
        if (on) glColor3f(0.05f, 0.05f, 0.05f);
        else glColor3f(0.9f, 0.9f, 0.9f);
        draw_text(b.x0 + 6, b.y0 + 4, b.label, kScale);
    }

    // Bottom bar: what the tool does, and the last result or what is under the cursor.
    fill_rect(0, h - kBottomBarH, w, h, 0.08f, 0.08f, 0.1f, 0.85f);
    glColor3f(0.85f, 0.85f, 0.85f);
    draw_text(8, h - kBottomBarH + 6, hint(), kScale);
    std::string line2;
    bool good = true;
    if (SDL_GetTicks() < message_until_) {
        line2 = message_;
        good = message_good_;
    } else if (tool_ == Tool::Inspect) {
        line2 = inspect_text();
    }
    if (good) glColor3f(0.6f, 0.95f, 0.6f);
    else glColor3f(1.0f, 0.45f, 0.4f);
    draw_text(8, h - kBottomBarH + 26, line2, kScale);

    // Price or problem next to the cursor while laying track.
    if (const auto cmd = pending_track()) {
        const sim::PlanResult plan = world_.preview(*cmd);
        const std::string label = plan.plan ? format_money(plan.plan->total_cost) : plan.error;
        if (plan.plan) glColor3f(1.0f, 1.0f, 0.6f);
        else glColor3f(1.0f, 0.45f, 0.4f);
        draw_text(hover_sx_ + 14, hover_sy_ + 10, label, kScale);
    }
}

} // namespace railmaster::client
