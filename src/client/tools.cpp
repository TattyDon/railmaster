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
    case sim::TrainState::Crashed: return "WRECKED";
    case sim::TrainState::Retired: return "RETIRED";
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

void Tools::draw_finance_panel() const {
    const sim::Company& co = world_.company();
    const auto& years = co.history();
    const sim::YearAccounts& now = years.back();
    const sim::YearAccounts* last = years.size() > 1 ? &years[years.size() - 2] : nullptr;

    constexpr float kRow = 16.0f;
    const float x0 = 40, y0 = 56, w = 760;
    // Ledger lines with nothing on them this year or last are left out, so
    // the screen fits the window; the rest of the panel is a fixed 37 rows.
    const auto shown = [&](std::size_t i) {
        return now.lines[i] != sim::Money{} || (last && last->lines[i] != sim::Money{});
    };
    std::size_t ledger_rows = 0;
    for (std::size_t i = 0; i < sim::kLedgerLines; ++i) ledger_rows += shown(i);
    const float h = kRow * static_cast<float>(37 + ledger_rows);
    fill_rect(x0, y0, x0 + w, y0 + h, 0.06f, 0.06f, 0.09f, 0.94f);
    float y = y0 + 10;
    const auto row = [&](const std::string& label, const std::string& a, const std::string& b, float r, float g,
                         float bl) {
        glColor3f(r, g, bl);
        draw_text(x0 + 14, y, label, kScale);
        draw_text(x0 + 500 - static_cast<float>(text_width(a, kScale)), y, a, kScale);
        draw_text(x0 + 720 - static_cast<float>(text_width(b, kScale)), y, b, kScale);
        y += kRow;
    };
    row(co.name() + (world_.player_company() ? "" : "   (YOU NO LONGER RUN IT)"), "", "", 1.0f, 0.9f, 0.5f);
    row("INCOME STATEMENT", std::to_string(now.year) + " SO FAR", last ? std::to_string(last->year) : "", 1.0f, 0.9f,
        0.5f);
    y += 4;
    for (std::size_t i = 0; i < sim::kLedgerLines; ++i) {
        if (!shown(i)) continue;
        const auto line = static_cast<sim::Ledger>(i);
        const bool rev = sim::is_revenue(line);
        const auto cell = [&](const sim::YearAccounts& ya) {
            const sim::Money m = ya.lines[i];
            return (rev || m == sim::Money{} ? "" : "-") + format_money(m);
        };
        row(sim::ledger_name(line), cell(now), last ? cell(*last) : "", rev ? 0.6f : 0.95f, rev ? 0.95f : 0.65f,
            rev ? 0.6f : 0.6f);
    }
    row("PROFIT", format_money(now.profit()), last ? format_money(last->profit()) : "", 1, 1, 1);
    y += kRow / 2;
    row("INVESTED IN TRACK", format_money(now.track_built), last ? format_money(last->track_built) : "", 0.8f, 0.8f,
        0.8f);
    row("INVESTED IN BUILDINGS", format_money(now.buildings_built), last ? format_money(last->buildings_built) : "",
        0.8f, 0.8f, 0.8f);
    row("INVESTED IN TRAINS", format_money(now.trains_bought), last ? format_money(last->trains_bought) : "", 0.8f,
        0.8f, 0.8f);
    row("INVESTED IN INDUSTRIES", format_money(now.industries_bought), last ? format_money(last->industries_bought) : "",
        0.8f, 0.8f, 0.8f);
    row("SPENT ON MERGERS", format_money(now.acquisitions), last ? format_money(last->acquisitions) : "", 0.8f, 0.8f,
        0.8f);
    y += kRow / 2;
    row("BALANCE SHEET", "", "", 1.0f, 0.9f, 0.5f);
    row("CASH", format_money(co.cash()), "", 0.9f, 0.9f, 0.9f);
    row("TRACK", format_money(co.track_value()), "", 0.9f, 0.9f, 0.9f);
    row("STATIONS AND BUILDINGS", format_money(co.building_value()), "", 0.9f, 0.9f, 0.9f);
    row("TRAINS", format_money(co.rolling_stock_value()), "", 0.9f, 0.9f, 0.9f);
    row("INDUSTRIES", format_money(co.industry_value()), "", 0.9f, 0.9f, 0.9f);
    row("BONDS", (co.debt() > sim::Money{} ? "-" : "") + format_money(co.debt()), "", 0.95f, 0.65f, 0.6f);
    row("BOOK VALUE", format_money(co.book_value()), "", 1, 1, 1);
    y += kRow / 2;
    std::string bonds;
    for (const sim::Bond& b : co.bonds()) {
        bonds += (bonds.empty() ? "" : ", ") + std::to_string(b.rate_bp / 100) + "." +
                 std::to_string(b.rate_bp % 100 / 10) + "%";
    }
    row("PRIME RATE " + std::to_string(co.prime_rate_bp() / 100) + "." +
            std::to_string(co.prime_rate_bp() % 100 / 10) + "%   COSTS " + std::to_string(world_.cost_percent()) +
            "% OF NORMAL",
        "", "", 0.9f, 0.9f, 0.9f);
    row(std::string("CREDIT RATING ") + sim::rating_name(co.credit_rating()) + "   NEW BONDS AT " +
            std::to_string(co.bond_rate_bp() / 100) + "." + std::to_string(co.bond_rate_bp() % 100 / 10) + "%",
        "", "", 1.0f, 0.9f, 0.5f);
    row("BONDS OUTSTANDING: " + (bonds.empty() ? std::string("NONE") : bonds), "", "", 0.9f, 0.9f, 0.9f);
    {
        const sim::Sentiment mood = co.sentiment();
        const bool worried = mood == sim::Sentiment::Grumbling || mood == sim::Sentiment::Hostile;
        row(std::string("INVESTORS: ") + sim::sentiment_name(mood) + "   5-YEAR RETURN " +
                format_permille(co.weighted_return_permille()) + "   BAD YEARS IN A ROW " +
                std::to_string(co.bad_year_streak()) + "   SALARY " + format_money(sim::chairman_salary(co)),
            "", "", worried ? 0.95f : 0.9f, worried ? 0.65f : 0.9f, worried ? 0.6f : 0.9f);
    }

    y += kRow / 2;
    const sim::Investor& me = world_.investor();
    const auto pct = [](std::int64_t part, std::int64_t whole) {
        return whole > 0 ? std::to_string(part * 100 / whole) + "%" : std::string("-");
    };
    row("STOCK", "", "", 1.0f, 0.9f, 0.5f);
    row("SHARE PRICE", format_cents(co.share_price()), "", 0.9f, 0.9f, 0.9f);
    row("SHARES OUTSTANDING", format_count(co.shares_outstanding()), "", 0.9f, 0.9f, 0.9f);
    row("BOOK VALUE PER SHARE", format_cents(co.book_value_per_share()), "", 0.9f, 0.9f, 0.9f);
    row("DIVIDEND PER SHARE (A YEAR)", format_cents(co.dividend_per_share()),
        "PAID " + format_money(now.dividends_paid), 0.9f, 0.9f, 0.9f);
    row("STOCK ISSUES THIS YEAR", std::to_string(co.stock_issues_this_year()) + " OF 2", "", 0.9f, 0.9f, 0.9f);
    y += kRow / 2;
    row("YOU", "", "", 1.0f, 0.9f, 0.5f);
    row("PERSONAL CASH", format_money(me.cash), "", me.cash < sim::Money{} ? 0.95f : 0.9f,
        me.cash < sim::Money{} ? 0.65f : 0.9f, me.cash < sim::Money{} ? 0.6f : 0.9f);
    const std::int64_t mine = me.shares_in(co.id());
    row("SHARES HELD", format_count(mine) + " (" + pct(mine, co.shares_outstanding()) + ")",
        format_money(co.share_price() * mine), 0.9f, 0.9f, 0.9f);
    row("PURCHASING POWER", format_money(sim::purchasing_power(me, world_.market())), "", 0.9f, 0.9f, 0.9f);
    row("NET WORTH", format_money(sim::net_worth(me, world_.market())), "", 1, 1, 1);
}

std::string format_permille(std::int32_t p) {
    const std::int32_t a = p < 0 ? -p : p;
    return (p < 0 ? "-" : "+") + std::to_string(a / 10) + "." + std::to_string(a % 10) + "%";
}

std::string format_count(std::int64_t n) {
    const std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}

std::string format_cents(sim::Money m) {
    const std::int64_t c = m.in_cents();
    const std::int64_t frac = (c < 0 ? -c : c) % 100;
    return format_money(m) + "." + (frac < 10 ? "0" : "") + std::to_string(frac);
}

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
    if (founding_) return;
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
    case Tool::Inspect:
    case Tool::Finance:
    case Tool::Market: return;
    case Tool::Industry: {
        if (const auto id = site_under_cursor()) {
            const sim::Site& s = world_.economy().sites()[*id];
            const std::string name = world_.data().industries.get(s.type).name;
            if (s.owner && s.owner == world_.player_company()) {
                const sim::CommandResult r = world_.execute(sim::UpgradeIndustry{.site = *id});
                if (r.ok) show("UPGRADED THE " + name + " TO LEVEL " + std::to_string(s.level) + " FOR " + format_money(r.cost), true);
                else show("CANNOT UPGRADE: " + r.error, false);
            } else {
                const sim::CommandResult r = world_.execute(sim::BuyIndustry{.site = *id});
                if (r.ok) show("BOUGHT THE " + name + " FOR " + format_money(r.cost), true);
                else show("CANNOT BUY: " + r.error, false);
            }
            return;
        }
        const auto types = buildable_types();
        if (types.empty()) {
            show("NO PLANTS CAN BE BUILT YET", false);
            return;
        }
        const sim::IndustryTypeId t = types[build_choice_ % types.size()];
        const sim::CommandResult r = world_.execute(sim::BuildIndustry{
            .type = t, .cx = world_.economy().cell_x(hover_), .cy = world_.economy().cell_y(hover_)});
        if (r.ok) show("BUILT A " + world_.data().industries.get(t).name + " FOR " + format_money(r.cost), true);
        else show("CANNOT BUILD: " + r.error, false);
        return;
    }
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
        if (station_building_) {
            const sim::CommandResult r = world_.execute(sim::BuildStationBuilding{.type = *station_building_, .pos = hover_});
            if (r.ok) show(std::string("BUILT A ") + sim::station_building_name(*station_building_) + " FOR " + format_money(r.cost), true);
            else show("CANNOT BUILD: " + r.error, false);
            return;
        }
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

void Tools::open_founding(bool cancellable) {
    const sim::Balance::Stock& b = world_.data().balance.stock;
    founding_ = true;
    founding_cancellable_ = cancellable;
    found_personal_ = std::min(b.founder_investment, world_.investor().cash.whole_dollars());
    found_outside_ = b.outside_investment;
}

bool Tools::founding_key(SDL_Keycode key, Uint16 mod) {
    const sim::Balance::Stock& b = world_.data().balance.stock;
    const std::int64_t step = (mod & KMOD_SHIFT) != 0 ? 1'000'000 : 100'000;
    const std::int64_t fortune = std::max<std::int64_t>(0, world_.investor().cash.whole_dollars());
    switch (key) {
    case SDLK_LEFT: found_personal_ = std::max<std::int64_t>(b.min_founder_investment, found_personal_ - step); break;
    case SDLK_RIGHT: found_personal_ = std::min(fortune, found_personal_ + step); break;
    case SDLK_DOWN: found_outside_ = std::max<std::int64_t>(0, found_outside_ - step); break;
    case SDLK_UP: found_outside_ = std::min(b.outside_investment, found_outside_ + step); break;
    case SDLK_ESCAPE:
        if (founding_cancellable_) founding_ = false;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
        bool taken = false;
        for (const sim::Company& c : world_.companies()) taken |= c.name() == "Railmaster Railroad";
        const std::string name = taken ? "Railmaster Railroad " + std::to_string(world_.companies().size() + 1)
                                       : "Railmaster Railroad";
        const sim::CommandResult r = world_.execute(sim::FoundCompany{.name = name,
                                                                      .personal_investment = sim::Money::dollars(found_personal_),
                                                                      .outside_investment = sim::Money::dollars(found_outside_)});
        if (r.ok) {
            founding_ = false;
            show("YOU HAVE FOUNDED " + name, true);
        } else {
            show("CANNOT FOUND IT: " + r.error, false);
        }
        break;
    }
    default: break;
    }
    return true;
}

void Tools::draw_founding_dialog() const {
    const sim::Balance::Stock& b = world_.data().balance.stock;
    constexpr float kRow = 20.0f;
    const auto w = static_cast<float>(cam_.width_px);
    const float x0 = std::max(20.0f, w / 2 - 470), y0 = 90;
    fill_rect(x0, y0, x0 + 940, y0 + kRow * 15, 0.06f, 0.06f, 0.09f, 0.96f);
    float y = y0 + 14;
    const auto line = [&](const std::string& s, float r, float g, float bl) {
        glColor3f(r, g, bl);
        draw_text(x0 + 20, y, s, kScale);
        y += kRow;
    };
    const sim::Money mine = sim::Money::dollars(found_personal_);
    const sim::Money outside = sim::Money::dollars(found_outside_);
    const std::int64_t price = std::max<std::int64_t>(1, b.founding_share_price_cents);
    const std::int64_t shares = (mine + outside).in_cents() / price;
    const std::int64_t stake = mine.in_cents() / price;
    line("FOUND YOUR RAILROAD", 1.0f, 0.9f, 0.5f);
    y += kRow / 2;
    line("YOUR FORTUNE " + format_money(world_.investor().cash), 0.9f, 0.9f, 0.9f);
    line("YOU INVEST " + format_money(mine) + "   (LEFT/RIGHT, SHIFT FOR $1,000,000)", 1, 1, 1);
    line("OUTSIDE INVESTORS PUT IN " + format_money(outside) + " OF " +
             format_money(sim::Money::dollars(b.outside_investment)) + " OFFERED   (UP/DOWN)",
         1, 1, 1);
    y += kRow / 2;
    line("CAPITAL " + format_money(mine + outside) + "   " + format_count(shares) + " SHARES AT " +
             format_cents(sim::Money::cents(price)),
         0.9f, 0.9f, 0.9f);
    line("YOUR STAKE " + format_count(stake) + " SHARES (" + std::to_string(shares > 0 ? stake * 100 / shares : 0) +
             "%)   YOUR CASH LEFT " + format_money(world_.investor().cash - mine),
         0.9f, 0.9f, 0.9f);
    if (stake * 2 <= shares) {
        line("AT HALF THE SHARES OR LESS, INVESTORS CAN VOTE YOU OUT AFTER BAD YEARS", 0.95f, 0.65f, 0.6f);
    } else {
        line("WITH OVER HALF THE SHARES, NOBODY CAN VOTE YOU OUT", 0.6f, 0.95f, 0.6f);
    }
    line("MORE OUTSIDE MONEY BUILDS MORE RAILROAD, BUT DILUTES YOUR CONTROL", 0.8f, 0.8f, 0.8f);
    y += kRow / 2;
    line(founding_cancellable_ ? "ENTER TO FOUND THE COMPANY, ESC TO CANCEL" : "ENTER TO FOUND THE COMPANY", 1.0f, 0.9f,
         0.5f);
}

bool Tools::on_key(SDL_Keycode key, Uint16 mod) {
    if (founding_) return founding_key(key, mod);
    switch (key) {
    case SDLK_F1: select(Tool::Inspect); return true;
    case SDLK_F2: select(Tool::Track); return true;
    case SDLK_F3: select(Tool::Station); return true;
    case SDLK_F4: select(Tool::ServiceTower); return true;
    case SDLK_F5: select(Tool::Maintenance); return true;
    case SDLK_F6: select(Tool::Train); return true;
    case SDLK_F7: select(Tool::Finance); return true;
    case SDLK_F8: select(Tool::Market); return true;
    case SDLK_F9: select(Tool::Industry); return true;
    case SDLK_o: cycle_overlay(+1); return true;
    case SDLK_p: cycle_overlay(-1); return true;
    case SDLK_ESCAPE:
        if (track_start_ || !route_.empty()) cancel();
        else select(Tool::Inspect);
        return true;
    default: break;
    }

    switch (tool_) {
    case Tool::Inspect: {
        // Keys act on the train under the cursor (rt3-clone-spec §9.3).
        const sim::Railway& rw = world_.railway();
        std::optional<sim::TrainId> id;
        for (const sim::Train& t : rw.trains()) {
            if (t.in_service() && sim::distance_mm(rw.train_position(t.id), hover_) <= snap_mm()) {
                id = t.id;
                break;
            }
        }
        if (!id) return false;
        const sim::Train& t = rw.train(*id);
        const std::string name = "TRAIN " + std::to_string(*id + 1);
        const auto report = [&](const sim::CommandResult& r, const std::string& done) {
            if (r.ok) show(done, true);
            else show("CANNOT: " + r.error, false);
            return true;
        };
        // Change every stop's rule the same way ("apply to all stations").
        const auto every_stop = [&](auto&& change, const std::string& done) {
            sim::ConsistRule rule = t.rules.empty() ? sim::ConsistRule{} : t.rules.front();
            rule.custom = false;
            change(rule);
            return report(world_.execute(sim::SetConsist{.train = *id, .rule = rule}), done);
        };
        switch (key) {
        case SDLK_r: {
            const auto locos = available_locos();
            if (locos.empty()) {
                show("NO ENGINE IS AVAILABLE", false);
                return true;
            }
            const sim::LocoTypeId loco = locos[loco_choice_ % locos.size()];
            return report(world_.execute(sim::ReplaceLocomotive{.train = *id, .loco = loco}),
                          name + " NOW PULLED BY A NEW " + world_.data().locomotives.get(loco).name);
        }
        case SDLK_f:
            return every_stop(
                [](sim::ConsistRule& r) { r.filter = static_cast<sim::CargoFilter>((static_cast<int>(r.filter) + 1) % 3); },
                name + " CONSIST CHANGED");
        case SDLK_w:
            return every_stop([](sim::ConsistRule& r) { r.min = r.min > 0 ? 0 : r.max; },
                              name + (t.rules.empty() || t.rules.front().min == 0 ? " WILL WAIT FOR FULL LOADS"
                                                                                 : " NO LONGER WAITS FOR FULL LOADS"));
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET:
            return every_stop(
                [&](sim::ConsistRule& r) {
                    if (key == SDLK_LEFTBRACKET && r.max > 0) --r.max;
                    if (key == SDLK_RIGHTBRACKET) ++r.max;
                    r.min = std::min(r.min, r.max);
                },
                name + " CONSIST CHANGED");
        case SDLK_c:
            return report(world_.execute(sim::SetSpecialCars{.train = *id, .caboose = !t.caboose, .diner = t.diner}),
                          name + (t.caboose ? " DROPS ITS CABOOSE" : " GETS A CABOOSE"));
        case SDLK_d:
            return report(world_.execute(sim::SetSpecialCars{.train = *id, .caboose = t.caboose, .diner = !t.diner}),
                          name + (t.diner ? " DROPS ITS DINING CAR" : " GETS A DINING CAR"));
        case SDLK_y: {
            const sim::CommandResult r = world_.execute(sim::CopyTrain{.train = *id});
            return report(r, "BOUGHT TRAIN " + std::to_string(r.created_id + 1) + ", A COPY OF " + name);
        }
        case SDLK_x:
            // Retiring cannot be undone: ask for a second press.
            if (SDL_GetTicks() > retire_armed_until_ || retire_armed_ != *id) {
                retire_armed_until_ = SDL_GetTicks() + 3000;
                retire_armed_ = *id;
                show("PRESS X AGAIN TO RETIRE " + name, false);
                return true;
            }
            retire_armed_until_ = 0;
            return report(world_.execute(sim::RetireTrain{.train = *id}), name + " RETIRED");
        default: return false;
        }
    }
    case Tool::Track:
        if (key == SDLK_e) {
            // Electrify the piece under the cursor, or with Shift all your track.
            sim::ElectrifyTrack cmd;
            if ((mod & KMOD_SHIFT) == 0) {
                const auto p = world_.railway().track().nearest_edge_point(hover_, snap_mm());
                if (!p) {
                    show("POINT AT A PIECE OF TRACK (SHIFT+E FOR ALL YOUR TRACK)", false);
                    return true;
                }
                cmd.edges.push_back(p->edge);
            }
            const sim::CommandResult r = world_.execute(cmd);
            if (r.ok) show("ELECTRIFIED FOR " + format_money(r.cost), true);
            else show("CANNOT: " + r.error, false);
            return true;
        }
        if (key == SDLK_d) double_track_ = !double_track_;
        else if (key == SDLK_c) curves_ = !curves_;
        else if (key == SDLK_LEFTBRACKET) tunnel_preference_ = std::max(0, tunnel_preference_ - 10);
        else if (key == SDLK_RIGHTBRACKET) tunnel_preference_ = std::min(100, tunnel_preference_ + 10);
        else return false;
        return true;
    case Tool::Station:
        if (key == SDLK_b) {
            // Station, then post office, hotel, restaurant, tavern, and round again.
            if (!station_building_) station_building_ = sim::StationBuildingType::PostOffice;
            else if (*station_building_ == sim::StationBuildingType::Tavern) station_building_.reset();
            else station_building_ = static_cast<sim::StationBuildingType>(static_cast<int>(*station_building_) + 1);
            return true;
        }
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
    case Tool::Finance: {
        if (key == SDLK_k) {
            // A last resort: ask for a second press.
            if (SDL_GetTicks() > bankrupt_armed_until_) {
                bankrupt_armed_until_ = SDL_GetTicks() + 3000;
                show("PRESS K AGAIN TO DECLARE BANKRUPTCY", false);
                return true;
            }
            bankrupt_armed_until_ = 0;
            const sim::CommandResult r = world_.execute(sim::DeclareBankruptcy{});
            if (r.ok) show("BANKRUPT: HALF THE BOND DEBT IS GONE, AND SO IS YOUR CREDIT", false);
            else show("CANNOT DECLARE BANKRUPTCY: " + r.error, false);
            return true;
        }
        const bool big = (mod & KMOD_SHIFT) != 0; // 5,000 shares at a time
        const std::int64_t blocks = big ? 5 : 1;
        const sim::Money step = sim::Money::cents(25);
        sim::CommandResult r;
        std::string done;
        if (key == SDLK_b) {
            r = world_.execute(sim::IssueBond{});
            done = "ISSUED A " + format_money(sim::Money::dollars(world_.company().finance_balance().bond_face_value)) +
                   " BOND";
        } else if (key == SDLK_r) {
            r = world_.execute(sim::RepayBond{});
            done = "REPAID A BOND";
        } else if (key == SDLK_a) {
            r = world_.execute(sim::BuyShares{.blocks = blocks});
            done = "BOUGHT " + format_count(blocks * world_.company().stock_balance().share_block) + " SHARES";
        } else if (key == SDLK_s) {
            r = world_.execute(sim::SellShares{.blocks = blocks});
            done = "SOLD " + format_count(blocks * world_.company().stock_balance().share_block) + " SHARES";
        } else if (key == SDLK_i) {
            r = world_.execute(sim::IssueStock{});
            done = "ISSUED NEW STOCK";
        } else if (key == SDLK_y) {
            r = world_.execute(sim::BuyBackStock{});
            done = "BOUGHT BACK STOCK";
        } else if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
            const sim::Money now = world_.company().dividend_per_share();
            const sim::Money next = key == SDLK_RIGHTBRACKET ? now + step : std::max(sim::Money{}, now - step);
            r = world_.execute(sim::SetDividend{.per_share = next});
            done = "DIVIDEND SET TO " + format_cents(next) + " A SHARE A YEAR";
        } else {
            return false;
        }
        if (r.ok) show(done, true);
        else show("CANNOT: " + r.error, false);
        return true;
    }
    case Tool::Industry:
        if (key == SDLK_m) {
            const auto id = site_under_cursor();
            if (!id || world_.economy().sites()[*id].kind != sim::IndustryKind::Warehouse) {
                show("POINT AT ONE OF YOUR WAREHOUSES TO CHANGE WHAT IT DOES", false);
                return true;
            }
            const auto next = static_cast<sim::PortMode>((static_cast<int>(world_.economy().sites()[*id].port_mode) + 1) % 3);
            const sim::CommandResult r = world_.execute(sim::SetPortMode{.site = *id, .mode = next});
            if (r.ok) show(std::string("THE WAREHOUSE NOW HANDLES ") + sim::port_mode_name(next), true);
            else show("CANNOT: " + r.error, false);
            return true;
        }
        if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
            const std::size_t n = std::max<std::size_t>(1, buildable_types().size());
            build_choice_ = (build_choice_ + (key == SDLK_RIGHTBRACKET ? 1 : n - 1)) % n;
            return true;
        }
        return false;
    case Tool::Market: {
        const auto n = static_cast<sim::CompanyId>(world_.companies().size());
        if (key == SDLK_UP) {
            market_choice_ = static_cast<sim::CompanyId>((market_choice_ + n - 1) % n);
            return true;
        }
        if (key == SDLK_DOWN) {
            market_choice_ = static_cast<sim::CompanyId>((market_choice_ + 1) % n);
            return true;
        }
        const sim::Company& target = world_.company(market_choice_ % n);
        if (key == SDLK_n) {
            if (world_.player_company()) {
                show("YOU ALREADY RUN A COMPANY", false);
            } else {
                open_founding(/*cancellable=*/true);
            }
            return true;
        }
        if (key == SDLK_q) {
            // Resigning cannot be undone: ask for a second press.
            if (SDL_GetTicks() > resign_armed_until_) {
                resign_armed_until_ = SDL_GetTicks() + 3000;
                show("PRESS Q AGAIN TO RESIGN AS CHAIRMAN", false);
                return true;
            }
            resign_armed_until_ = 0;
            const sim::CommandResult r = world_.execute(sim::Resign{});
            if (r.ok) show("YOU HAVE RESIGNED. YOU KEEP YOUR SHARES", true);
            else show("CANNOT RESIGN: " + r.error, false);
            return true;
        }
        if (key == SDLK_t) {
            const sim::CommandResult r = world_.execute(sim::AttemptTakeover{.target = target.id()});
            if (r.ok) show("THE SHAREHOLDERS MADE YOU CHAIRMAN OF " + target.name(), true);
            else show("TAKEOVER FAILED: " + r.error, false);
            return true;
        }
        if (key == SDLK_m) {
            // Offer a premium over the market price: 20%, or 50% with Shift.
            const std::int32_t premium = (mod & KMOD_SHIFT) != 0 ? 150 : 120;
            const sim::Money offer = target.share_price().scaled(premium, 100);
            const std::string name = target.name();
            const sim::CommandResult r =
                world_.execute(sim::AttemptMerger{.target = target.id(), .offer_per_share = offer});
            if (r.ok) show("MERGED " + name + " FOR " + format_money(r.cost), true);
            else show("MERGER FAILED: " + r.error, false);
            return true;
        }
        const bool buy = key == SDLK_a;
        const bool sell = key == SDLK_s;
        if (!buy && !sell) return false;
        const std::int64_t blocks = (mod & KMOD_SHIFT) != 0 ? 5 : 1;
        const std::string shares = format_count(blocks * target.stock_balance().share_block);
        const sim::CommandResult r =
            buy ? world_.execute(sim::BuyShares{.blocks = blocks, .company = target.id()})
                : world_.execute(sim::SellShares{.blocks = blocks, .company = target.id()});
        const bool now_short = world_.investor().shares_in(target.id()) < 0;
        if (r.ok) show((buy ? "BOUGHT " : now_short ? "SOLD SHORT " : "SOLD ") + shares + " " + target.name() + " SHARES", true);
        else show("CANNOT: " + r.error, false);
        return true;
    }
    default: return false;
    }
}

void Tools::draw_market_panel() const {
    constexpr float kRow = 16.0f;
    const auto& companies = world_.companies();
    const float x0 = 40, y0 = 56, w = 1100;
    const float h = kRow * static_cast<float>(companies.size() + world_.investors().size() + 12);
    fill_rect(x0, y0, x0 + w, y0 + h, 0.06f, 0.06f, 0.09f, 0.94f);
    float y = y0 + 10;
    // Name, then right-aligned columns ending at each later position.
    const float cols[] = {x0 + 14, 0, x0 + 600, x0 + 780, x0 + 950, x0 + 1090};
    const auto cells = [&](const std::vector<std::string>& v, float r, float g, float b) {
        glColor3f(r, g, b);
        draw_text(cols[0], y, v[0], kScale);
        for (std::size_t i = 1; i < v.size(); ++i) {
            draw_text(cols[i + 1] - static_cast<float>(text_width(v[i], kScale)), y, v[i], kScale);
        }
        y += kRow;
    };
    cells({"STOCK MARKET", "PRICE", "BOOK/SHARE", "LAST YEAR", "YOU HOLD"}, 1.0f, 0.9f, 0.5f);
    const sim::Investor& me = world_.investor();
    const auto n = static_cast<sim::CompanyId>(companies.size());
    const sim::CompanyId chosen = static_cast<sim::CompanyId>(market_choice_ % n);
    for (const sim::Company& c : companies) {
        const auto& hist = c.history();
        const sim::Money last = hist.size() > 1 ? hist[hist.size() - 2].profit() : hist.back().profit();
        const bool sel = c.id() == chosen;
        float r, g, b;
        owner_rgb(c.id(), world_.player_company(), r, g, b);
        const float swatch = cols[0] + static_cast<float>(text_width("> ", kScale));
        fill_rect(swatch, y + 2, swatch + 8, y + 10, r, g, b, 1.0f);
        if (c.defunct()) {
            cells({std::string(sel ? ">" : " ") + "    " + c.name() + " (MERGED INTO " +
                       world_.company(*c.merged_into()).name() + ")",
                   "", "", "", ""},
                  0.5f, 0.5f, 0.5f);
            continue;
        }
        const std::int64_t held = me.shares_in(c.id());
        cells({std::string(sel ? ">" : " ") + "    " + c.name(), format_cents(c.share_price()),
               format_cents(c.book_value_per_share()), format_money(last),
               format_count(held)}, // negative: sold short
              sel ? 1.0f : 0.85f, sel ? 1.0f : 0.85f, sel ? 0.7f : 0.85f);
    }
    y += kRow / 2;

    const sim::Company& c = world_.company(chosen);
    std::string chairman = "NONE";
    std::string bio;
    for (const sim::Investor& inv : world_.investors()) {
        if (inv.chairs && *inv.chairs == c.id()) chairman = inv.name;
    }
    for (const sim::Rival& rv : world_.rivals()) {
        if (world_.investors()[rv.player].chairs == c.id()) bio = world_.data().tycoons.all()[rv.tycoon].bio;
    }
    std::int32_t stations = 0, trains = 0;
    for (const sim::Station& s : world_.railway().stations()) stations += s.owner == c.id();
    for (const sim::Train& t : world_.railway().trains()) trains += t.owner == c.id() && t.in_service();
    glColor3f(1.0f, 0.9f, 0.5f);
    draw_text(cols[0], y, c.name() + "   CHAIRMAN: " + chairman, kScale);
    y += kRow;
    glColor3f(0.8f, 0.8f, 0.8f);
    if (!bio.empty()) {
        draw_text(cols[0], y, bio, kScale);
        y += kRow;
    }
    const auto line = [&](const std::string& s) {
        draw_text(cols[0], y, s, kScale);
        y += kRow;
    };
    line("CASH " + format_money(c.cash()) + "   BOOK VALUE " + format_money(c.book_value()) + "   BONDS " +
         std::to_string(c.bonds().size()) + "   RATING " + sim::rating_name(c.credit_rating()));
    line("TRACK " + format_money(c.track_value()) + "   STATIONS " + std::to_string(stations) + "   TRAINS " +
         std::to_string(trains));
    line(std::string("INVESTORS ") + sim::sentiment_name(c.sentiment()) + "   5-YEAR RETURN " +
         format_permille(c.weighted_return_permille()) + "   BAD YEARS IN A ROW " + std::to_string(c.bad_year_streak()));
    line("SHARES " + format_count(c.shares_outstanding()) + "   IN PUBLIC HANDS " +
         format_count(sim::public_float(world_.market(), c.id())) + "   DIVIDEND " + format_cents(c.dividend_per_share()) +
         " A YEAR");
    const sim::YearAccounts& now = c.this_year();
    line("THIS YEAR: REVENUE " + format_money(now.revenue()) + "   PROFIT " + format_money(now.profit()) +
         "   TRACKAGE IN " + format_money(now.lines[static_cast<std::size_t>(sim::Ledger::TrackageIncome)]) +
         " OUT " + format_money(now.lines[static_cast<std::size_t>(sim::Ledger::TrackagePaid)]));
    y += kRow / 2;
    glColor3f(1.0f, 0.9f, 0.5f);
    line("PLAYERS");
    for (const sim::Investor& inv : world_.investors()) {
        glColor3f(0.85f, 0.85f, 0.85f);
        line(inv.name + "   NET WORTH " + format_money(sim::net_worth(inv, world_.market())) + "   CASH " +
             format_money(inv.cash) + "   " +
             (inv.chairs ? "RUNS " + world_.company(*inv.chairs).name() : std::string("NO COMPANY")));
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
    if (founding_) return;
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
    case Tool::Inspect:
    case Tool::Finance:
    case Tool::Market: return;
    case Tool::Industry: {
        // Owned industries in their owner's colour; the cell under the cursor in white.
        const sim::Economy& eco = world_.economy();
        for (const sim::Site& s : eco.sites()) {
            if (!s.owner || s.closed) continue;
            float r, g, b;
            owner_rgb(*s.owner, world_.player_company(), r, g, b);
            draw_marker(eco.node_centre(s.cx, s.cy), cam_, 7, r, g, b);
        }
        draw_marker(eco.node_centre(eco.cell_x(hover_), eco.cell_y(hover_)), cam_, 4, 1, 1, 1);
        return;
    }
    }
}

std::vector<sim::IndustryTypeId> Tools::buildable_types() const {
    std::vector<sim::IndustryTypeId> out;
    for (const sim::IndustryType& t : world_.data().industries.all()) {
        if (!sim::buildable(t.kind)) continue;
        bool available = true; // a warehouse trades whatever of its cargo exists
        if (t.kind != sim::IndustryKind::Warehouse) {
            for (sim::CargoId c : t.outputs) available &= world_.data().cargo.get(c).available_year <= world_.date().year();
        }
        if (available) out.push_back(t.id);
    }
    return out;
}

std::optional<sim::SiteId> Tools::site_under_cursor() const {
    const std::int32_t cx = world_.economy().cell_x(hover_), cy = world_.economy().cell_y(hover_);
    for (const sim::Site& s : world_.economy().sites()) {
        if (s.closed || s.cx != cx || s.cy != cy) continue;
        if (world_.data().industries.get(s.type).kind != sim::IndustryKind::House) return s.id;
    }
    return std::nullopt;
}

std::string Tools::industry_text() const {
    const auto id = site_under_cursor();
    if (!id) return "OPEN LAND";
    const sim::Site& s = world_.economy().sites()[*id];
    const sim::Balance::Industries& b = world_.data().balance.industries;
    const sim::IndustryType& type = world_.data().industries.get(s.type);
    std::string text = type.name + " (LEVEL " + std::to_string(s.level) + "): ";
    if (type.kind == sim::IndustryKind::Port) return text + sim::port_mode_name(s.port_mode) + ". NOT FOR SALE";
    if (type.kind == sim::IndustryKind::Sink) return text + "A CONSUMER. NOT FOR SALE";
    if (!s.owner) {
        text += "FOR SALE AT " + format_money(sim::industry_price(s, b));
    } else if (s.owner == world_.player_company()) {
        text += "YOURS";
        if (type.kind == sim::IndustryKind::Warehouse) text += std::string(", ") + sim::port_mode_name(s.port_mode) + " (M TO CHANGE)";
        if (sim::buildable(type.kind)) {
            text += ", UPGRADE " + format_money(world_.construction_cost(sim::industry_upgrade_cost(s, b)));
        }
    } else {
        text += "OWNED BY " + world_.company(*s.owner).name();
    }
    return text + ".  PROFIT " + format_money(sim::annual_profit(s)) + " A YEAR, RUNNING AT " +
           std::to_string(s.utilisation_permille / 10) + "%";
}

std::vector<Tools::Button> Tools::layout_buttons() const {
    static constexpr std::pair<Tool, const char*> kButtons[] = {
        {Tool::Inspect, "F1 INSPECT"},    {Tool::Track, "F2 TRACK"},           {Tool::Station, "F3 STATION"},
        {Tool::ServiceTower, "F4 TOWER"}, {Tool::Maintenance, "F5 MAINTENANCE"}, {Tool::Train, "F6 TRAIN"},
        {Tool::Finance, "F7 FINANCES"}, {Tool::Market, "F8 MARKET"}, {Tool::Industry, "F9 INDUSTRY"},
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
        std::string cargo_map = "O/P MAP: ";
        cargo_map += overlay_ ? world_.data().cargo.get(*overlay_).name : "OFF";
        return "TRAIN: F FILTER W FULL [ ] CARS C CABOOSE D DINER Y COPY XX RETIRE R ENGINE  " + cargo_map;
    }
    case Tool::Track:
        return std::string(track_start_ ? "CLICK TO BUILD, RIGHT-CLICK TO STOP." : "CLICK TO START A LINE.") +
               "  D DOUBLE: " + (double_track_ ? "ON" : "OFF") + "  C CURVES: " + (curves_ ? "ON" : "OFF") +
               "  [ ] TUNNELS: " + std::to_string(tunnel_preference_) + "%  E ELECTRIFY (SHIFT: ALL)";
    case Tool::Station:
        if (station_building_) {
            return std::string("CLICK NEAR A STATION TO BUILD A ") + sim::station_building_name(*station_building_) + " " +
                   format_money(world_.construction_cost(sim::station_building_cost(*station_building_, world_.data().balance))) +
                   ".  B NEXT KIND";
        }
        return std::string("CLICK ON TRACK TO BUILD A STATION.  B FOR BUILDINGS.  [ ] SIZE: ") + size_name(station_size_) + " " +
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
    case Tool::Industry: {
        const auto types = buildable_types();
        const std::string plant =
            types.empty() ? std::string("NONE YET") : world_.data().industries.get(types[build_choice_ % types.size()]).name;
        return "CLICK AN INDUSTRY TO BUY IT, YOURS TO UPGRADE, OPEN LAND TO BUILD.  [ ] " + plant + " " +
               format_money(world_.construction_cost(sim::industry_build_cost(world_.data().balance.industries)));
    }
    case Tool::Market:
        return "UP/DOWN CHOOSE  A/S BUY/SELL, BELOW 0 IS SHORT  T TAKEOVER  M MERGE +20% (SHIFT +50%)  QQ RESIGN  N NEW "
               "COMPANY";
    case Tool::Finance:
        return "B/R BOND ISSUE/REPAY   A/S BUY/SELL 1,000 SHARES (SHIFT 5,000)   I/Y ISSUE/BUY BACK STOCK   [ ] "
               "DIVIDEND   KK BANKRUPTCY";
    }
    return {};
}

std::string Tools::inspect_text() const {
    const sim::Railway& rw = world_.railway();
    const std::int64_t snap = snap_mm();
    for (const sim::Train& t : rw.trains()) {
        if (sim::distance_mm(rw.train_position(t.id), hover_) > snap) continue;
        const auto& loco = world_.data().locomotives.get(t.loco);
        std::string s = "TRAIN " + std::to_string(t.id + 1) + ": " + loco.name + ", " + state_name(t.state) + ". ";
        if (t.owner != world_.company().id()) s = world_.company(t.owner).name() + " " + s;
        // Load, grouped by cargo: "2 COAL, 1 STEEL, 1 EMPTY".
        std::vector<std::pair<std::string, int>> load;
        for (const sim::Car& car : t.cars) {
            const std::string what = car.cargo ? world_.data().cargo.get(*car.cargo).name : "EMPTY";
            auto it = std::find_if(load.begin(), load.end(), [&](const auto& p) { return p.first == what; });
            if (it == load.end()) load.emplace_back(what, 1);
            else ++it->second;
        }
        for (std::size_t i = 0; i < load.size(); ++i) {
            s += (i ? ", " : "") + std::to_string(load[i].second) + " " + load[i].first;
        }
        const std::int32_t age = (world_.date().days_since_epoch() - t.built_day) / 365;
        s += ". ENGINE " + std::to_string(age) + (age == 1 ? " YEAR" : " YEARS") + " OLD. EARNED " + format_money(t.revenue) + ".";
        if (!t.rules.empty()) {
            const sim::ConsistRule& r = t.rules.front();
            std::string what = r.custom ? std::string("CUSTOM") : std::string(sim::cargo_filter_name(r.filter));
            for (char& ch : what) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            s += " " + what + " " + std::to_string(r.min) + "-" + std::to_string(r.capacity()) + " CARS";
            if (t.holding) s += ", WAITING FOR A FULL LOAD";
            if (t.caboose) s += ", CABOOSE";
            if (t.diner) s += ", DINER";
            s += ".";
        }
        if (loco.fuel == sim::Fuel::Steam) s += " WATER " + percent(t.water);
        s += " SAND " + percent(t.sand) + " OIL " + percent(t.oil);
        return s;
    }
    if (const auto st = station_near(hover_)) {
        const sim::Station& s = rw.station(*st);
        std::string text = "STATION: " + s.name + " (" + size_name(s.size) + ")" +
                           (s.owner != world_.company().id() ? ", " + world_.company(s.owner).name() : std::string()) +
                           ". WAITING:";
        bool any = false;
        for (std::size_t c = 0; c < s.waiting.size(); ++c) {
            if (s.waiting[c].milli < sim::kMilli) continue;
            text += std::string(any ? ", " : " ") + std::to_string(s.waiting[c].milli / sim::kMilli) + " " +
                    world_.data().cargo.get(static_cast<sim::CargoId>(c)).name;
            any = true;
        }
        // Express loads, totalled over destinations.
        std::vector<std::int32_t> express(world_.data().cargo.all().size(), 0);
        for (const sim::ExpressWaiting& e : s.express) express[e.cargo] += e.milli;
        for (std::size_t c = 0; c < express.size(); ++c) {
            if (express[c] < sim::kMilli) continue;
            text += std::string(any ? ", " : " ") + std::to_string(express[c] / sim::kMilli) + " " +
                    world_.data().cargo.get(static_cast<sim::CargoId>(c)).name;
            any = true;
        }
        return any ? text : text + " NOTHING";
    }
    return cell_text();
}

void Tools::draw_ui(const std::string& status) const {
    const auto w = static_cast<float>(cam_.width_px);
    const auto h = static_cast<float>(cam_.height_px);
    if (founding_) {
        fill_rect(0, 0, w, kTopBarH, 0.08f, 0.08f, 0.1f, 0.85f);
        glColor3f(0.95f, 0.95f, 0.9f);
        draw_text(8, 4, status, kScale);
        draw_founding_dialog();
        if (SDL_GetTicks() < message_until_) {
            if (message_good_) glColor3f(0.6f, 0.95f, 0.6f);
            else glColor3f(0.95f, 0.5f, 0.45f);
            draw_text(8, h - 22, message_, kScale);
        }
        return;
    }

    // Top bar: status on the left, money spent on the right.
    fill_rect(0, 0, w, kTopBarH, 0.08f, 0.08f, 0.1f, 0.85f);
    glColor3f(0.95f, 0.95f, 0.9f);
    draw_text(8, 4, status, kScale);
    const std::string spent = (world_.sandbox() ? std::string("SANDBOX   ") : std::string()) + "COMPANY " +
                              format_money(world_.company().cash()) + "   NET WORTH " +
                              format_money(sim::net_worth(world_.investor(), world_.market()));
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
    } else if (tool_ == Tool::Industry) {
        line2 = industry_text();
    }
    if (good) glColor3f(0.6f, 0.95f, 0.6f);
    else glColor3f(1.0f, 0.45f, 0.4f);
    draw_text(8, h - kBottomBarH + 26, line2, kScale);

    // "+$12,345" floating over trains that have just been paid, for two game days.
    for (const sim::Train& t : world_.railway().trains()) {
        if (t.last_income_tick == 0 ||
            world_.total_ticks() - t.last_income_tick > 2 * static_cast<std::uint64_t>(sim::World::kTicksPerDay)) {
            continue;
        }
        float sx = 0, sy = 0;
        cam_.to_screen(world_.railway().train_position(t.id), sx, sy);
        const std::string label = "+" + format_money(t.last_income);
        glColor3f(0.0f, 0.0f, 0.0f);
        draw_text(sx - static_cast<float>(text_width(label, kScale)) / 2 + 1, sy - 25, label, kScale);
        glColor3f(0.4f, 1.0f, 0.4f);
        draw_text(sx - static_cast<float>(text_width(label, kScale)) / 2, sy - 26, label, kScale);
    }

    if (tool_ == Tool::Finance) draw_finance_panel(); // on top of everything on the map
    if (tool_ == Tool::Market) draw_market_panel();

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
