#include "tools.hpp"
#include "ui.hpp"

#include "font.hpp"

#include "railmaster/sim/freight.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace railmaster::client {

namespace {

sim::Frequency next_frequency(sim::Frequency f, bool back) {
    return static_cast<sim::Frequency>((static_cast<int>(f) + (back ? 3 : 1)) % 4);
}

// Names as written: the interface font has lower case (this once made
// everything capitals for the old pixel font).
std::string as_written(std::string s) {
    return s;
}


constexpr int kScale = 2;
constexpr float kTopBarH = 22.0f;
constexpr float kToolbarY = 26.0f;
constexpr float kButtonH = 22.0f;
constexpr float kBottomBarH = 44.0f;

const char* size_name(sim::StationSize s) {
    switch (s) {
    case sim::StationSize::Small: return "Small";
    case sim::StationSize::Medium: return "Medium";
    case sim::StationSize::Large: return "Large";
    }
    return "?";
}

const char* state_name(sim::TrainState s) {
    switch (s) {
    case sim::TrainState::Dwelling: return "at station";
    case sim::TrainState::Moving: return "moving";
    case sim::TrainState::Servicing: return "servicing";
    case sim::TrainState::BrokenDown: return "broken down";
    case sim::TrainState::NoRoute: return "no route";
    case sim::TrainState::Crashed: return "wrecked";
    case sim::TrainState::Retired: return "retired";
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

// Words laid into lines no wider than `width` points.
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

float Tools::draw_tabs(float x, float y, const std::vector<std::string>& labels, int chosen) const {
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const float w = static_cast<float>(text_width(labels[i], kScale)) + 18.0f;
        const bool on = static_cast<int>(i) == chosen;
        ui::tab(x, y, x + w, y + kButtonH, on);
        if (on) glColor3f(0.12f, 0.08f, 0.03f);
        else glColor3f(0.92f, 0.88f, 0.8f);
        draw_text(x + 9, y + 4, labels[i], kScale);
        tab_boxes_.push_back({x, y, x + w, y + kButtonH});
        x += w + 4.0f;
    }
    return y + kButtonH + 8.0f;
}

void Tools::open_ledger() {
    select(Tool::Ledger);
    ledger_tab_ = 1;
}

namespace {

std::string rate_text(std::int32_t bp) { return std::to_string(bp / 100) + "." + std::to_string(bp % 100 / 10) + "%"; }

// Rows of label and figures, the figures right-aligned at column edges.
struct Table {
    float x0, y;
    std::vector<float> edges; // right edges of the figure columns, from x0
    float row = 17.0f;

    void line(const std::string& label, const std::vector<std::string>& cells, float r = 0.9f, float g = 0.88f,
              float b = 0.82f) {
        glColor3f(r, g, b);
        draw_text(x0 + 14, y, label, kScale);
        for (std::size_t i = 0; i < cells.size() && i < edges.size(); ++i) {
            draw_text(x0 + edges[i] - static_cast<float>(text_width(cells[i], kScale)), y, cells[i], kScale);
        }
        y += row;
    }
    void heading(const std::string& label, const std::vector<std::string>& cells = {}) {
        y += 4;
        line(label, cells, 1.0f, 0.86f, 0.5f);
    }
    void gap() { y += row / 2; }
};

void money_line(Table& t, const std::string& label, const std::vector<sim::Money>& values, bool expense = false) {
    std::vector<std::string> cells;
    bool any = false;
    for (const sim::Money& m : values) {
        cells.push_back((expense && m != sim::Money{} ? "-" : "") + format_money(m));
        any |= m != sim::Money{};
    }
    if (expense && any) t.line(label, cells, 0.95f, 0.66f, 0.6f);
    else t.line(label, cells);
}

} // namespace

void Tools::draw_finance_panel() const {
    const sim::Company& co = world_.company();
    const auto& years = co.history();
    const sim::YearAccounts& now = years.back();
    const sim::YearAccounts* last = years.size() > 1 ? &years[years.size() - 2] : nullptr;

    const float x0 = 40, y0 = 56, w = 820;
    const float h = static_cast<float>(cam_.height_px) - kBottomBarH - y0 - 12;
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y0 + 8, as_written(co.name()) + (world_.player_company() ? "" : "   (you no longer run it)"), kScale + 1);
    float y = draw_tabs(x0 + 14, y0 + 36, {"Overview", "Bonds", "Stock", "Territories"}, company_tab_);
    ui::rule(x0 + 10, x0 + w - 10, y - 4);
    Table t{x0, y + 4, {560, 780}};

    switch (company_tab_) {
    case 0: {
        // Ledger lines with nothing on them this year or last are left out.
        t.heading("Income statement", {std::to_string(now.year) + " so far", last ? std::to_string(last->year) : ""});
        for (std::size_t i = 0; i < sim::kLedgerLines; ++i) {
            if (now.lines[i] == sim::Money{} && (!last || last->lines[i] == sim::Money{})) continue;
            const auto line = static_cast<sim::Ledger>(i);
            std::vector<sim::Money> v = {now.lines[i]};
            if (last) v.push_back(last->lines[i]);
            money_line(t, sim::ledger_name(line), v, !sim::is_revenue(line));
        }
        t.line("Revenue", {format_money(now.revenue()), last ? format_money(last->revenue()) : ""});
        t.line("Expenses", {format_money(now.expenses()), last ? format_money(last->expenses()) : ""}, 0.95f, 0.66f, 0.6f);
        t.line("Profit", {format_money(now.profit()), last ? format_money(last->profit()) : ""}, 1, 1, 1);
        t.line("Dividends paid", {format_money(now.dividends_paid), last ? format_money(last->dividends_paid) : ""});
        t.gap();
        t.heading("Invested");
        t.line("Track", {format_money(now.track_built), last ? format_money(last->track_built) : ""});
        t.line("Stations and buildings", {format_money(now.buildings_built), last ? format_money(last->buildings_built) : ""});
        t.line("Trains", {format_money(now.trains_bought), last ? format_money(last->trains_bought) : ""});
        t.line("Industries", {format_money(now.industries_bought), last ? format_money(last->industries_bought) : ""});
        if (now.acquisitions != sim::Money{} || (last && last->acquisitions != sim::Money{})) {
            t.line("Mergers", {format_money(now.acquisitions), last ? format_money(last->acquisitions) : ""});
        }
        t.gap();
        t.heading("Running the railroad");
        const sim::Sentiment mood = co.sentiment();
        const bool worried = mood == sim::Sentiment::Grumbling || mood == sim::Sentiment::Hostile;
        t.line("Investors", {sim::sentiment_name(mood)}, worried ? 0.95f : 0.9f, worried ? 0.66f : 0.88f, worried ? 0.6f : 0.82f);
        t.line("Five-year return to shareholders", {format_permille(co.weighted_return_permille())});
        t.line("Bad years in a row", {std::to_string(co.bad_year_streak())});
        // Why, and what comes next (rt3-clone-spec §12.2), in the investors' words.
        std::vector<std::string> reasons = co.investor_reasons();
        if (co.dividend_per_share() <= sim::Money{} && co.suggested_dividend() > sim::Money{}) {
            reasons.push_back("You pay no dividend: " + format_cents(co.suggested_dividend()) +
                              " a share would add to their return, and pay you as a shareholder (D, or the button below)");
        }
        for (const std::string& reason : reasons) {
            for (const std::string& l : wrap(reason, kScale, w - 40)) t.line("  " + l, {}, 0.72f, 0.7f, 0.64f);
        }
        t.line("Your salary", {format_money(sim::chairman_salary(co)) + " a year"});
        t.line("Average train speed", {std::to_string(world_.average_speed_mph(co.id())) + " mph"});
        t.line("Costs against normal", {std::to_string(world_.cost_percent()) + "%"});
        break;
    }
    case 1: {
        t.heading("Debt");
        t.line("Bonds outstanding", {format_money(co.debt())});
        t.line("Credit rating", {sim::rating_name(co.credit_rating())}, 1.0f, 0.95f, 0.75f);
        t.line("Prime rate", {rate_text(co.prime_rate_bp())});
        t.line("A new bond would pay", {rate_text(co.bond_rate_bp())});
        t.line("A bond's face value", {format_money(sim::Money::dollars(co.finance_balance().bond_face_value))});
        t.line("Interest this year", {format_money(now.lines[static_cast<std::size_t>(sim::Ledger::Interest)])});
        t.gap();
        t.heading("Bonds", {"Rate", "Issued"});
        t.edges = {560, 780};
        for (const sim::Bond& b : co.bonds()) t.line(format_money(b.principal), {rate_text(b.rate_bp), std::to_string(b.issued_year)});
        if (co.bonds().empty()) t.line("None", {}, 0.68f, 0.66f, 0.62f);
        t.gap();
        t.line("B issues a bond, R repays the dearest. K twice declares bankruptcy:", {}, 0.68f, 0.66f, 0.62f);
        t.line("half the bond debt is written off, and so is your credit.", {}, 0.68f, 0.66f, 0.62f);
        break;
    }
    case 2: {
        const sim::Investor& me = world_.investor();
        const auto pct = [](std::int64_t part, std::int64_t whole) {
            return whole > 0 ? std::to_string(part * 100 / whole) + "%" : std::string("-");
        };
        t.heading("Stock");
        t.line("Share price", {format_cents(co.share_price())}, 1, 1, 1);
        t.line("Shares outstanding", {format_count(co.shares_outstanding())});
        t.line("Market value", {format_money(co.share_price() * co.shares_outstanding())});
        t.line("Book value per share", {format_cents(co.book_value_per_share())});
        t.line("Dividend per share, a year", {format_cents(co.dividend_per_share())});
        if (const sim::Money s = co.suggested_dividend(); s > sim::Money{} && s != co.dividend_per_share()) {
            t.line("  Suggested: " + std::to_string(world_.data().balance.corporate.suggested_payout_percent) +
                       "% of the last 12 months' profit (D)",
                   {format_cents(s)}, 0.72f, 0.7f, 0.64f);
        }
        t.line("Dividends paid this year", {format_money(now.dividends_paid)});
        t.line("Stock issues this year", {std::to_string(co.stock_issues_this_year()) + " of 2"});
        t.gap();
        t.heading("You");
        const bool short_cash = me.cash < sim::Money{};
        t.line("Personal cash", {format_money(me.cash)}, short_cash ? 0.95f : 0.9f, short_cash ? 0.66f : 0.88f, short_cash ? 0.6f : 0.82f);
        const std::int64_t mine = me.shares_in(co.id());
        t.line("Shares held", {format_count(mine) + " (" + pct(mine, co.shares_outstanding()) + ")", format_money(co.share_price() * mine)});
        t.line("Purchasing power", {format_money(sim::purchasing_power(me, world_.market()))});
        t.line("Net worth", {format_money(sim::net_worth(me, world_.market()))}, 1, 1, 1);
        t.gap();
        t.line("A / S buy or sell shares, I issues new stock, Y buys it back, [ ] sets the dividend.", {}, 0.68f, 0.66f, 0.62f);
        break;
    }
    default: {
        const auto& terrs = world_.territories().territories;
        t.edges = {330, 450, 560, 660, 780};
        if (!terrs.empty()) t.heading("Territory", {"Access", "Stations", "Upkeep", "Credit", "Borders"});
        for (std::size_t i = 0; i < terrs.size(); ++i) {
            const sim::Territory& tr = terrs[i];
            const bool yours = world_.player_company() && co.has_access(static_cast<sim::TerritoryId>(i));
            const std::string access = tr.open() ? "open" : yours ? "yours" : format_money(tr.access_cost);
            const auto percent_text = [](std::int32_t p) {
                return p == 100 ? std::string("normal") : (p > 100 ? "+" : "") + std::to_string(p - 100) + "%";
            };
            add_row(x0 + 6, t.y - 2, x0 + w - 6, t.y + t.row - 3, RowPanel::Territories, i);
            if (i == territory_sel_) ui::selection(x0 + 6, t.y - 2, x0 + w - 6, t.y + t.row - 3);
            const float bright = i == territory_sel_ ? 1.0f : 0.9f;
            t.line(tr.name,
                   {access, percent_text(tr.station_cost_percent), percent_text(tr.overhead_percent),
                    tr.credit_grades == 0 ? std::string("-") : (tr.credit_grades > 0 ? "+" : "") + std::to_string(tr.credit_grades),
                    tr.closed_border ? "closed" : "open"},
                   bright, bright * (tr.open() || yours ? 0.98f : 0.85f), bright * (tr.open() || yours ? 0.9f : 0.7f));
        }
        if (terrs.empty()) t.line("This map has no territories: you may build anywhere.", {}, 0.68f, 0.66f, 0.62f);
        break;
    }
    }
    // The actions, for the mouse: what the keys do.
    const float by = y0 + h - 40;
    if (company_tab_ == 3) {
        ui::rule(x0 + 10, x0 + w - 10, by - 8);
        key_buttons(x0 + 10, x0 + 300, by, {{"Buy access", SDLK_t}});
    } else {
        // Two rows: bonds and stock, then the dividend.
        ui::rule(x0 + 10, x0 + w - 10, by - 42);
        key_buttons(x0 + 10, x0 + w - 10, by - 34,
                    {{"Issue bond", SDLK_b}, {"Repay bond", SDLK_r}, {"Buy shares", SDLK_a}, {"Sell shares", SDLK_s},
                     {"Issue stock", SDLK_i}, {"Buy back", SDLK_y}});
        const sim::Money suggested = co.suggested_dividend();
        key_buttons(x0 + 10, x0 + w - 10, by,
                    {{"Dividend -", SDLK_LEFTBRACKET}, {"Dividend +", SDLK_RIGHTBRACKET},
                     {suggested > sim::Money{} ? "Suggested dividend: " + format_cents(suggested) : std::string("Suggested dividend"), SDLK_d}});
    }
}

void Tools::key_buttons(float x0, float x1, float y, const std::vector<KeyButton>& buttons) const {
    constexpr float kGap = 6, kH = 26;
    const float bw = (x1 - x0 - kGap * static_cast<float>(buttons.size() - 1)) / static_cast<float>(buttons.size());
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const float bx = x0 + static_cast<float>(i) * (bw + kGap);
        ui::tab(bx, y, bx + bw, y + kH, false);
        glColor3f(0.95f, 0.92f, 0.82f);
        draw_text(bx + (bw - static_cast<float>(text_width(buttons[i].label, kScale))) / 2, y + 5, buttons[i].label, kScale);
        add_row(bx, y, bx + bw, y + kH, RowPanel::KeyButton, static_cast<std::size_t>(buttons[i].key));
    }
}

void Tools::draw_ledger_panel() const {
    const sim::Company& co = world_.company();
    const auto& years = co.history();
    const float x0 = 40, y0 = 56, w = std::min(1180.0f, static_cast<float>(cam_.width_px) - 80);
    const float h = static_cast<float>(cam_.height_px) - kBottomBarH - y0 - 12;
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y0 + 8, "Ledger: " + as_written(co.name()), kScale + 1);
    float y = draw_tabs(x0 + 14, y0 + 36, {"Overview", "Income statement", "Balance sheets", "Haulage", "Players", "Trains", "Stations", "Industries", "Cargo"},
                        ledger_tab_);
    ui::rule(x0 + 10, x0 + w - 10, y - 4);
    Table t{x0, y + 4, {}};
    const auto& cargo = world_.data().cargo.all();

    switch (ledger_tab_) {
    case 0: {
        // RT3's Company Overview: revenue and profit, haulage, speed, and
        // how the investors and the chairman are doing.
        const sim::YearAccounts& now = years.back();
        const sim::YearAccounts* last = years.size() > 1 ? &years[years.size() - 2] : nullptr;
        t.edges = {560, 760};
        t.heading("", {std::to_string(now.year) + " so far", last ? std::to_string(last->year) : ""});
        t.line("Revenue", {format_money(now.revenue()), last ? format_money(last->revenue()) : ""});
        t.line("Profit", {format_money(now.profit()), last ? format_money(last->profit()) : ""}, 1, 1, 1);
        t.gap();
        std::int64_t loads_milli = 0;
        for (const sim::CargoType& c : cargo) loads_milli += co.delivered_milli(c.id);
        sim::Money haul_revenue;
        for (const sim::YearAccounts& ya : years) {
            for (const sim::Ledger l : {sim::Ledger::FreightRevenue, sim::Ledger::PassengerRevenue, sim::Ledger::MailRevenue,
                                        sim::Ledger::TroopRevenue}) {
                haul_revenue += ya.lines[static_cast<std::size_t>(l)];
            }
        }
        t.edges = {560};
        t.line("Loads delivered since founding", {format_count(loads_milli / sim::kMilli)});
        t.line("Revenue a load", {loads_milli > 0 ? format_money(haul_revenue.scaled(sim::kMilli, loads_milli)) : "-"});
        t.line("Average train speed, last 12 months", {std::to_string(world_.average_speed_mph(co.id())) + " mph"});
        std::size_t trains = 0, stations = 0;
        for (const sim::Train& tr : world_.railway().trains()) trains += tr.in_service() && tr.owner == co.id();
        for (const sim::Station& s : world_.railway().stations()) stations += s.owner == co.id();
        t.line("Trains", {std::to_string(trains)});
        t.line("Stations", {std::to_string(stations)});
        t.gap();
        t.line("Investors", {sim::sentiment_name(co.sentiment())});
        t.line("Chairman's salary", {format_money(sim::chairman_salary(co)) + " a year"});
        t.line("Credit rating", {sim::rating_name(co.credit_rating())});
        t.line("Share price", {format_cents(co.share_price())});
        t.line("Book value", {format_money(co.book_value())});
        // Revenue and profit year by year, as bars: the last ten years, this
        // one so far on the right.
        if (w >= 1100) {
            const float cx0 = x0 + 800, cx1 = x0 + w - 20, cy0 = y + 8, cy1 = y + 300;
            glColor3f(1.0f, 0.86f, 0.5f);
            draw_text(cx0, cy0, "Revenue and profit, by year", kScale);
            const std::size_t count = std::min<std::size_t>(10, years.size());
            const std::size_t first = years.size() - count;
            double top = 1, bottom = 0; // dollars, bottom <= 0
            for (std::size_t i = first; i < years.size(); ++i) {
                top = std::max({top, static_cast<double>(years[i].revenue().whole_dollars()),
                                static_cast<double>(years[i].profit().whole_dollars())});
                bottom = std::min(bottom, static_cast<double>(years[i].profit().whole_dollars()));
            }
            const float py0 = cy0 + 28, py1 = cy1 - 22; // the plot
            const auto at = [&](double dollars) {
                return py1 - static_cast<float>((dollars - bottom) / (top - bottom)) * (py1 - py0);
            };
            const float zero = at(0);
            const float slot = (cx1 - cx0) / static_cast<float>(std::max<std::size_t>(count, 1));
            for (std::size_t i = 0; i < count; ++i) {
                const sim::YearAccounts& ya = years[first + i];
                const float sx = cx0 + slot * static_cast<float>(i);
                const float bw = slot * 0.36f;
                const float rev = at(static_cast<double>(ya.revenue().whole_dollars()));
                const double profit = static_cast<double>(ya.profit().whole_dollars());
                const float pro = at(profit);
                const float alpha = first + i + 1 == years.size() ? 0.6f : 0.95f; // this year is not over
                fill_rect(sx + slot * 0.1f, std::min(rev, zero), sx + slot * 0.1f + bw, std::max(rev, zero), 0.95f, 0.78f, 0.3f, alpha);
                if (profit >= 0) fill_rect(sx + slot * 0.1f + bw, std::min(pro, zero), sx + slot * 0.1f + 2 * bw, std::max(pro, zero), 0.35f, 0.85f, 0.4f, alpha);
                else fill_rect(sx + slot * 0.1f + bw, std::min(pro, zero), sx + slot * 0.1f + 2 * bw, std::max(pro, zero), 0.95f, 0.35f, 0.3f, alpha);
                glColor3f(0.75f, 0.73f, 0.67f);
                const std::int32_t yy = ya.year % 100;
                draw_text(sx + slot * 0.1f, py1 + 4, (yy < 10 ? "'0" : "'") + std::to_string(yy), kScale);
            }
            fill_rect(cx0, zero, cx1, zero + 1, 0.8f, 0.78f, 0.7f, 0.8f); // the zero line
            glColor3f(0.75f, 0.73f, 0.67f);
            const std::string scale = format_money(sim::Money::dollars(static_cast<std::int64_t>(top)));
            draw_text(cx1 - static_cast<float>(text_width(scale, kScale)), cy0, scale, kScale);
            fill_rect(cx0, cy1 + 4, cx0 + 12, cy1 + 16, 0.95f, 0.78f, 0.3f, 0.95f);
            glColor3f(0.85f, 0.83f, 0.77f);
            draw_text(cx0 + 16, cy1 + 2, "revenue", kScale);
            fill_rect(cx0 + 100, cy1 + 4, cx0 + 112, cy1 + 16, 0.35f, 0.85f, 0.4f, 0.95f);
            draw_text(cx0 + 116, cy1 + 2, "profit", kScale);
        }
        break;
    }
    case 1: {
        // Ten years side by side, the latest on the right (§12.7 [C]).
        const std::size_t shown = std::min<std::size_t>(years.size(), static_cast<std::size_t>(std::max(1.0f, (w - 300) / 86)));
        const std::size_t first = std::min<std::size_t>(years.size() - shown, years.size() > 10 ? years.size() - 10 : 0);
        const std::size_t count = std::min<std::size_t>(shown, 10);
        std::vector<std::string> head;
        for (std::size_t i = 0; i < count; ++i) {
            t.edges.push_back(300 + 86.0f * static_cast<float>(i + 1));
            head.push_back(std::to_string(years[first + i].year) + (first + i + 1 == years.size() ? "*" : ""));
        }
        t.heading("", head);
        const auto row = [&](const std::string& label, auto&& value, bool expense) {
            std::vector<sim::Money> v;
            bool any = false;
            for (std::size_t i = 0; i < count; ++i) {
                v.push_back(value(years[first + i]));
                any |= v.back() != sim::Money{};
            }
            if (any) money_line(t, label, v, expense);
        };
        for (std::size_t i = 0; i < sim::kLedgerLines; ++i) {
            const auto line = static_cast<sim::Ledger>(i);
            row(sim::ledger_name(line), [&](const sim::YearAccounts& ya) { return ya.lines[i]; }, !sim::is_revenue(line));
        }
        std::vector<std::string> profit;
        for (std::size_t i = 0; i < count; ++i) profit.push_back(format_money(years[first + i].profit()));
        t.line("Net profit", profit, 1, 1, 1);
        row("Dividends", [](const sim::YearAccounts& ya) { return ya.dividends_paid; }, false);
        t.gap();
        row("Invested in track", [](const sim::YearAccounts& ya) { return ya.track_built; }, false);
        row("Invested in buildings", [](const sim::YearAccounts& ya) { return ya.buildings_built; }, false);
        row("Invested in trains", [](const sim::YearAccounts& ya) { return ya.trains_bought; }, false);
        row("Invested in industries", [](const sim::YearAccounts& ya) { return ya.industries_bought; }, false);
        row("Spent on mergers", [](const sim::YearAccounts& ya) { return ya.acquisitions; }, false);
        t.gap();
        t.line("* this year so far", {}, 0.68f, 0.66f, 0.62f);
        break;
    }
    case 2: {
        // Every company's balance sheet, side by side.
        std::vector<const sim::Company*> cos;
        for (const sim::Company& c : world_.companies()) {
            if (!c.defunct()) cos.push_back(&c);
        }
        const std::size_t n = std::min<std::size_t>(cos.size(), static_cast<std::size_t>((w - 260) / 150));
        std::vector<std::string> head;
        for (std::size_t i = 0; i < n; ++i) {
            t.edges.push_back(260 + 150.0f * static_cast<float>(i + 1));
            std::string name = as_written(cos[i]->name());
            while (name.size() > 3 && text_width(name, kScale) > 140) name = name.substr(0, name.size() - 4) + "...";
            head.push_back(name);
        }
        t.heading("", head);
        const auto row = [&](const std::string& label, auto&& value, bool expense = false) {
            std::vector<sim::Money> v;
            for (std::size_t i = 0; i < n; ++i) v.push_back(value(*cos[i]));
            money_line(t, label, v, expense);
        };
        row("Cash", [](const sim::Company& c) { return c.cash(); });
        row("Track", [](const sim::Company& c) { return c.track_value(); });
        row("Stations and buildings", [](const sim::Company& c) { return c.building_value(); });
        row("Trains", [](const sim::Company& c) { return c.rolling_stock_value(); });
        row("Industries", [](const sim::Company& c) { return c.industry_value(); });
        row("Bonds", [](const sim::Company& c) { return c.debt(); }, true);
        std::vector<std::string> book;
        for (std::size_t i = 0; i < n; ++i) book.push_back(format_money(cos[i]->book_value()));
        t.line("Book value", book, 1, 1, 1);
        t.gap();
        std::vector<std::string> per_share, price, rating;
        for (std::size_t i = 0; i < n; ++i) {
            per_share.push_back(format_cents(cos[i]->book_value_per_share()));
            price.push_back(format_cents(cos[i]->share_price()));
            rating.push_back(sim::rating_name(cos[i]->credit_rating()));
        }
        t.line("Book value per share", per_share);
        t.line("Share price", price);
        t.line("Credit rating", rating);
        break;
    }
    case 3: {
        // What the company has hauled, by cargo, since it was founded.
        t.edges = {380, 470};
        t.heading("Cargo", {"Loads delivered", "Share"});
        std::int64_t total = 0;
        for (const sim::CargoType& c : cargo) total += co.delivered_milli(c.id);
        std::vector<std::pair<std::int64_t, const sim::CargoType*>> hauled;
        for (const sim::CargoType& c : cargo) {
            if (co.delivered_milli(c.id) > 0) hauled.emplace_back(co.delivered_milli(c.id), &c);
        }
        std::sort(hauled.begin(), hauled.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [milli, c] : hauled) {
            t.line(c->name, {format_count((milli + sim::kMilli / 2) / sim::kMilli), std::to_string(milli * 100 / total) + "%"});
            // A bar for the share.
            const float bx = x0 + 490, bw = std::max(2.0f, 300.0f * static_cast<float>(milli) / static_cast<float>(total));
            fill_rect(bx, t.y - t.row + 4, bx + bw, t.y - t.row + 12, 0.85f, 0.7f, 0.35f, 0.85f);
        }
        if (hauled.empty()) t.line("Nothing delivered yet", {}, 0.68f, 0.66f, 0.62f);
        else t.line("All cargo", {format_count((total + sim::kMilli / 2) / sim::kMilli), "100%"}, 1, 1, 1);
        break;
    }
    case 4: {
        // Every player: cash, shares at market, net worth, and the company they run.
        t.edges = {560, 720, 880, 1040};
        t.heading("Player", {"Cash", "Shares", "Net worth", "Runs"});
        for (const sim::Investor& inv : world_.investors()) {
            const bool you = &inv == &world_.investor();
            t.line(as_written(inv.name) + (you && inv.name != "You" ? " (you)" : ""),
                   {format_money(inv.cash), format_money(sim::holdings_value(inv, world_.market())),
                    format_money(sim::net_worth(inv, world_.market())),
                    inv.chairs ? as_written(world_.company(*inv.chairs).name()) : std::string("-")},
                   you ? 1.0f : 0.9f, you ? 0.95f : 0.88f, you ? 0.7f : 0.82f);
        }
        break;
    }
    default: {
        // The lists (§12.7 sub-reports): one row per train, station,
        // industry or cargo, the chosen one highlighted; Enter opens it.
        const std::vector<std::uint32_t> items = ledger_items();
        const sim::Railway& rw = world_.railway();
        const sim::Economy& eco = world_.economy();
        const std::int32_t today = world_.date().days_since_epoch();
        const auto loads = [](std::int64_t milli) { return format_count((milli + sim::kMilli / 2) / sim::kMilli); };
        std::vector<std::string> head;
        std::string first;
        switch (ledger_tab_) {
        case 5:
            t.edges = {440, 500, 630, 760, 890, 1020, 1150};
            first = "Train";
            head = {"Age", "Stops", "Freight", "Passengers", "Costs", "Profit", "Last year"};
            break;
        case 6:
            t.edges = {420, 600, 700, 800, 960, 1120};
            first = "Station";
            head = {"Size", "Town", "Waiting", "Trains", "This month", "12 months"};
            break;
        case 7:
            t.edges = {420, 520, 680, 840, 1000};
            first = "Industry";
            head = {"Level", "Running", "Made", "Profit a year", "Paid"};
            break;
        default:
            t.edges = {420, 560, 720, 880};
            first = "Cargo";
            head = {"Since", "Base price", "Waiting", "Delivered"};
            break;
        }
        t.heading(first, head);
        if (items.empty()) {
            const char* none[] = {"You have no trains yet (F6 to buy one)", "You have no stations yet (F3 to build one)",
                                  "You own no industries (F9 to buy or build one)", "No cargo yet"};
            t.line(none[ledger_tab_ - 5], {}, 0.68f, 0.66f, 0.62f);
            break;
        }
        const float bottom = y0 + h - 30;
        const auto rows_shown = static_cast<std::size_t>(std::max(1.0f, (bottom - t.y) / t.row));
        const std::size_t sel = std::min(ledger_sel_, items.size() - 1);
        const std::size_t from = sel >= rows_shown ? sel - rows_shown + 1 : 0;
        std::vector<std::int64_t> waiting(world_.data().cargo.all().size(), 0);
        for (const sim::Station& s : rw.stations()) {
            if (s.owner != co.id()) continue;
            for (std::size_t c = 0; c < s.waiting.size() && c < waiting.size(); ++c) waiting[c] += s.waiting[c].milli;
            for (const sim::ExpressWaiting& e : s.express) waiting[e.cargo] += e.milli;
        }
        for (std::size_t i = from; i < items.size() && t.y < bottom; ++i) {
            const std::uint32_t id = items[i];
            add_row(x0 + 6, t.y - 2, x0 + w - 6, t.y + t.row - 3, RowPanel::Ledger, i);
            if (i == sel) ui::selection(x0 + 6, t.y - 2, x0 + w - 6, t.y + t.row - 3);
            const float b = i == sel ? 1.0f : 0.9f;
            std::vector<std::string> cells;
            std::string label;
            switch (ledger_tab_) {
            case 5: {
                const sim::Train& tr = rw.train(id);
                const sim::TrainAccounts& a = tr.accounts;
                label = "Train " + std::to_string(tr.id + 1) + "   " + as_written(world_.data().locomotives.get(tr.loco).name);
                const std::int32_t age = (today - tr.built_day) / 365;
                cells = {std::to_string(age), std::to_string(tr.route.size()), format_money(a.freight), format_money(a.express),
                         format_money(a.costs()), format_money(a.profit()),
                         sim::Date::from_days(tr.built_day).year() < world_.date().year() ? format_money(tr.accounts_last_year.profit())
                                                                                          : std::string("-")};
                break;
            }
            case 6: {
                const sim::Station& s = rw.station(id);
                label = as_written(s.name);
                std::int64_t milli = 0;
                for (const sim::WaitingCargo& wc : s.waiting) milli += wc.milli;
                for (const sim::ExpressWaiting& e : s.express) milli += e.milli;
                std::size_t calling = 0;
                for (const sim::Train& tr : rw.trains()) {
                    calling += tr.in_service() && std::find(tr.route.begin(), tr.route.end(), s.id) != tr.route.end();
                }
                cells = {size_name(s.size), s.town ? as_written(eco.towns()[*s.town].name) : std::string("-"), loads(milli),
                         std::to_string(calling), format_money(s.income_this_month), format_money(s.income_last_year())};
                break;
            }
            case 7: {
                const sim::Site& s = eco.sites()[id];
                label = as_written(world_.data().industries.get(s.type).name);
                std::int64_t made = 0;
                for (const std::int64_t m : s.made_milli) made += m;
                cells = {std::to_string(s.level), s.closed ? std::string("closed") : std::to_string(s.utilisation_permille / 10) + "%",
                         loads(made), format_money(sim::annual_profit(s)), format_money(s.owner_paid)};
                break;
            }
            default: {
                const sim::CargoType& c = world_.data().cargo.get(static_cast<sim::CargoId>(id));
                label = c.name;
                const bool express = c.cargo_class == sim::CargoClass::Express;
                cells = {std::to_string(c.available_year), express ? std::string("fare") : format_money(c.base_price),
                         loads(waiting[id]), loads(co.delivered_milli(static_cast<std::uint16_t>(id)))};
                break;
            }
            }
            t.line(label, cells, b, b * (i == sel ? 0.95f : 0.98f), b * (i == sel ? 0.7f : 0.92f));
        }
        glColor3f(0.68f, 0.66f, 0.62f);
        std::string foot = std::to_string(sel + 1) + " of " + std::to_string(items.size()) + "   Up/Down choose   Enter ";
        foot += ledger_tab_ == 8 ? "shows its price map" : "opens it on the map";
        if (ledger_tab_ == 5) foot += "   Figures are this year's so far";
        draw_text(x0 + 14, y0 + h - 26, foot, kScale);
        break;
    }
    }
}

std::vector<std::uint32_t> Tools::ledger_items() const {
    std::vector<std::uint32_t> out;
    const sim::CompanyId me = world_.company().id();
    switch (ledger_tab_) {
    case 5: {
        // Your trains, the most profitable this year first.
        for (const sim::Train& t : world_.railway().trains()) {
            if (t.in_service() && t.owner == me) out.push_back(t.id);
        }
        std::stable_sort(out.begin(), out.end(), [&](std::uint32_t a, std::uint32_t b) {
            return world_.railway().train(a).accounts.profit() > world_.railway().train(b).accounts.profit();
        });
        break;
    }
    case 6: {
        // Your stations, the best earners over the last 12 months first.
        for (const sim::Station& s : world_.railway().stations()) {
            if (s.owner == me) out.push_back(s.id);
        }
        std::stable_sort(out.begin(), out.end(), [&](std::uint32_t a, std::uint32_t b) {
            return world_.railway().station(a).income_last_year() > world_.railway().station(b).income_last_year();
        });
        break;
    }
    case 7:
        for (const sim::Site& s : world_.economy().sites()) {
            if (s.owner && *s.owner == me) out.push_back(s.id);
        }
        break;
    case 8:
        for (const sim::CargoType& c : world_.data().cargo.all()) {
            if (c.available_year <= world_.date().year()) out.push_back(c.id);
        }
        break;
    default: break;
    }
    return out;
}

bool Tools::ledger_key(SDL_Keycode key) {
    const std::vector<std::uint32_t> items = ledger_items();
    if (items.empty()) return false;
    const std::size_t n = items.size();
    ledger_sel_ = std::min(ledger_sel_, n - 1);
    switch (key) {
    case SDLK_UP: ledger_sel_ = (ledger_sel_ + n - 1) % n; return true;
    case SDLK_DOWN: ledger_sel_ = (ledger_sel_ + 1) % n; return true;
    case SDLK_PAGEUP: ledger_sel_ = ledger_sel_ > 20 ? ledger_sel_ - 20 : 0; return true;
    case SDLK_PAGEDOWN: ledger_sel_ = std::min(n - 1, ledger_sel_ + 20); return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
        const std::uint32_t id = items[ledger_sel_];
        const sim::Railway& rw = world_.railway();
        if (ledger_tab_ == 8) {
            // The cargo's price map.
            map_overlay_ = MapOverlay::Cargo;
            overlay_ = static_cast<sim::CargoId>(id);
            select(Tool::Inspect);
            return true;
        }
        const int tab = ledger_tab_;
        select(Tool::Inspect);
        if (tab == 5) {
            jump_ = rw.train_position(id);
            open_detail(DetailKind::Train, id);
        } else if (tab == 6) {
            jump_ = rw.track().node(rw.station(id).node).pos;
            open_detail(DetailKind::Station, id);
        } else {
            const sim::Site& s = world_.economy().sites()[id];
            jump_ = world_.economy().node_centre(s.cx, s.cy);
            open_detail(DetailKind::Industry, id);
        }
        hover_ = *jump_;
        return true;
    }
    default: return false;
    }
}

std::string save_summary(const std::string& path) {
    std::ifstream in(path + ".txt");
    std::string line;
    if (std::getline(in, line)) return line;
    return std::ifstream(path) ? "A saved game" : "Empty";
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
    // A train's route being planned is kept, to come back to (say, after
    // electrifying its track); only the track being laid is let go.
    track_start_.reset();
    // Leaving the Track tool ends free undo [D].
    if (tool_ == Tool::Track && t != Tool::Track) world_.execute(sim::EndTrackUndo{});
    // Likewise stations and buildings, on leaving the Station, Tower and Shed tools.
    const auto builds = [](Tool x) { return x == Tool::Station || x == Tool::ServiceTower || x == Tool::Maintenance; };
    if (builds(tool_) && !builds(t)) world_.execute(sim::EndBuildUndo{});
    if (t != Tool::Inspect) {
        detail_.reset();
        follow_ = false;
    }
    tool_ = t;
}

void Tools::cancel() {
    track_start_.reset();
    stops_.clear();
    stop_sel_ = 0;
    editing_.reset();
}

void Tools::show(std::string message, bool good) {
    message_ = std::move(message);
    message_good_ = good;
    message_until_ = SDL_GetTicks() + 4000;
}

void Tools::check_trains() {
    const auto me = world_.player_company();
    if (!me) return;
    const auto& trains = world_.railway().trains();
    // Payments since last frame float up from the train.
    if (last_revenue_.size() < trains.size()) {
        const std::size_t old = last_revenue_.size();
        last_revenue_.resize(trains.size());
        for (std::size_t i = old; i < trains.size(); ++i) last_revenue_[i] = trains[i].revenue;
    }
    for (const sim::Train& t : trains) {
        const sim::Money paid = t.revenue - last_revenue_[t.id];
        last_revenue_[t.id] = t.revenue;
        if (t.owner != *me || paid < sim::Money::dollars(1)) continue;
        floaters_.push_back({world_.railway().train_position(t.id), "+" + format_money(paid), SDL_GetTicks()});
    }
    std::erase_if(floaters_, [](const Floater& f) { return SDL_GetTicks() - f.born > 2600; });
    if (dry_warned_.size() < trains.size()) dry_warned_.resize(trains.size(), false);
    for (const sim::Train& t : trains) {
        if (t.owner != *me || !t.in_service() || world_.data().locomotives.get(t.loco).fuel != sim::Fuel::Steam) continue;
        if (t.water > 0) {
            dry_warned_[t.id] = false;
            continue;
        }
        if (dry_warned_[t.id]) continue;
        dry_warned_[t.id] = true;
        const std::string text = "Train " + std::to_string(t.id + 1) +
                                 " is out of water and crawls: build a service tower (F4) on its line, by a station";
        show(text, false);
        message_until_ = SDL_GetTicks() + 9000;
        log(text, false);
    }
}

void Tools::log(const std::string& text, bool good) {
    log_.push_back({world_.date().month_year_label(), text, good});
    if (log_.size() > 500) log_.erase(log_.begin());
}

void Tools::open_newspaper(const std::string& headline, std::vector<std::string> more) {
    paper_ = Paper{world_.date().month_year_label(), headline, std::move(more)};
    paper_open_ = true;
}

namespace {


} // namespace

void Tools::draw_log_panel() const {
    constexpr float kRow = 18.0f;
    const float x0 = 40, y0 = 56, w = std::min(900.0f, static_cast<float>(cam_.width_px) - 80);
    // As tall as the messages need, up to the bottom bar.
    float need = 60;
    for (std::size_t i = log_scroll_; i < log_.size() && need < 2000; ++i) {
        need += kRow * static_cast<float>(wrap(log_[log_.size() - 1 - i].text, kScale, w - 170).size()) + 4;
    }
    const float h = std::min(std::max(need, 90.0f), static_cast<float>(cam_.height_px) - kBottomBarH - y0 - 12);
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y0 + 8, "Messages", kScale + 1);
    glColor3f(0.68f, 0.66f, 0.62f);
    const std::string keys = "Up/Down, PgUp/PgDn scroll   M or Esc close";
    draw_text(x0 + w - 14 - static_cast<float>(text_width(keys, kScale)), y0 + 12, keys, kScale);
    ui::rule(x0 + 10, x0 + w - 10, y0 + 36);
    float y = y0 + 44;
    if (log_.empty()) {
        glColor3f(0.68f, 0.66f, 0.62f);
        draw_text(x0 + 14, y, "No news yet", kScale);
        return;
    }
    // Newest first, each with the month it came.
    for (std::size_t i = log_scroll_; i < log_.size(); ++i) {
        const LogEntry& e = log_[log_.size() - 1 - i];
        const auto lines = wrap(e.text, kScale, w - 170);
        if (y + kRow * static_cast<float>(lines.size()) > y0 + h - 8) break;
        glColor3f(0.75f, 0.7f, 0.6f);
        draw_text(x0 + 14, y, e.when, kScale);
        if (e.good) glColor3f(0.9f, 0.88f, 0.82f);
        else glColor3f(1.0f, 0.6f, 0.52f);
        for (const std::string& l : lines) {
            draw_text(x0 + 150, y, l, kScale);
            y += kRow;
        }
        y += 4;
    }
}

std::vector<Tools::FindHit> Tools::find_hits() const {
    // Names holding the typed text, ignoring case; those starting with it first.
    const auto lower = [](std::string t) {
        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const std::string want = lower(find_text_);
    std::vector<std::pair<int, FindHit>> found;
    const auto consider = [&](const std::string& name, const std::string& kind, sim::MapPoint at, std::optional<sim::StationId> st) {
        const std::size_t pos = lower(name).find(want);
        if (pos == std::string::npos) return;
        found.push_back({pos == 0 ? 0 : 1, FindHit{name, kind, at, st}});
    };
    const sim::Economy& eco = world_.economy();
    for (const sim::Town& t : eco.towns()) consider(as_written(t.name), "town", eco.node_centre(t.cx, t.cy), std::nullopt);
    const sim::Railway& rw = world_.railway();
    for (const sim::Station& s : rw.stations()) {
        const std::string kind = s.owner == world_.company().id() ? std::string("your station")
                                                                    : as_written(world_.company(s.owner).name()) + " station";
        consider(as_written(s.name), kind, rw.track().node(s.node).pos, s.id);
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.name < b.second.name;
    });
    std::vector<FindHit> hits;
    for (auto& f : found) hits.push_back(std::move(f.second));
    return hits;
}

bool Tools::find_key(SDL_Keycode key) {
    const std::vector<FindHit> hits = find_hits();
    switch (key) {
    case SDLK_ESCAPE: find_open_ = false; return true;
    case SDLK_BACKSPACE:
        if (!find_text_.empty()) find_text_.pop_back();
        find_sel_ = 0;
        return true;
    case SDLK_UP: find_sel_ = find_sel_ > 0 ? find_sel_ - 1 : 0; return true;
    case SDLK_DOWN: find_sel_ = hits.empty() ? 0 : std::min(hits.size() - 1, find_sel_ + 1); return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (find_sel_ < hits.size()) {
            const FindHit& h = hits[find_sel_];
            jump_ = h.at;
            hover_ = h.at;
            if (h.station) {
                select(Tool::Inspect);
                open_detail(DetailKind::Station, *h.station);
            } else {
                detail_.reset(); // a town: whatever was open no longer applies
                follow_ = false;
            }
            show(h.name, true);
            find_open_ = false;
        }
        return true;
    default: return true; // letters arrive as text
    }
}

namespace {

// The debug console's cheats, each on its own key (rt3-clone-spec §15.8).
struct ConsoleCheat {
    SDL_Keycode key;
    const char* label;
    sim::CheatKind kind;
    std::int64_t dollars;
    std::int32_t tier;
};
constexpr ConsoleCheat kCheats[] = {
    {SDLK_1, "$1,000,000 to you", sim::CheatKind::PlayerMoney, 1'000'000, 0},
    {SDLK_2, "$10,000,000 to you", sim::CheatKind::PlayerMoney, 10'000'000, 0},
    {SDLK_3, "$1,000,000 to your company", sim::CheatKind::CompanyMoney, 1'000'000, 0},
    {SDLK_4, "$10,000,000 to your company", sim::CheatKind::CompanyMoney, 10'000'000, 0},
    {SDLK_5, "Access to every territory", sim::CheatKind::AllTerritories, 0, 0},
    {SDLK_6, "Every engine available", sim::CheatKind::AllEngines, 0, 0},
    {SDLK_7, "Twice the production, for good", sim::CheatKind::DoubleProduction, 0, 0},
    {SDLK_8, "Twice the train speed", sim::CheatKind::DoubleSpeed, 0, 0},
    {SDLK_9, "No crashes", sim::CheatKind::NoCrashes, 0, 0},
    {SDLK_k, "Break down the train shown", sim::CheatKind::BreakDown, 0, 0},
    {SDLK_c, "Crash the train shown", sim::CheatKind::Crash, 0, 0},
    {SDLK_b, "Win the scenario: Bronze", sim::CheatKind::ForceWin, 0, 0},
    {SDLK_s, "Win the scenario: Silver", sim::CheatKind::ForceWin, 0, 1},
    {SDLK_g, "Win the scenario: Gold", sim::CheatKind::ForceWin, 0, 2},
    {SDLK_l, "Lose the scenario", sim::CheatKind::ForceLoss, 0, 0},
};

} // namespace

bool Tools::console_key(SDL_Keycode key) {
    if (key == SDLK_ESCAPE || key == SDLK_PERIOD) {
        console_open_ = false;
        return true;
    }
    const auto c = std::find_if(std::begin(kCheats), std::end(kCheats), [&](const ConsoleCheat& x) { return x.key == key; });
    if (c == std::end(kCheats)) return true;
    sim::Cheat cmd{.kind = c->kind, .amount = sim::Money::dollars(c->dollars), .train = 0, .tier = c->tier};
    if (c->kind == sim::CheatKind::BreakDown || c->kind == sim::CheatKind::Crash) {
        const auto id = detail_train();
        if (!id) {
            show("Cannot: open a train's panel first (click it in the Look tool)", false);
            return true;
        }
        cmd.train = *id;
    }
    const sim::CommandResult r = world_.execute(cmd);
    if (!r.ok) {
        show("Cannot: " + r.error, false);
        return true;
    }
    std::string done = c->label;
    if (c->kind == sim::CheatKind::DoubleSpeed) done += world_.railway().rules().speed_percent > 100 ? ": on" : ": off";
    if (c->kind == sim::CheatKind::NoCrashes) done += world_.railway().rules().crashes ? ": off" : ": on";
    show("Cheat: " + done + "  (this game is now marked as assisted)", true);
    return true;
}

void Tools::draw_console() const {
    constexpr float kRow = 21.0f;
    constexpr auto kCount = static_cast<float>(std::size(kCheats));
    const float w = 460, x0 = static_cast<float>(cam_.width_px) - w - 16, y0 = 56;
    const float h = 84 + kRow * kCount;
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y0 + 8, "Debug console", kScale + 1);
    glColor3f(0.95f, 0.65f, 0.6f);
    const std::string note = world_.assisted() ? "assisted game" : "marks the game assisted";
    draw_text(x0 + w - 14 - static_cast<float>(text_width(note, kScale)), y0 + 12, note, kScale);
    ui::rule(x0 + 10, x0 + w - 10, y0 + 36);
    float y = y0 + 44;
    const sim::OperatingRules& rules = world_.railway().rules();
    for (const ConsoleCheat& c : kCheats) {
        add_row(x0 + 8, y - 3, x0 + w - 8, y + kRow - 3, RowPanel::Console, static_cast<std::size_t>(&c - kCheats));
        glColor3f(1.0f, 0.86f, 0.5f);
        draw_text(x0 + 18, y, std::string(1, static_cast<char>(std::toupper(static_cast<int>(c.key)))), kScale);
        glColor3f(0.9f, 0.88f, 0.82f);
        draw_text(x0 + 44, y, c.label, kScale);
        std::string state;
        if (c.kind == sim::CheatKind::DoubleSpeed) state = rules.speed_percent > 100 ? "On" : "Off";
        if (c.kind == sim::CheatKind::NoCrashes) state = rules.crashes ? "Off" : "On";
        if (!state.empty()) {
            if (state == "On") glColor3f(0.6f, 0.95f, 0.6f);
            else glColor3f(0.68f, 0.66f, 0.62f);
            draw_text(x0 + w - 18 - static_cast<float>(text_width(state, kScale)), y, state, kScale);
        }
        y += kRow;
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 14, y0 + h - 26, "A key uses its cheat   . or Esc close", kScale);
}

bool Tools::click_row(float sx, float sy) {
    const auto hit = std::find_if(row_boxes_.rbegin(), row_boxes_.rend(), [&](const RowBox& r) {
        return sx >= r.x0 && sx < r.x1 && sy >= r.y0 && sy < r.y1;
    });
    if (hit == row_boxes_.rend()) return false;
    const std::size_t i = hit->index;
    switch (hit->panel) {
    case RowPanel::Find:
        if (find_sel_ == i) find_key(SDLK_RETURN);
        else find_sel_ = i;
        break;
    case RowPanel::Console: console_key(kCheats[i].key); break;
    case RowPanel::Lists:
        if (list_sel_ == i) lists_key(SDLK_RETURN);
        else list_sel_ = i;
        break;
    case RowPanel::Ledger:
        if (ledger_sel_ == i) ledger_key(SDLK_RETURN);
        else ledger_sel_ = i;
        break;
    case RowPanel::Territories: territory_sel_ = i; break;
    case RowPanel::Market: market_choice_ = static_cast<sim::CompanyId>(i); break;
    case RowPanel::Guide:
        if (guide_sel_ == i) guide_key(SDLK_RETURN);
        else guide_sel_ = i;
        break;
    case RowPanel::TrainTool: train_panel_click(i); break;
    case RowPanel::ToolOptions: tool_options_click(i); break;
    case RowPanel::KeyButton: on_key(static_cast<SDL_Keycode>(i), 0); break;
    }
    return true;
}

bool Tools::on_wheel(float sx, float sy, int steps) {
    // Over a list's rows (or between them), the wheel moves its choice.
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    std::optional<RowPanel> panel;
    for (const RowBox& r : row_boxes_) {
        if (r.panel == RowPanel::Console) continue;
        if (panel && *panel != r.panel) continue;
        panel = r.panel;
        x0 = std::min(x0, r.x0), x1 = std::max(x1, r.x1), y0 = std::min(y0, r.y0), y1 = std::max(y1, r.y1);
    }
    if (!panel || sx < x0 || sx >= x1 || sy < y0 - 20 || sy >= y1 + 20) return false;
    const SDL_Keycode key = steps > 0 ? SDLK_UP : SDLK_DOWN;
    for (int k = 0; k < std::abs(steps); ++k) {
        switch (*panel) {
        case RowPanel::Find: find_key(key); break;
        case RowPanel::Lists: lists_key(key); break;
        case RowPanel::Ledger: ledger_key(key); break;
        case RowPanel::Territories:
            if (key == SDLK_UP && territory_sel_ > 0) --territory_sel_;
            if (key == SDLK_DOWN && territory_sel_ + 1 < world_.territories().territories.size()) ++territory_sel_;
            break;
        case RowPanel::Market: {
            const auto n = static_cast<sim::CompanyId>(world_.companies().size());
            market_choice_ = static_cast<sim::CompanyId>((market_choice_ + (key == SDLK_UP ? n - 1 : 1)) % n);
            break;
        }
        case RowPanel::Guide: guide_key(key); break;
        case RowPanel::TrainTool: on_key(key, 0); break; // through the stops
        case RowPanel::ToolOptions: // the Industry tool's plants scroll; the other panels stay put
            if (tool_ == Tool::Industry) on_key(key == SDLK_UP ? SDLK_LEFTBRACKET : SDLK_RIGHTBRACKET, 0);
            break;
        case RowPanel::KeyButton: break;
        case RowPanel::Console: break;
        }
    }
    return true;
}

bool Tools::rename_key(SDL_Keycode key) {
    if (!detail_ || detail_->kind != DetailKind::Station || key == SDLK_ESCAPE) {
        renaming_ = false;
        return true;
    }
    if (key == SDLK_BACKSPACE) {
        // Whole characters: drop UTF-8 continuation bytes with their lead.
        while (!rename_text_.empty() && (static_cast<unsigned char>(rename_text_.back()) & 0xC0) == 0x80) rename_text_.pop_back();
        if (!rename_text_.empty()) rename_text_.pop_back();
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        const std::string old = world_.railway().station(detail_->id).name;
        const sim::CommandResult r = world_.execute(sim::RenameStation{.station = detail_->id, .name = rename_text_});
        if (r.ok) {
            renaming_ = false;
            if (world_.railway().station(detail_->id).name != old) {
                show(as_written(old) + " is now " + as_written(world_.railway().station(detail_->id).name), true);
            }
        } else {
            show("Cannot: " + r.error, false);
        }
    }
    return true;
}

bool Tools::on_text(const char* text) {
    if (renaming_) {
        if (rename_text_.size() < 30) rename_text_ += text;
        return true;
    }
    if (!find_open_) return false;
    if (find_text_.size() < 30) find_text_ += text;
    find_sel_ = 0;
    return true;
}

void Tools::draw_find_panel() const {
    constexpr float kRow = 22.0f;
    constexpr std::size_t kShown = 12;
    const std::vector<FindHit> hits = find_hits();
    const float w = 460, x0 = (static_cast<float>(cam_.width_px) - w) / 2, y0 = 90;
    const std::size_t shown = std::min(hits.size(), kShown);
    const float h = 108 + kRow * static_cast<float>(std::max<std::size_t>(shown, 1));
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y0 + 8, "Find a town or station", kScale + 1);
    ui::rule(x0 + 10, x0 + w - 10, y0 + 36);
    const bool blink = (SDL_GetTicks() / 500) % 2 == 0;
    fill_rect(x0 + 12, y0 + 44, x0 + w - 12, y0 + 68, 0.08f, 0.07f, 0.06f, 0.9f);
    glColor3f(1.0f, 0.97f, 0.88f);
    draw_text(x0 + 18, y0 + 47, find_text_ + (blink ? "|" : ""), kScale);
    float y = y0 + 76;
    if (hits.empty()) {
        glColor3f(0.68f, 0.66f, 0.62f);
        draw_text(x0 + 18, y, "Nothing by that name", kScale);
    }
    // Scroll so the chosen one is in view.
    const std::size_t first = find_sel_ >= kShown ? find_sel_ + 1 - kShown : 0;
    for (std::size_t i = first; i < hits.size() && i < first + kShown; ++i) {
        add_row(x0 + 10, y - 3, x0 + w - 10, y + kRow - 3, RowPanel::Find, i);
        if (i == find_sel_) ui::selection(x0 + 10, y - 3, x0 + w - 10, y + kRow - 3);
        if (i == find_sel_) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.9f, 0.88f, 0.82f);
        draw_text(x0 + 18, y, hits[i].name, kScale);
        glColor3f(0.68f, 0.66f, 0.62f);
        draw_text(x0 + w - 18 - static_cast<float>(text_width(hits[i].kind, kScale)), y, hits[i].kind, kScale);
        y += kRow;
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 14, y0 + h - 22, "Type to narrow   Up/Down or click choose   Enter or click again: go   Esc close", kScale);
}

void Tools::draw_newspaper() const {
    // A front page: newsprint, a masthead, the date and the headline.
    const float w = 640, cx = static_cast<float>(cam_.width_px) / 2;
    const float x0 = cx - w / 2, x1 = cx + w / 2;
    const auto head = wrap(paper_->headline, kScale + 2, w - 60);
    float h = 176 + 34.0f * static_cast<float>(head.size()) + (paper_->more.empty() ? 0.0f : 30.0f);
    for (const std::string& m : paper_->more) h += 18.0f * static_cast<float>(wrap(m, kScale, w - 80).size());
    const float y0 = std::max(60.0f, (static_cast<float>(cam_.height_px) - h) / 2 - 20), y1 = y0 + h;
    fill_rect(x0 + 8, y0 + 10, x1 + 8, y1 + 10, 0.0f, 0.0f, 0.0f, 0.35f); // shadow
    fill_rect(x0, y0, x1, y1, 0.93f, 0.9f, 0.8f, 1.0f);
    fill_rect(x0 + 6, y0 + 6, x1 - 6, y1 - 6, 0.95f, 0.92f, 0.83f, 1.0f);
    const auto centred = [&](const std::string& s, float y, int scale) {
        draw_text(cx - static_cast<float>(text_width(s, scale)) / 2, y, s, scale);
    };
    const auto hrule = [&](float y, float thick) { fill_rect(x0 + 20, y, x1 - 20, y + thick, 0.15f, 0.12f, 0.1f, 1.0f); };
    glColor3f(0.1f, 0.08f, 0.07f);
    centred("The Railroad Gazette", y0 + 18, kScale + 2);
    float y = y0 + 64;
    hrule(y, 2);
    hrule(y + 4, 1);
    glColor3f(0.25f, 0.2f, 0.17f);
    draw_text(x0 + 24, y + 9, paper_->when, kScale);
    const std::string price = "One cent";
    draw_text(x1 - 24 - static_cast<float>(text_width(price, kScale)), y + 9, price, kScale);
    y += 32;
    hrule(y, 1);
    y += 14;
    glColor3f(0.08f, 0.06f, 0.05f);
    for (const std::string& l : head) {
        centred(l, y, kScale + 2);
        y += 34;
    }
    if (!paper_->more.empty()) {
        y += 6;
        hrule(y, 1);
        y += 12;
        glColor3f(0.2f, 0.17f, 0.14f);
        for (const std::string& m : paper_->more) {
            for (const std::string& l : wrap(m, kScale, w - 80)) {
                centred(l, y, kScale);
                y += 18;
            }
        }
    }
    glColor3f(0.45f, 0.4f, 0.35f);
    centred("Esc, Enter or a click to read on", y1 - 30, kScale);
}

std::string Tools::slot_path(int slot) const {
    if (slot == 0) return quick_save_;
    if (slot == kAutosaveSlot) return save_dir_ + "autosave.sav";
    return save_dir_ + "game" + std::to_string(slot) + ".sav";
}

void Tools::open_menu() {
    menu_open_ = true;
    menu_page_ = 0;
    menu_sel_ = 0;
}

namespace {

// The game menu's pages, and the items on its first.
enum MenuPage { kMenuMain, kMenuSave, kMenuLoad, kMenuQuit, kMenuLeave, kMenuSettings };
enum MenuItem { kItemResume, kItemSave, kItemLoad, kItemBriefing, kItemSettings, kItemMainMenu, kItemQuit, kMainItems };
constexpr int kSettingRows = 9; // sound, music, edge scrolling, time of day, seasons, clouds, frames a second, debug console, back
constexpr const char* kTimeOfDay[] = {"Day", "Dusk", "Night", "Dawn", "Day and night"};

} // namespace

int Tools::menu_items() const {
    switch (menu_page_) {
    case kMenuMain: return kMainItems;
    case kMenuSave:
    case kMenuLoad: return menu_page_ == kMenuLoad ? kAutosaveSlot + 1 : kSaveSlots + 1;
    case kMenuSettings: return kSettingRows;
    default: return 2;
    }
}

void Tools::change_setting(int step) {
    switch (menu_sel_) {
    case 0: settings_.volume = std::clamp(settings_.volume + step, 0, 10); break;
    case 1: settings_.music = std::clamp(settings_.music + step, 0, 10); break;
    case 2: settings_.edge_scroll = !settings_.edge_scroll; break;
    case 3: settings_.time_mode = (settings_.time_mode + step + 5) % 5; break;
    case 4: settings_.seasons = !settings_.seasons; break;
    case 5: settings_.clouds = !settings_.clouds; break;
    case 6: settings_.show_fps = !settings_.show_fps; break;
    case 7: settings_.cheats = !settings_.cheats; break;
    default: return;
    }
    settings_changed_ = true;
}

void Tools::menu_choose() {
    const auto read_slots = [&] {
        slot_info_.clear();
        for (int i = 0; i <= kAutosaveSlot; ++i) slot_info_.push_back(save_summary(slot_path(i)));
    };
    switch (menu_page_) {
    case kMenuMain:
        switch (menu_sel_) {
        case kItemResume: menu_open_ = false; break;
        case kItemSave:
        case kItemLoad:
            menu_page_ = menu_sel_ == kItemSave ? kMenuSave : kMenuLoad;
            menu_sel_ = 1;
            read_slots();
            break;
        case kItemBriefing:
            menu_open_ = false;
            select(Tool::Status);
            break;
        case kItemSettings:
            menu_page_ = kMenuSettings;
            menu_sel_ = 0;
            break;
        default:
            // Main menu or quit: ask first.
            menu_page_ = menu_sel_ == kItemMainMenu ? kMenuLeave : kMenuQuit;
            menu_sel_ = 0;
            break;
        }
        return;
    case kMenuSave:
    case kMenuLoad: {
        const bool saving = menu_page_ == kMenuSave;
        if (!saving && slot_info_[static_cast<std::size_t>(menu_sel_)] == "Empty") {
            show("Nothing saved there", false);
            return;
        }
        menu_request_ = MenuRequest{saving ? MenuRequest::Kind::Save : MenuRequest::Kind::Load, slot_path(menu_sel_)};
        menu_open_ = false;
        return;
    }
    case kMenuSettings:
        if (menu_sel_ == kSettingRows - 1) menu_key(SDLK_ESCAPE);
        else change_setting(+1);
        return;
    default:
        if (menu_sel_ == 1) {
            menu_request_ = MenuRequest{menu_page_ == kMenuLeave ? MenuRequest::Kind::MainMenu : MenuRequest::Kind::Quit, {}};
            menu_open_ = false;
            menu_sel_ = 0;
        } else {
            menu_key(SDLK_ESCAPE);
        }
        return;
    }
}

bool Tools::menu_key(SDL_Keycode key) {
    const int n = menu_items();
    switch (key) {
    case SDLK_ESCAPE:
        // Back to the first page, on the item that led here.
        switch (menu_page_) {
        case kMenuMain: menu_open_ = false; break;
        case kMenuSave: menu_sel_ = kItemSave; break;
        case kMenuLoad: menu_sel_ = kItemLoad; break;
        case kMenuQuit: menu_sel_ = kItemQuit; break;
        case kMenuLeave: menu_sel_ = kItemMainMenu; break;
        default: menu_sel_ = kItemSettings; break;
        }
        menu_page_ = kMenuMain;
        break;
    case SDLK_UP: menu_sel_ = (menu_sel_ + n - 1) % n; break;
    case SDLK_DOWN: menu_sel_ = (menu_sel_ + 1) % n; break;
    case SDLK_LEFT:
    case SDLK_RIGHT:
        if (menu_page_ == kMenuSettings) change_setting(key == SDLK_LEFT ? -1 : +1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: menu_choose(); break;
    default:
        // Number keys pick a slot.
        if ((menu_page_ == kMenuSave || menu_page_ == kMenuLoad) && key >= SDLK_0 && key <= SDLK_0 + kSaveSlots) {
            menu_sel_ = static_cast<int>(key - SDLK_0);
            menu_choose();
        }
        break;
    }
    return true;
}

void Tools::draw_menu() const {
    menu_boxes_.clear();
    std::vector<std::string> items, notes;
    std::string title;
    switch (menu_page_) {
    case kMenuMain:
        title = "Railmaster";
        items = {"Resume", "Save game", "Load game", "Briefing and goals", "Settings", "Main menu", "Quit"};
        break;
    case kMenuSave:
    case kMenuLoad:
        title = menu_page_ == kMenuSave ? "Save game" : "Load game";
        for (int i = 0; i <= (menu_page_ == kMenuLoad ? kAutosaveSlot : kSaveSlots); ++i) {
            items.push_back(i == 0 ? std::string("Quick save (Ctrl+S)")
                            : i == kAutosaveSlot ? std::string("Autosave (each new year)")
                                                 : "Game " + std::to_string(i));
            notes.push_back(static_cast<std::size_t>(i) < slot_info_.size() ? slot_info_[static_cast<std::size_t>(i)] : "");
        }
        break;
    case kMenuSettings:
        title = "Settings";
        items = {"Sound", "Music", "Scroll at the window's edge", "Time of day", "Seasons (snow, autumn leaves)", "Clouds and weather",
                 "Frames a second", "Debug console (. key, cheats)", "Back"};
        notes = {settings_.volume == 0 ? std::string("Off") : std::to_string(settings_.volume * 10) + "%",
                 settings_.music == 0 ? std::string("Off") : std::to_string(settings_.music * 10) + "%", settings_.edge_scroll ? "On" : "Off", kTimeOfDay[settings_.time_mode], settings_.seasons ? "On" : "Off",
                 settings_.clouds ? "On" : "Off", settings_.show_fps ? "On" : "Off", settings_.cheats ? "On" : "Off", ""};
        break;
    default:
        title = menu_page_ == kMenuLeave ? "Leave without saving?" : "Quit without saving?";
        items = {"No, back to the game", menu_page_ == kMenuLeave ? "Yes, to the main menu" : "Yes, quit"};
        break;
    }
    constexpr float kRow = 30.0f;
    const bool slots = menu_page_ == kMenuSave || menu_page_ == kMenuLoad;
    const float w = slots ? 760.0f : menu_page_ == kMenuSettings ? 560.0f : 420.0f;
    const float h = 84 + kRow * static_cast<float>(items.size()) + 34;
    const float x0 = (static_cast<float>(cam_.width_px) - w) / 2;
    const float y0 = std::max(60.0f, (static_cast<float>(cam_.height_px) - h) / 2 - 30);
    fill_rect(0, 0, static_cast<float>(cam_.width_px), static_cast<float>(cam_.height_px), 0.0f, 0.0f, 0.0f, 0.35f);
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + (w - static_cast<float>(text_width(title, kScale + 2))) / 2, y0 + 14, title, kScale + 2);
    ui::rule(x0 + 14, x0 + w - 14, y0 + 62);
    float y = y0 + 76;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const bool sel = static_cast<int>(i) == menu_sel_;
        if (sel) ui::selection(x0 + 10, y - 3, x0 + w - 10, y + kRow - 5);
        menu_boxes_.push_back({x0 + 10, y - 3, x0 + w - 10, y + kRow - 5});
        if (sel) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.9f, 0.88f, 0.82f);
        if (notes.empty()) {
            draw_text(x0 + (w - static_cast<float>(text_width(items[i], kScale + 1))) / 2, y, items[i], kScale + 1);
        } else if (menu_page_ == kMenuSettings) {
            draw_text(x0 + 24, y + 2, items[i], kScale);
            if (!notes[i].empty()) {
                const std::string v = "<  " + notes[i] + "  >";
                draw_text(x0 + w - 24 - static_cast<float>(text_width(v, kScale)), y + 2, v, kScale);
            }
        } else {
            draw_text(x0 + 24, y + 2, items[i], kScale);
            if (notes[i] == "Empty") glColor3f(0.55f, 0.53f, 0.5f);
            draw_text(x0 + 220, y + 2, notes[i], kScale);
        }
        y += kRow;
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    const std::string foot = menu_page_ == kMenuMain       ? "Up/Down and Enter, or click.  Esc returns to the game"
                             : menu_page_ == kMenuSettings ? "Left/Right change.  Esc goes back"
                             : slots                       ? "Up/Down and Enter, a number, or click.  Esc goes back"
                                                           : "Up/Down and Enter, or click.  Esc goes back";
    draw_text(x0 + (w - static_cast<float>(text_width(foot, kScale))) / 2, y0 + h - 28, foot, kScale);
}

void Tools::on_mouse_move(float sx, float sy) {
    if (std::fabs(sx - hover_sx_) + std::fabs(sy - hover_sy_) > 2) hover_moved_at_ = SDL_GetTicks();
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
    cmd.overpasses = overpasses_;
    cmd.tunnels = tunnels_;
    cmd.bridges = bridges_;
    if (curves_ && cmd.start.kind == sim::TrackEnd::Kind::Node) {
        cmd.curve_control = sim::continuing_control_point(world_.railway().track(), cmd.start.node, cmd.end.pos);
    }
    return cmd;
}

std::optional<sim::StationId> Tools::station_at_cursor() const {
    // In 3D, by where stations are drawn on screen: a few pixels near the
    // horizon cover far more ground than near the camera, so a distance on
    // the map alone would miss far stations.
    if (cam_.three_d) {
        std::optional<sim::StationId> best;
        float best_d = 16.0f;
        for (const sim::Station& s : world_.railway().stations()) {
            float sx = 0, sy = 0;
            if (!cam_.to_screen(world_.railway().track().node(s.node).pos, sx, sy)) continue;
            const float d = std::hypot(sx - hover_sx_, sy - hover_sy_);
            if (d < best_d) {
                best = s.id;
                best_d = d;
            }
        }
        if (best) return best;
    }
    return station_near(hover_);
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

std::optional<sim::ServiceBuildingId> Tools::building_near(sim::MapPoint p) const {
    std::optional<sim::ServiceBuildingId> best;
    std::int64_t best_d = snap_mm() + 1;
    const auto type = tool_ == Tool::ServiceTower ? sim::ServiceType::ServiceTower : sim::ServiceType::MaintenanceFacility;
    for (const sim::ServiceBuilding& b : world_.railway().service_buildings()) {
        if (b.type != type) continue;
        const std::int64_t d = sim::distance_mm(p, world_.railway().track().node(b.node).pos);
        if (d < best_d) {
            best = b.id;
            best_d = d;
        }
    }
    return best;
}

bool Tools::undo_build() {
    // Free while still in the tool, as for track (rt3-clone-spec §15).
    const sim::CommandResult r = world_.execute(sim::UndoBuild{});
    if (!r.ok) {
        show("Cannot undo: " + r.error, false);
        return true;
    }
    if (detail_ && detail_->kind == DetailKind::Station) detail_.reset(); // ids may have moved
    stops_.clear();
    stop_sel_ = 0;
    show("Undone: " + format_money(sim::Money{} - r.cost) + " back", true);
    return true;
}

bool Tools::remove_site_key() {
    // Take down the station, tower or shed under the cursor, on a second press.
    const bool station = tool_ == Tool::Station;
    const char* what = station ? "station" : tool_ == Tool::ServiceTower ? "service tower" : "shed";
    const std::optional<std::uint32_t> id = station ? station_at_cursor() : building_near(hover_);
    if (!id) {
        show(std::string("Point at one of your ") + what + "s to take it down", false);
        return true;
    }
    if (SDL_GetTicks() > remove_site_until_ || remove_site_ != id) {
        const sim::Balance& bal = world_.data().balance;
        const sim::Money book = station ? sim::station_cost(world_.railway().station(*id).size, bal)
                                        : sim::service_building_cost(world_.railway().service_buildings()[*id].type, bal);
        remove_site_until_ = SDL_GetTicks() + 3000;
        remove_site_ = id;
        const std::string name = station ? world_.railway().station(*id).name : std::string("this ") + what;
        show("Press X again to take down " + name + ": " + format_money(book) + " book value, " +
                 std::to_string(bal.track.remove_refund_percent) + "% back",
             false);
        return true;
    }
    remove_site_until_ = 0;
    remove_site_.reset();
    const std::string name = station ? world_.railway().station(*id).name : std::string("The ") + what;
    const sim::CommandResult r = station ? world_.execute(sim::RemoveStation{.station = *id})
                                         : world_.execute(sim::RemoveServiceBuilding{.building = *id});
    if (!r.ok) {
        show("Cannot: " + r.error, false);
        return true;
    }
    // Station and building ids above it have moved down one.
    if (detail_ && detail_->kind == DetailKind::Station) detail_.reset();
    stops_.clear();
    stop_sel_ = 0;
    ledger_sel_ = 0;
    show(name + " taken down: " + format_money(sim::Money{} - r.cost) + " back", true);
    return true;
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
    if (paper_open_) {
        paper_open_ = false;
        return;
    }
    if (menu_open_) {
        if (right_button) {
            menu_key(SDLK_ESCAPE);
            return;
        }
        for (std::size_t i = 0; i < menu_boxes_.size(); ++i) {
            const TabBox& b = menu_boxes_[i];
            if (sx >= b.x0 && sx < b.x1 && sy >= b.y0 && sy < b.y1) {
                menu_sel_ = static_cast<int>(i);
                menu_choose();
            }
        }
        return;
    }
    if (help_open_) {
        help_open_ = false; // any click closes the card
        return;
    }
    if (const auto gb = guide_button(); !right_button && sx >= gb[0] && sx < gb[2] && sy >= gb[1] && sy < gb[3]) {
        if (guide_open_) guide_open_ = false;
        else open_guide();
        return;
    }
    if (const auto hb = help_button(); !right_button && sx >= hb[0] && sx < hb[2] && sy >= hb[1] && sy < hb[3]) {
        help_open_ = true;
        return;
    }
    if (!right_button && click_row(sx, sy)) return;
    if (guide_open_) {
        guide_open_ = false; // a click away from the guide closes it
        return;
    }
    if (find_open_) {
        find_open_ = false; // a click away from the box closes it
        return;
    }
    if (right_button) {
        if (tool_ == Tool::Train) cancel();
        else track_start_.reset();
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
    // On a tool's side panel, not the map below it.
    if (const auto& pb = side_panel_box_; (tool_ == Tool::Train || tool_ == Tool::Track || tool_ == Tool::Station ||
                                           tool_ == Tool::Industry) &&
                                          sx >= pb[0] && sx < pb[2] && sy >= pb[1] && sy < pb[3]) {
        return;
    }
    switch (tool_) {
    case Tool::Inspect:
        if (detail_ && sx >= detail_box_[0] && sx < detail_box_[2] && sy >= detail_box_[1] && sy < detail_box_[3]) return;
        if (!open_detail_under_cursor()) {
            detail_.reset();
            follow_ = false;
        }
        return;
    case Tool::Finance:
    case Tool::Ledger:
        for (std::size_t i = 0; i < tab_boxes_.size(); ++i) {
            const TabBox& b = tab_boxes_[i];
            if (sx >= b.x0 && sx < b.x1 && sy >= b.y0 && sy < b.y1) {
                (tool_ == Tool::Finance ? company_tab_ : ledger_tab_) = static_cast<int>(i);
                ledger_sel_ = 0;
            }
        }
        return;
    case Tool::Status:
    case Tool::Lists:
    case Tool::Market: return;
    case Tool::Industry: {
        if (const auto id = site_under_cursor()) {
            const sim::Site& s = world_.economy().sites()[*id];
            const std::string name = world_.data().industries.get(s.type).name;
            if (s.owner && s.owner == world_.player_company()) {
                const sim::CommandResult r = world_.execute(sim::UpgradeIndustry{.site = *id});
                if (r.ok) show("Upgraded the " + name + " to level " + std::to_string(s.level) + " for " + format_money(r.cost), true);
                else show("Cannot upgrade: " + r.error, false);
            } else {
                const sim::CommandResult r = world_.execute(sim::BuyIndustry{.site = *id});
                if (r.ok) show("Bought the " + name + " for " + format_money(r.cost), true);
                else show("Cannot buy: " + r.error, false);
            }
            return;
        }
        const auto types = buildable_types();
        if (types.empty()) {
            show("No plants can be built yet", false);
            return;
        }
        const sim::IndustryTypeId t = types[build_choice_ % types.size()];
        const sim::CommandResult r = world_.execute(sim::BuildIndustry{
            .type = t, .cx = world_.economy().cell_x(hover_), .cy = world_.economy().cell_y(hover_)});
        if (r.ok) show("Built a " + world_.data().industries.get(t).name + " for " + format_money(r.cost), true);
        else show("Cannot build: " + r.error, false);
        return;
    }
    case Tool::Track: {
        if (!track_start_) {
            track_start_ = hover_pick_;
            return;
        }
        const sim::CommandResult r = world_.execute(*pending_track());
        if (!r.ok) {
            show("Cannot build: " + r.error, false);
            return;
        }
        show("Built track for " + format_money(r.cost), true);
        // Carry on from where this run ended, as when dragging track in RT3.
        track_start_ = sim::TrackEnd{sim::TrackEnd::Kind::Node, r.created_id, 0,
                                     world_.railway().track().node(r.created_id).pos};
        on_mouse_move(sx, sy);
        return;
    }
    case Tool::Station: {
        if (station_building_) {
            const sim::CommandResult r = world_.execute(sim::BuildStationBuilding{.type = *station_building_, .pos = hover_});
            if (r.ok) show(std::string("Built a ") + sim::station_building_name(*station_building_) + " for " + format_money(r.cost), true);
            else show("Cannot build: " + r.error, false);
            return;
        }
        const sim::CommandResult r = world_.execute(sim::BuildStation{.at = hover_pick_, .size = station_size_});
        if (r.ok) show("Built " + world_.railway().station(r.created_id).name + " for " + format_money(r.cost), true);
        else show("Cannot build: " + r.error, false);
        return;
    }
    case Tool::ServiceTower:
    case Tool::Maintenance: {
        const auto type = tool_ == Tool::ServiceTower ? sim::ServiceType::ServiceTower
                                                      : sim::ServiceType::MaintenanceFacility;
        const sim::CommandResult r = world_.execute(sim::BuildServiceBuilding{.at = hover_pick_, .type = type});
        if (r.ok) show("Built for " + format_money(r.cost), true);
        else show("Cannot build: " + r.error, false);
        return;
    }
    case Tool::Train: {
        const auto s = station_at_cursor();
        if (!s) {
            show("Click a station to add it to the route", false);
            return;
        }
        if (stops_.empty() || stops_.back().station != *s) {
            // A new stop takes the consist of the one selected, or the default.
            const sim::ConsistRule rule = stops_.empty() ? sim::ConsistRule{.max = cars_} : stops_[stop_sel_].rule;
            stops_.push_back({.station = *s, .service = false, .rule = rule});
            stop_sel_ = stops_.size() - 1;
        }
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
            show("You have founded " + name, true);
        } else {
            show("Cannot found it: " + r.error, false);
        }
        break;
    }
    default: break;
    }
    return true;
}

void Tools::draw_choice_dialog() const {
    const auto& pending = world_.pending_choices();
    if (pending.empty()) return;
    const sim::EventEffect& q = world_.choice_effect(pending.front());
    constexpr float kRow = 20.0f;
    const auto w = static_cast<float>(cam_.width_px);
    float width = 940;
    width = std::max(width, static_cast<float>(text_width(as_written(q.text), kScale)) + 40);
    const float x0 = std::max(10.0f, w / 2 - width / 2), y0 = 90;
    const float rows = static_cast<float>(q.options.size()) + 3.5f;
    ui::panel(x0, y0, x0 + width, y0 + kRow * rows);
    float y = y0 + 14;
    glColor3f(1.0f, 0.9f, 0.5f);
    draw_text(x0 + 20, y, as_written(q.text), kScale);
    y += kRow * 1.5f;
    glColor3f(1, 1, 1);
    for (std::size_t i = 0; i < q.options.size(); ++i) {
        draw_text(x0 + 20, y, std::to_string(i + 1) + "  " + as_written(q.options[i].label), kScale);
        y += kRow;
    }
    const std::int32_t left = pending.front().until_month - (world_.date().year() * 12 + world_.date().month() - 1);
    glColor3f(0.75f, 0.75f, 0.7f);
    draw_text(x0 + 20, y, "Answer with 1-" + std::to_string(q.options.size()) + " within " + std::to_string(left) +
                              (left == 1 ? " month" : " months") +
                              (pending.size() > 1 ? ", " + std::to_string(pending.size() - 1) + " more waiting" : ""),
              kScale);
}

void Tools::draw_founding_dialog() const {
    const sim::Balance::Stock& b = world_.data().balance.stock;
    constexpr float kRow = 20.0f;
    const auto w = static_cast<float>(cam_.width_px);
    const float x0 = std::max(20.0f, w / 2 - 470), y0 = 90;
    ui::panel(x0, y0, x0 + 940, y0 + kRow * 11);
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
    line("Found your railroad", 1.0f, 0.9f, 0.5f);
    y += kRow / 2;
    line("Your fortune " + format_money(world_.investor().cash), 0.9f, 0.9f, 0.9f);
    line("You invest " + format_money(mine) + "   (Left/Right, Shift for $1,000,000)", 1, 1, 1);
    line("Outside investors put in " + format_money(outside) + " of " +
             format_money(sim::Money::dollars(b.outside_investment)) + " offered   (Up/Down)",
         1, 1, 1);
    y += kRow / 2;
    line("Capital " + format_money(mine + outside) + "   " + format_count(shares) + " shares at " +
             format_cents(sim::Money::cents(price)),
         0.9f, 0.9f, 0.9f);
    line("Your stake " + format_count(stake) + " shares (" + std::to_string(shares > 0 ? stake * 100 / shares : 0) +
             "%)   your cash left " + format_money(world_.investor().cash - mine),
         0.9f, 0.9f, 0.9f);
    if (stake * 2 <= shares) {
        line("At half the shares or less, investors can vote you out after bad years", 0.95f, 0.65f, 0.6f);
    } else {
        line("With over half the shares, nobody can vote you out", 0.6f, 0.95f, 0.6f);
    }
    line("More outside money builds more railroad, but dilutes your control", 0.8f, 0.8f, 0.8f);
    y += kRow / 2;
    line(founding_cancellable_ ? "Enter to found the company, Esc to cancel" : "Enter to found the company", 1.0f, 0.9f,
         0.5f);
}

bool Tools::on_key(SDL_Keycode key, Uint16 mod) {
    if (founding_) return founding_key(key, mod);
    if (paper_open_) {
        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) paper_open_ = false;
        return true;
    }
    if (menu_open_) return menu_key(key);
    if (guide_open_) return guide_key(key);
    if (renaming_) return rename_key(key);
    if (key == SDLK_g && (mod & (KMOD_CTRL | KMOD_GUI)) != 0) {
        open_guide();
        return true;
    }
    if (help_open_) {
        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_SLASH || key == SDLK_QUESTION || key == SDLK_F1) {
            help_open_ = false;
        }
        return true;
    }
    if (key == SDLK_QUESTION || (key == SDLK_SLASH && (mod & KMOD_SHIFT) != 0)) {
        help_open_ = true;
        return true;
    }
    if (find_open_) return find_key(key);
    if (console_open_) return console_key(key);
    if (key == SDLK_PERIOD && settings_.cheats && tool_ != Tool::Train && (mod & (KMOD_CTRL | KMOD_GUI)) == 0) {
        console_open_ = true;
        return true;
    }
    if (key == SDLK_f && (mod & (KMOD_CTRL | KMOD_GUI)) != 0) {
        find_open_ = true;
        find_text_.clear();
        find_sel_ = 0;
        return true;
    }
    if (log_open_) {
        const std::size_t n = log_.size();
        switch (key) {
        case SDLK_ESCAPE:
        case SDLK_m: log_open_ = false; return true;
        case SDLK_UP: log_scroll_ = log_scroll_ > 0 ? log_scroll_ - 1 : 0; return true;
        case SDLK_DOWN: log_scroll_ = n > 0 ? std::min(n - 1, log_scroll_ + 1) : 0; return true;
        case SDLK_PAGEUP: log_scroll_ = log_scroll_ > 10 ? log_scroll_ - 10 : 0; return true;
        case SDLK_PAGEDOWN: log_scroll_ = n > 0 ? std::min(n - 1, log_scroll_ + 10) : 0; return true;
        default: break;
        }
    }
    if (key == SDLK_m && tool_ != Tool::Market && tool_ != Tool::Industry && (mod & (KMOD_CTRL | KMOD_GUI)) == 0) {
        log_open_ = true;
        log_scroll_ = 0;
        return true;
    }
    if (key == SDLK_n && (mod & KMOD_SHIFT) != 0 && paper_) {
        paper_open_ = true;
        return true;
    }
    // A scenario event's question takes the number keys until it is answered.
    if (!world_.pending_choices().empty() && key >= SDLK_1 && key <= SDLK_9) {
        const auto option = static_cast<std::size_t>(key - SDLK_1);
        const std::string label = option < world_.choice_effect(world_.pending_choices().front()).options.size()
                                      ? world_.choice_effect(world_.pending_choices().front()).options[option].label
                                      : "";
        const sim::CommandResult r = world_.execute(sim::AnswerChoice{.choice = 0, .option = option});
        if (r.ok) show("You chose: " + as_written(label), true);
        else show("Cannot: " + r.error, false);
        return true;
    }
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
    case SDLK_F10: select(Tool::Status); return true;
    case SDLK_F11: select(Tool::Lists); return true;
    case SDLK_l:
        if (tool_ == Tool::Train || (mod & (KMOD_CTRL | KMOD_GUI)) != 0) break; // L picks the engine there
        select(tool_ == Tool::Ledger ? Tool::Inspect : Tool::Ledger);
        return true;
    case SDLK_TAB:
        if (tool_ == Tool::Finance || tool_ == Tool::Ledger) {
            int& tab = tool_ == Tool::Finance ? company_tab_ : ledger_tab_;
            const int n = tool_ == Tool::Finance ? 4 : 9;
            tab = (tab + ((mod & KMOD_SHIFT) != 0 ? n - 1 : 1)) % n;
            ledger_sel_ = 0;
            return true;
        }
        break;
    case SDLK_o: cycle_overlay(+1); return true;
    case SDLK_p: cycle_overlay(-1); return true;
    case SDLK_ESCAPE:
        if (track_start_) track_start_.reset();
        else if (tool_ == Tool::Train && (!stops_.empty() || editing_)) cancel(); // the route only in its own tool
        else if (detail_) {
            detail_.reset();
            follow_ = false;
        } else if (link_ && tool_ == Tool::Inspect) {
            link_.reset();
            show("The planned line is cleared", true);
        } else if (tool_ == Tool::Inspect) {
            open_menu();
        } else {
            select(Tool::Inspect);
        }
        return true;
    default: break;
    }

    switch (tool_) {
    case Tool::Inspect: {
        if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
            // Through your trains in turn (rt3-clone-spec §15.4), the view following.
            std::vector<sim::TrainId> mine;
            for (const sim::Train& t : world_.railway().trains())
                if (t.in_service() && world_.player_company() && t.owner == *world_.player_company()) mine.push_back(t.id);
            if (mine.empty()) return true;
            page_train_ = (page_train_ + (key == SDLK_PAGEDOWN ? 1 : mine.size() - 1)) % mine.size();
            const sim::TrainId id = mine[page_train_];
            jump_ = world_.railway().train_position(id);
            hover_ = *jump_;
            show("Train " + std::to_string(id + 1) + " (" + std::to_string(page_train_ + 1) + " of " +
                     std::to_string(mine.size()) + ")",
                 true);
            return true;
        }
        if (key == SDLK_b && link_) {
            build_link();
            return true;
        }
        if (key == SDLK_t) {
            // Buy access rights to the territory under the cursor (rt3-clone-spec §3.3).
            const sim::TerritoryId terr = world_.territory_at(hover_);
            if (terr == sim::kNoTerritory) return false;
            const sim::CommandResult r = world_.execute(sim::BuyTerritoryAccess{.territory = terr});
            if (r.ok) show("Bought access to " + world_.territories().territories[terr].name + " for " + format_money(r.cost), true);
            else show("Cannot: " + r.error, false);
            return true;
        }
        if (key == SDLK_SPACE && detail_train()) {
            follow_ = !follow_;
            return true;
        }
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && detail_ && detail_->kind == DetailKind::Station &&
            world_.player_company() && world_.railway().station(detail_->id).owner == *world_.player_company()) {
            renaming_ = true;
            rename_text_ = world_.railway().station(detail_->id).name;
            return true;
        }
        if (key == SDLK_u && detail_ && detail_->kind == DetailKind::Station) {
            // Enlarge the station shown (rt3-clone-spec §15.4).
            const sim::Station& s = world_.railway().station(detail_->id);
            const sim::CommandResult r = world_.execute(sim::UpgradeStation{.station = s.id});
            if (r.ok) {
                show(as_written(world_.railway().station(s.id).name) + " is now a " +
                         size_name(world_.railway().station(s.id).size) + " station, for " + format_money(r.cost),
                     true);
            } else {
                show("Cannot: " + r.error, false);
            }
            return true;
        }
        // Keys act on the detail panel's train, else the train under the
        // cursor (rt3-clone-spec §9.3).
        const sim::Railway& rw = world_.railway();
        std::optional<sim::TrainId> id = detail_train();
        for (const sim::Train& t : rw.trains()) {
            if (id) break;
            if (t.in_service() && sim::distance_mm(rw.train_position(t.id), hover_) <= snap_mm()) {
                id = t.id;
                break;
            }
        }
        if (!id) return false;
        const sim::Train& t = rw.train(*id);
        const std::string name = "Train " + std::to_string(*id + 1);
        const auto report = [&](const sim::CommandResult& r, const std::string& done) {
            if (r.ok) show(done, true);
            else show("Cannot: " + r.error, false);
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
        case SDLK_e:
            if (t.owner != world_.company().id()) {
                show("Cannot: you can only change your own trains", false);
                return true;
            }
            // Edit the route in the Train tool, starting from the current one.
            select(Tool::Train);
            cancel(); // whatever route was being planned gives way to this one
            editing_ = *id;
            for (std::size_t i = 0; i < t.route.size(); ++i) {
                stops_.push_back({.station = t.route[i],
                                  .service = i < t.service_stop.size() && t.service_stop[i] != 0,
                                  .rule = i < t.rules.size() ? t.rules[i] : sim::ConsistRule{}});
            }
            stop_sel_ = 0;
            show("Editing the route of " + name, true);
            return true;
        case SDLK_h: {
            const std::int32_t priority = t.priority + ((mod & KMOD_SHIFT) != 0 ? -1 : 1);
            return report(world_.execute(sim::SetPriority{.train = *id, .priority = priority}),
                          name + " priority " + std::to_string(priority));
        }
        case SDLK_r: {
            const auto locos = available_locos();
            if (locos.empty()) {
                show("No engine is available", false);
                return true;
            }
            const sim::LocoTypeId loco = locos[loco_choice_ % locos.size()];
            return report(world_.execute(sim::ReplaceLocomotive{.train = *id, .loco = loco}),
                          name + " now pulled by a new " + world_.data().locomotives.get(loco).name);
        }
        case SDLK_f:
            return every_stop(
                [](sim::ConsistRule& r) { r.filter = static_cast<sim::CargoFilter>((static_cast<int>(r.filter) + 1) % 3); },
                name + " consist changed");
        case SDLK_w:
            return every_stop([](sim::ConsistRule& r) { r.min = r.min > 0 ? 0 : r.max; },
                              name + (t.rules.empty() || t.rules.front().min == 0 ? " will wait for full loads"
                                                                                 : " no longer waits for full loads"));
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET:
            return every_stop(
                [&](sim::ConsistRule& r) {
                    if (key == SDLK_LEFTBRACKET && r.max > 0) --r.max;
                    if (key == SDLK_RIGHTBRACKET) ++r.max;
                    r.min = std::min(r.min, r.max);
                },
                name + " consist changed");
        case SDLK_c:
            return report(world_.execute(sim::SetSpecialCars{.train = *id, .caboose = !t.caboose, .diner = t.diner}),
                          name + (t.caboose ? " drops its caboose" : " gets a caboose"));
        case SDLK_d:
            return report(world_.execute(sim::SetSpecialCars{.train = *id, .caboose = t.caboose, .diner = !t.diner}),
                          name + (t.diner ? " drops its dining car" : " gets a dining car"));
        case SDLK_y: {
            const sim::CommandResult r = world_.execute(sim::CopyTrain{.train = *id});
            return report(r, "Bought train " + std::to_string(r.created_id + 1) + ", a copy of " + name);
        }
        case SDLK_x:
            // Retiring cannot be undone: ask for a second press.
            if (SDL_GetTicks() > retire_armed_until_ || retire_armed_ != *id) {
                retire_armed_until_ = SDL_GetTicks() + 3000;
                retire_armed_ = *id;
                show("Press X again to retire " + name, false);
                return true;
            }
            retire_armed_until_ = 0;
            return report(world_.execute(sim::RetireTrain{.train = *id}), name + " retired");
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
                    show("Point at a piece of track (Shift+E for all your track)", false);
                    return true;
                }
                cmd.edges.push_back(p->edge);
            }
            const sim::CommandResult r = world_.execute(cmd);
            if (r.ok) show("Electrified for " + format_money(r.cost), true);
            else show("Cannot: " + r.error, false);
            return true;
        }
        if (key == SDLK_z && (mod & KMOD_CTRL) != 0) {
            // Free undo of the last run built since opening the Track tool [D].
            const sim::CommandResult r = world_.execute(sim::UndoTrack{});
            if (r.ok) {
                track_start_.reset();
                show("Undone: " + format_money(sim::Money{} - r.cost) + " refunded", true);
            } else {
                show("Cannot undo: " + r.error, false);
            }
            return true;
        }
        if (key == SDLK_u) {
            // Double the run of single track under the cursor.
            const auto p = world_.railway().track().nearest_edge_point(hover_, snap_mm());
            if (!p) {
                show("Point at a piece of your track to double it", false);
                return true;
            }
            const sim::CommandResult r = world_.execute(sim::UpgradeTrack{.edges = world_.railway().run_of(p->edge)});
            if (r.ok) show("Doubled for " + format_money(r.cost), true);
            else show("Cannot: " + r.error, false);
            return true;
        }
        if (key == SDLK_x) {
            // Bulldoze the run of track under the cursor, on a second press.
            const auto p = world_.railway().track().nearest_edge_point(hover_, snap_mm());
            if (!p) {
                show("Point at a piece of your track to remove it", false);
                return true;
            }
            const std::vector<sim::EdgeId> run = world_.railway().run_of(p->edge);
            if (SDL_GetTicks() > remove_armed_until_ || remove_armed_ != run) {
                sim::Money book;
                for (sim::EdgeId e : run) book += world_.railway().track().edge(e).cost;
                remove_armed_until_ = SDL_GetTicks() + 3000;
                remove_armed_ = run;
                show("Press X again to remove this track: " + format_money(book) + " book value, " +
                         std::to_string(world_.data().balance.track.remove_refund_percent) + "% back",
                     false);
                return true;
            }
            remove_armed_until_ = 0;
            remove_armed_.clear();
            const sim::CommandResult r = world_.execute(sim::RemoveTrack{.edges = run});
            if (r.ok) {
                track_start_.reset();
                show("Track removed: " + format_money(sim::Money{} - r.cost) + " back", true);
            } else {
                show("Cannot: " + r.error, false);
            }
            return true;
        }
        // Frequency settings cycle Never, Rarely, Average, Common (Shift: back).
        if (key == SDLK_g) overpasses_ = next_frequency(overpasses_, (mod & KMOD_SHIFT) != 0);
        else if (key == SDLK_t) tunnels_ = next_frequency(tunnels_, (mod & KMOD_SHIFT) != 0);
        else if (key == SDLK_b) bridges_ = next_frequency(bridges_, (mod & KMOD_SHIFT) != 0);
        else if (key == SDLK_d) double_track_ = !double_track_;
        else if (key == SDLK_c) curves_ = !curves_;
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
        if (key == SDLK_x) return remove_site_key();
        if (key == SDLK_z && (mod & (KMOD_CTRL | KMOD_GUI)) != 0) return undo_build();
        return false;
    case Tool::ServiceTower:
    case Tool::Maintenance:
        if (key == SDLK_x) return remove_site_key();
        if (key == SDLK_z && (mod & (KMOD_CTRL | KMOD_GUI)) != 0) return undo_build();
        return false;
    case Tool::Train: {
        const auto locos = available_locos();
        const auto& cargo = world_.data().cargo.all();
        sim::ConsistRule* rule = stops_.empty() ? nullptr : &stops_[stop_sel_].rule;
        if (key == SDLK_l && !locos.empty()) {
            loco_choice_ = (loco_choice_ + 1) % locos.size();
        } else if (key == SDLK_UP || key == SDLK_DOWN) {
            if (stops_.empty()) return true;
            stop_sel_ = (stop_sel_ + (key == SDLK_UP ? stops_.size() - 1 : 1)) % stops_.size();
        } else if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
            const bool more = key == SDLK_RIGHTBRACKET;
            if (!rule) {
                // No stops yet: the cars new stops start with.
                if (!more && cars_ > 0) --cars_;
                if (more && cars_ < sim::kMaxCarsPerTrain) ++cars_;
            } else if (rule->custom) {
                // Custom: take off the last car, or add one of the chosen cargo.
                if (!more && !rule->cars.empty()) rule->cars.pop_back();
                if (more && rule->cars.size() < sim::kMaxCarsPerTrain && !cargo.empty()) {
                    rule->cars.push_back(cargo[cargo_choice_ % cargo.size()].id);
                }
            } else {
                if (!more && rule->max > 0) --rule->max;
                if (more && rule->max < sim::kMaxCarsPerTrain) ++rule->max;
                rule->min = std::min(rule->min, rule->max);
            }
        } else if (key == SDLK_f && rule) {
            rule->custom = false;
            rule->filter = static_cast<sim::CargoFilter>((static_cast<int>(rule->filter) + 1) % 3);
        } else if (key == SDLK_w && rule) {
            rule->min = rule->min > 0 ? 0 : static_cast<std::uint8_t>(rule->capacity());
        } else if (key == SDLK_k && rule) {
            // Custom cars: start from as many cars of the chosen cargo as the automatic rule took.
            rule->custom = !rule->custom;
            if (rule->custom && rule->cars.empty() && !cargo.empty()) {
                rule->cars.assign(std::max<std::size_t>(1, rule->max), cargo[cargo_choice_ % cargo.size()].id);
            }
            rule->min = std::min<std::uint8_t>(rule->min, static_cast<std::uint8_t>(rule->capacity()));
        } else if ((key == SDLK_COMMA || key == SDLK_PERIOD) && !cargo.empty()) {
            cargo_choice_ = (cargo_choice_ + (key == SDLK_COMMA ? cargo.size() - 1 : 1)) % cargo.size();
        } else if (key == SDLK_BACKSPACE && !stops_.empty()) {
            stops_.pop_back();
            stop_sel_ = stops_.empty() ? 0 : std::min(stop_sel_, stops_.size() - 1);
        } else if (key == SDLK_s && !stops_.empty()) {
            // Make the selected stop a service stop, or an ordinary one again.
            stops_[stop_sel_].service = !stops_[stop_sel_].service;
        } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && editing_) {
            const sim::TrainId id = *editing_;
            const sim::CommandResult r =
                world_.execute(sim::SetRoute{.train = id, .route = pending_route(), .service_stops = pending_service()});
            if (!r.ok) {
                show("Cannot change the route: " + r.error, false);
            } else if (apply_consists(id)) {
                show("Train " + std::to_string(id + 1) + " now calls at " + std::to_string(stops_.size()) + " stops", true);
                select(Tool::Inspect);
            }
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (locos.empty()) {
                show("No locomotives available this year", false);
                return true;
            }
            const sim::LocoTypeId loco = locos[loco_choice_ % locos.size()];
            std::size_t cars = 0;
            for (const PendingStop& p : stops_) cars = std::max(cars, p.rule.capacity());
            const sim::CommandResult r = world_.execute(
                sim::BuyTrain{.loco = loco, .cars = static_cast<std::uint8_t>(cars), .route = pending_route()});
            if (!r.ok) {
                show("Cannot buy: " + r.error, false);
                return true;
            }
            const std::vector<bool> service = pending_service();
            if (std::any_of(service.begin(), service.end(), [](bool b) { return b; })) {
                world_.execute(sim::SetRoute{.train = r.created_id, .route = pending_route(), .service_stops = service});
            }
            if (apply_consists(r.created_id)) {
                const bool steam = world_.data().locomotives.get(loco).fuel == sim::Fuel::Steam;
                if (steam && !tower_near(pending_route())) {
                    show("Bought " + world_.data().locomotives.get(loco).name +
                             ", but no service tower is near its stations: build one (F4) or it will run dry and crawl",
                         false);
                    message_until_ = SDL_GetTicks() + 9000;
                } else {
                    show("Bought " + world_.data().locomotives.get(loco).name + " for " + format_money(r.cost), true);
                }
            }
            stops_.clear();
            stop_sel_ = 0;
        } else {
            return false;
        }
        return true;
    }
    case Tool::Finance: {
        if (company_tab_ == 3 && (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_t)) {
            const auto& terrs = world_.territories().territories;
            if (terrs.empty()) return true;
            if (key == SDLK_UP) territory_sel_ = (territory_sel_ + terrs.size() - 1) % terrs.size();
            else if (key == SDLK_DOWN) territory_sel_ = (territory_sel_ + 1) % terrs.size();
            else if (territory_sel_ < terrs.size()) {
                const sim::CommandResult r =
                    world_.execute(sim::BuyTerritoryAccess{.territory = static_cast<sim::TerritoryId>(territory_sel_)});
                if (r.ok) show("Bought access to " + terrs[territory_sel_].name + " for " + format_money(r.cost), true);
                else show("Cannot: " + r.error, false);
            }
            return true;
        }
        if (key == SDLK_k) {
            // A last resort: ask for a second press.
            if (SDL_GetTicks() > bankrupt_armed_until_) {
                bankrupt_armed_until_ = SDL_GetTicks() + 3000;
                show("Press K again to declare bankruptcy", false);
                return true;
            }
            bankrupt_armed_until_ = 0;
            const sim::CommandResult r = world_.execute(sim::DeclareBankruptcy{});
            if (r.ok) show("Bankrupt: half the bond debt is gone, and so is your credit", false);
            else show("Cannot declare bankruptcy: " + r.error, false);
            return true;
        }
        const bool big = (mod & KMOD_SHIFT) != 0; // 5,000 shares at a time
        const std::int64_t blocks = big ? 5 : 1;
        const sim::Money step = sim::Money::cents(25);
        sim::CommandResult r;
        std::string done;
        if (key == SDLK_b) {
            r = world_.execute(sim::IssueBond{});
            done = "Issued A " + format_money(sim::Money::dollars(world_.company().finance_balance().bond_face_value)) +
                   " bond";
        } else if (key == SDLK_r) {
            r = world_.execute(sim::RepayBond{});
            done = "Repaid a bond";
        } else if (key == SDLK_a) {
            r = world_.execute(sim::BuyShares{.blocks = blocks});
            done = "Bought " + format_count(blocks * world_.company().stock_balance().share_block) + " shares";
        } else if (key == SDLK_s) {
            r = world_.execute(sim::SellShares{.blocks = blocks});
            done = "Sold " + format_count(blocks * world_.company().stock_balance().share_block) + " shares";
        } else if (key == SDLK_i) {
            r = world_.execute(sim::IssueStock{});
            done = "Issued new stock";
        } else if (key == SDLK_y) {
            r = world_.execute(sim::BuyBackStock{});
            done = "Bought back stock";
        } else if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
            const sim::Money now = world_.company().dividend_per_share();
            const sim::Money next = key == SDLK_RIGHTBRACKET ? now + step : std::max(sim::Money{}, now - step);
            r = world_.execute(sim::SetDividend{.per_share = next});
            done = "Dividend set to " + format_cents(next) + " a share a year";
        } else if (key == SDLK_d) {
            // The suggested dividend: a share of last year's profit the cash covers.
            const sim::Money per_share = world_.company().suggested_dividend();
            if (per_share <= sim::Money{}) {
                show("No dividend to suggest: it needs a profit over the last 12 months, and the cash to pay a year of it", false);
                return true;
            }
            r = world_.execute(sim::SetDividend{.per_share = per_share});
            done = "Dividend set to " + format_cents(per_share) + " a share a year (" +
                   format_money(per_share.scaled(world_.company().shares_outstanding(), 1)) + " a year in all)";
        } else {
            return false;
        }
        if (r.ok) show(done, true);
        else show("Cannot: " + r.error, false);
        return true;
    }
    case Tool::Industry:
        if (key == SDLK_m || key == SDLK_c) {
            const auto id = site_under_cursor();
            const auto kind = id ? world_.economy().sites()[*id].kind : sim::IndustryKind::Raw;
            if (!id || (kind != sim::IndustryKind::Warehouse && kind != sim::IndustryKind::Port) ||
                (key == SDLK_c && kind != sim::IndustryKind::Warehouse)) {
                show(key == SDLK_m ? "Point at one of your warehouses or ports to change what it does"
                                   : "Point at one of your warehouses to set what it converts",
                     false);
                return true;
            }
            const sim::Site& site = world_.economy().sites()[*id];
            const std::string what = kind == sim::IndustryKind::Port ? "Port" : "Warehouse";
            sim::SetPortMode cmd{.site = *id};
            if (key == SDLK_m) {
                // Exchange, exports, imports, round again.
                cmd.mode = site.port_mode == sim::PortMode::Convert
                               ? sim::PortMode::Exchange
                               : static_cast<sim::PortMode>((static_cast<int>(site.port_mode) + 1) % 3);
            } else {
                // The next of the conversions it can make: each cargo it takes into each it supplies.
                const sim::IndustryType& t = world_.data().industries.get(site.type);
                std::vector<std::pair<sim::CargoId, sim::CargoId>> pairs;
                for (const sim::IndustryInput& in : t.inputs)
                    for (sim::CargoId out : t.outputs)
                        if (in.cargo != out) pairs.emplace_back(in.cargo, out);
                if (pairs.empty()) {
                    show("This warehouse has nothing to convert", false);
                    return true;
                }
                std::size_t next = 0;
                if (site.port_mode == sim::PortMode::Convert) {
                    for (std::size_t i = 0; i < pairs.size(); ++i)
                        if (pairs[i] == std::pair{site.convert_from, site.convert_to}) next = (i + 1) % pairs.size();
                }
                cmd.mode = sim::PortMode::Convert;
                cmd.convert_from = pairs[next].first;
                cmd.convert_to = pairs[next].second;
            }
            const sim::CommandResult r = world_.execute(cmd);
            if (!r.ok) show("Cannot: " + r.error, false);
            else if (cmd.mode == sim::PortMode::Convert)
                show("The warehouse now converts " + world_.data().cargo.get(cmd.convert_from).name + " into " +
                         world_.data().cargo.get(cmd.convert_to).name,
                     true);
            else show("The " + what + " now handles " + sim::port_mode_name(cmd.mode), true);
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
                show("You already run a company", false);
            } else {
                open_founding(/*cancellable=*/true);
            }
            return true;
        }
        if (key == SDLK_q) {
            // Resigning cannot be undone: ask for a second press.
            if (SDL_GetTicks() > resign_armed_until_) {
                resign_armed_until_ = SDL_GetTicks() + 3000;
                show("Press Q again to resign as chairman", false);
                return true;
            }
            resign_armed_until_ = 0;
            const sim::CommandResult r = world_.execute(sim::Resign{});
            if (r.ok) show("You have resigned. You keep your shares", true);
            else show("Cannot resign: " + r.error, false);
            return true;
        }
        if (key == SDLK_t) {
            const sim::CommandResult r = world_.execute(sim::AttemptTakeover{.target = target.id()});
            if (r.ok) show("The shareholders made you chairman of " + target.name(), true);
            else show("Takeover failed: " + r.error, false);
            return true;
        }
        if (key == SDLK_m) {
            // Offer a premium over the market price: 20%, or 50% with Shift.
            const std::int32_t premium = (mod & KMOD_SHIFT) != 0 ? 150 : 120;
            const sim::Money offer = target.share_price().scaled(premium, 100);
            const std::string name = target.name();
            const sim::CommandResult r =
                world_.execute(sim::AttemptMerger{.target = target.id(), .offer_per_share = offer});
            if (r.ok) show("Merged " + name + " for " + format_money(r.cost), true);
            else show("Merger failed: " + r.error, false);
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
        if (r.ok) show((buy ? "Bought " : now_short ? "Sold short " : "Sold ") + shares + " " + target.name() + " shares", true);
        else show("Cannot: " + r.error, false);
        return true;
    }
    case Tool::Lists: return lists_key(key);
    case Tool::Ledger: return ledger_key(key);
    default: return false;
    }
}

// Game status (rt3-clone-spec §14.3 [D]): the scenario's briefing, each
// medal's deadline and goals with progress, and the medal won.
void Tools::draw_status_panel() const {
    constexpr float kRow = 16.0f;
    const float x0 = 40, y0 = 56, w = 1100;
    std::vector<std::pair<std::string, int>> rows; // text, colour: 0 title, 1 plain, 2 met, 3 not met, 4 dim
    const auto& sc = world_.scenario();
    if (!sc) {
        rows.push_back({"Game status", 0});
        rows.push_back({"A free game: no goals and no deadline. Start one with --scenario=file.", 1});
        if (world_.assisted()) rows.push_back({"Assisted: the debug console has been used in this game", 1});
    } else {
        const sim::ScenarioResult& res = world_.scenario_result();
        rows.push_back({sc->name, 0});
        // The briefing, wrapped to the panel.
        std::string line;
        std::istringstream words(sc->briefing);
        for (std::string word; words >> word;) {
            if (!line.empty() && text_width(line + " " + word, kScale) > static_cast<int>(w - 40)) {
                rows.push_back({line, 1});
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
        }
        if (!line.empty()) rows.push_back({line, 1});
        // What the scenario forbids or limits.
        const sim::Restrictions& r = sc->restrictions;
        std::string rules;
        const auto rule = [&](bool on, const char* text) {
            if (on) rules += (rules.empty() ? "" : ", ") + std::string(text);
        };
        if (r.track_tiles) {
            rules += "Track budget " + std::to_string(*r.track_tiles) + " cells + " + std::to_string(r.tiles_per_year) +
                     " a year" + (r.tile_refund ? "" : ", no refund");
        }
        rule(r.no_unconnected_track, "No unconnected track");
        rule(r.one_company_only, "One company only");
        rule(r.no_track, "No new track");
        rule(r.no_stations, "No new stations");
        rule(r.no_trains, "No new trains");
        rule(r.no_industries, "No industry purchases");
        rule(!sc->chairman_can_resign, "You cannot resign");
        rule(!sc->chairman_can_be_fired, "You cannot be fired");
        if (!rules.empty()) rows.push_back({"Rules: " + rules, 1});
        if (world_.assisted()) rows.push_back({"Assisted: the debug console has been used in this game", 1});
        rows.push_back({"", 1});
        for (int m = 0; m < 3; ++m) {
            const auto medal = static_cast<sim::Medal>(m);
            const bool won = res.medal && static_cast<int>(*res.medal) >= m;
            std::string head = std::string(sim::medal_name(medal)) + " by " +
                               sc->medals[static_cast<std::size_t>(m)].deadline.month_year_label();
            if (m > 0) head += "  (and the goals above)";
            if (won) head += "  - won";
            rows.push_back({head, 0});
            for (const sim::Goal& g : sc->medals[static_cast<std::size_t>(m)].goals) {
                const sim::GoalProgress p = sim::check_goal(world_, g);
                rows.push_back({std::string(p.met ? "  [X] " : "  [ ] ") + p.text, won ? 4 : p.met ? 2 : 3});
            }
        }
        rows.push_back({"", 1});
        if (res.medal) {
            rows.push_back({std::string(sim::medal_name(*res.medal)) + " medal won " + res.won_on->month_year_label() +
                                "  score " + format_count(world_.scenario_score_milli() / 1000) + "." +
                                std::to_string(world_.scenario_score_milli() % 1000 / 100),
                            2});
        }
        if (res.finished) rows.push_back({res.medal ? "The scenario is over." : "The scenario is over: no medal.", 0});
    }
    const float h = kRow * static_cast<float>(rows.size()) + 20;
    ui::panel(x0, y0, x0 + w, y0 + h);
    float y = y0 + 10;
    for (const auto& [text, colour] : rows) {
        switch (colour) {
        case 0: glColor3f(1.0f, 0.9f, 0.5f); break;
        case 2: glColor3f(0.6f, 0.95f, 0.6f); break;
        case 3: glColor3f(1.0f, 0.6f, 0.5f); break;
        case 4: glColor3f(0.55f, 0.55f, 0.55f); break;
        default: glColor3f(0.85f, 0.85f, 0.85f); break;
        }
        std::string upper = text;
        draw_text(x0 + 14, y, upper, kScale);
        y += kRow;
    }
}

void Tools::draw_market_panel() const {
    constexpr float kRow = 16.0f;
    const auto& companies = world_.companies();
    const float x0 = 40, y0 = 56, w = 1100;
    const float h = kRow * static_cast<float>(companies.size() + world_.investors().size() + 12) + 44; // and the buttons
    ui::panel(x0, y0, x0 + w, y0 + h);
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
    cells({"Stock market", "Price", "Book/share", "Last year", "You hold"}, 1.0f, 0.9f, 0.5f);
    const sim::Investor& me = world_.investor();
    const auto n = static_cast<sim::CompanyId>(companies.size());
    const sim::CompanyId chosen = static_cast<sim::CompanyId>(market_choice_ % n);
    for (const sim::Company& c : companies) {
        const auto& hist = c.history();
        const sim::Money last = hist.size() > 1 ? hist[hist.size() - 2].profit() : hist.back().profit();
        const bool sel = c.id() == chosen;
        add_row(x0 + 6, y - 2, x0 + w - 6, y + kRow - 3, RowPanel::Market, c.id());
        float r, g, b;
        owner_rgb(c.id(), world_.player_company(), r, g, b);
        const float swatch = cols[0] + static_cast<float>(text_width("> ", kScale));
        fill_rect(swatch, y + 2, swatch + 8, y + 10, r, g, b, 1.0f);
        if (c.defunct()) {
            cells({std::string(sel ? ">" : " ") + "    " + c.name() + " (merged into " +
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
    const float chart_top = y;
    std::string chairman = "None";
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
    draw_text(cols[0], y, c.name() + "   chairman: " + chairman, kScale);
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
    line("Cash " + format_money(c.cash()) + "   book value " + format_money(c.book_value()) + "   bonds " +
         std::to_string(c.bonds().size()) + "   rating " + sim::rating_name(c.credit_rating()));
    line("Track " + format_money(c.track_value()) + "   stations " + std::to_string(stations) + "   trains " +
         std::to_string(trains));
    line(std::string("Investors ") + sim::sentiment_name(c.sentiment()) + "   5-year return " +
         format_permille(c.weighted_return_permille()) + "   bad years in a row " + std::to_string(c.bad_year_streak()));
    line("Shares " + format_count(c.shares_outstanding()) + "   in public hands " +
         format_count(sim::public_float(world_.market(), c.id())) + "   dividend " + format_cents(c.dividend_per_share()) +
         " a year");
    const sim::YearAccounts& now = c.this_year();
    line("This year: revenue " + format_money(now.revenue()) + "   profit " + format_money(now.profit()) +
         "   trackage in " + format_money(now.lines[static_cast<std::size_t>(sim::Ledger::TrackageIncome)]) +
         " out " + format_money(now.lines[static_cast<std::size_t>(sim::Ledger::TrackagePaid)]));
    // The share price over the last ten years, month by month (rt3-clone-spec §15.4 View chart).
    {
        const float cx0 = x0 + 720, cx1 = x0 + w - 14, cy0 = chart_top, cy1 = y - 2;
        ui::chip(cx0, cy0, cx1, cy1, 0.45f);
        const std::vector<sim::Money>& prices = c.price_months();
        glColor3f(0.85f, 0.83f, 0.78f);
        draw_text(cx0 + 8, cy0 + 3, "Share price", kScale);
        if (prices.size() < 2) {
            glColor3f(0.6f, 0.58f, 0.54f);
            draw_text(cx0 + 8, cy0 + 22, "A chart after a month or two of trading", kScale);
        } else {
            std::int64_t lo = prices.front().in_cents(), hi = lo;
            for (const sim::Money& p : prices) lo = std::min(lo, p.in_cents()), hi = std::max(hi, p.in_cents());
            if (hi == lo) hi = lo + 1;
            const float px0 = cx0 + 74, px1 = cx1 - 8, py0 = cy0 + 22, py1 = cy1 - 18;
            const auto px = [&](std::size_t i) {
                return px0 + (px1 - px0) * static_cast<float>(i) / static_cast<float>(prices.size() - 1);
            };
            const auto py = [&](std::int64_t cents) {
                return py1 - (py1 - py0) * static_cast<float>(cents - lo) / static_cast<float>(hi - lo);
            };
            // Faint lines at the top and bottom, the high and low marked.
            fill_rect(px0, py0, px1, py0 + 1, 1.0f, 1.0f, 1.0f, 0.12f);
            fill_rect(px0, py1, px1, py1 + 1, 1.0f, 1.0f, 1.0f, 0.12f);
            glColor3f(0.7f, 0.68f, 0.62f);
            draw_text(cx0 + 8, py0 - 6, format_cents(sim::Money::cents(hi)), kScale);
            draw_text(cx0 + 8, py1 - 10, format_cents(sim::Money::cents(lo)), kScale);
            float r, g, b;
            owner_rgb(c.id(), world_.player_company(), r, g, b);
            glLineWidth(2.0f);
            glColor3f(r, g, b);
            glBegin(GL_LINE_STRIP);
            for (std::size_t i = 0; i < prices.size(); ++i) glVertex2f(px(i), py(prices[i].in_cents()));
            glEnd();
            glLineWidth(1.0f);
            // When the line starts: so many months back from now.
            const auto months = static_cast<std::int32_t>(prices.size());
            const std::int32_t m0 = world_.date().year() * 12 + world_.date().month() - 1 - months;
            static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            glColor3f(0.6f, 0.58f, 0.54f);
            draw_text(px0, cy1 - 17, std::string(kMonths[m0 % 12]) + " " + std::to_string(m0 / 12), kScale);
            const std::string end = "now";
            draw_text(px1 - static_cast<float>(text_width(end, kScale)), cy1 - 17, end, kScale);
        }
    }
    y += kRow / 2;
    glColor3f(1.0f, 0.9f, 0.5f);
    line("Players");
    for (const sim::Investor& inv : world_.investors()) {
        glColor3f(0.85f, 0.85f, 0.85f);
        line(inv.name + "   net worth " + format_money(sim::net_worth(inv, world_.market())) + "   cash " +
             format_money(inv.cash) + "   " +
             (inv.chairs ? "Runs " + world_.company(*inv.chairs).name() : std::string("No company")));
    }
    // The actions, for the mouse: what the keys do, on the company chosen above.
    const float by = y0 + h - 38;
    ui::rule(x0 + 10, x0 + w - 10, by - 8);
    key_buttons(x0 + 10, x0 + w - 10, by,
                {{"Buy 1,000 shares", SDLK_a}, {"Sell 1,000 shares", SDLK_s}, {"Take over", SDLK_t}, {"Merge (+20%)", SDLK_m},
                 {"Resign (click twice)", SDLK_q}, {"Found a new company", SDLK_n}});
}

void Tools::cycle_overlay(int direction) {
    // Off; profitability, grade and traffic (rt3-clone-spec §15.3); then
    // each cargo the economy is trading this year; then off again.
    struct Choice {
        MapOverlay mode;
        sim::CargoId cargo;
    };
    std::vector<Choice> choices{{MapOverlay::None, 0},
                                {MapOverlay::Holdings, 0},
                                {MapOverlay::Profit, 0},
                                {MapOverlay::Grade, 0},
                                {MapOverlay::Traffic, 0},
                                {MapOverlay::Travel, 0}};
    for (const auto& c : world_.data().cargo.all()) {
        if (world_.economy().active(c.id)) choices.push_back({MapOverlay::Cargo, c.id});
    }
    const auto n = static_cast<int>(choices.size());
    int at = 0;
    for (int i = 0; i < n; ++i) {
        const Choice& c = choices[static_cast<std::size_t>(i)];
        if (c.mode == map_overlay_ && (c.mode != MapOverlay::Cargo || (overlay_ && c.cargo == *overlay_))) at = i;
    }
    at = ((at + direction) % n + n) % n;
    map_overlay_ = choices[static_cast<std::size_t>(at)].mode;
    if (map_overlay_ == MapOverlay::Cargo) overlay_ = choices[static_cast<std::size_t>(at)].cargo;
    else overlay_.reset();
}

std::string Tools::cell_text() const {
    const sim::Economy& eco = world_.economy();
    const std::int32_t cx = eco.cell_x(hover_), cy = eco.cell_y(hover_);
    std::string s;
    for (const sim::Site& site : eco.sites()) {
        if (site.cx != cx || site.cy != cy) continue;
        const sim::IndustryType& t = world_.data().industries.get(site.type);
        if (!s.empty()) s += ", ";
        s += t.kind == sim::IndustryKind::House ? std::to_string(site.level) + " houses" : t.name;
        if (!t.outputs.empty() && t.kind != sim::IndustryKind::House) {
            s += " (makes";
            for (sim::CargoId o : t.outputs) s += " " + world_.data().cargo.get(o).name;
            s += ")";
        }
    }
    // The territory here, and whether the company may build in it.
    if (const sim::TerritoryId terr = world_.territory_at(hover_); terr != sim::kNoTerritory) {
        const sim::Territory& t = world_.territories().territories[terr];
        if (!s.empty()) s += ".  ";
        s += "Territory " + t.name + ": ";
        if (t.open()) s += "Open";
        else if (world_.player_company() && world_.company().has_access(terr)) s += "Yours";
        else s += "Access " + format_money(t.access_cost) + ", T to buy";
        if (t.station_cost_percent != 100) s += ", stations +" + std::to_string(t.station_cost_percent - 100) + "%";
        if (t.overhead_percent != 100) s += ", overhead +" + std::to_string(t.overhead_percent - 100) + "%";
        if (t.credit_grades != 0) s += std::string(", credit ") + (t.credit_grades > 0 ? "+" : "") + std::to_string(t.credit_grades);
        if (t.closed_border) s += ", borders closed";
    }
    if (map_overlay_ == MapOverlay::Travel) {
        // Where the travellers waiting at the station under the cursor want to go, most first.
        if (const auto st = station_at_cursor()) {
            const sim::Railway& rw = world_.railway();
            std::vector<std::pair<std::int32_t, std::string>> to;
            for (const sim::ExpressWaiting& e : rw.station(*st).express) {
                if (e.milli < sim::kMilli / 10 || e.destination >= rw.stations().size()) continue;
                to.emplace_back(e.milli, world_.data().cargo.get(e.cargo).name + " to " + rw.station(e.destination).name);
            }
            std::sort(to.begin(), to.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
            if (!s.empty()) s += ".  ";
            s += rw.station(*st).name + (to.empty() ? ": nobody waiting" : " waiting: ");
            for (std::size_t i = 0; i < std::min<std::size_t>(to.size(), 4); ++i) {
                const std::int32_t tenths = to[i].first / (sim::kMilli / 10);
                s += (i > 0 ? ", " : "") + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " " + to[i].second;
            }
        }
    }
    if (map_overlay_ == MapOverlay::Grade || map_overlay_ == MapOverlay::Traffic) {
        const sim::Railway& rw = world_.railway();
        if (const auto p = rw.track().nearest_edge_point(hover_, snap_mm())) {
            if (!s.empty()) s += ".  ";
            if (map_overlay_ == MapOverlay::Grade) {
                const std::int32_t bp = std::abs(rw.track().grade_bp({p->edge, true}));
                s += "Grade " + std::to_string(bp / 100) + "." + std::to_string(bp % 100 / 10) + "%";
            } else {
                s += "Track: " + std::to_string(rw.traffic_last_month(p->edge)) + " trains last month";
                const std::uint32_t held = rw.held_last_month(p->edge);
                if (held > 0) s += ", held at meets " + std::to_string(held * 24 / sim::World::kTicksPerDay) + " hours";
                if (rw.needs_double_track(p->edge)) s += ", needs double track";
            }
        }
    }
    if (overlay_) {
        const sim::CargoType& c = world_.data().cargo.get(*overlay_);
        if (!s.empty()) s += ".  ";
        s += c.name + " " + format_money(sim::Money::dollars(eco.price(c.id, cx, cy))) + " a load, " +
             std::to_string(eco.stock_milli(c.id, cx, cy) / sim::kMilli) + " waiting";
    }
    return s;
}

void Tools::draw_world_overlay() const {
    if (founding_) return;
    if (link_) draw_link();

    const sim::TrackNetwork& net = world_.railway().track();
    switch (tool_) {
    case Tool::Track:
        // Track about to be removed, in red.
        if (SDL_GetTicks() <= remove_armed_until_) {
            for (sim::EdgeId e : remove_armed_) {
                const sim::TrackEdge& edge = net.edge(e);
                if (!edge.removed) draw_line(net.node(edge.a).pos, net.node(edge.b).pos, cam_, 1.0f, 0.15f, 0.1f, 0.9f);
            }
        }
        if (const auto cmd = pending_track()) {
            const sim::PlanResult plan = world_.preview(*cmd);
            if (plan.plan) draw_plan(cmd->start.pos, *plan.plan, cam_);
            else draw_line(cmd->start.pos, cmd->end.pos, cam_, 0.9f, 0.1f, 0.1f, 0.8f);
            draw_marker(cmd->start.pos, cam_, 4, 1.0f, 1.0f, 0.3f);
        }
        [[fallthrough]];
    case Tool::Station:
        if (tool_ == Tool::Station && !station_building_) { // not for the Track tool, which falls through here
            // The ground a station here would serve, at the size chosen.
            const sim::MapPoint c = hover_pick_.kind == sim::TrackEnd::Kind::Free ? hover_ : hover_pick_.pos;
            const auto r = static_cast<double>(world_.economy().cells_mm(sim::catchment_radius(station_size_, world_.data().balance)));
            constexpr int kSides = 40;
            for (int i = 0; i < kSides; ++i) {
                const double a0 = 6.283185307 * i / kSides, a1 = 6.283185307 * (i + 1) / kSides;
                const sim::MapPoint p0{c.x_mm + static_cast<std::int64_t>(r * std::cos(a0)), c.y_mm + static_cast<std::int64_t>(r * std::sin(a0))};
                const sim::MapPoint p1{c.x_mm + static_cast<std::int64_t>(r * std::cos(a1)), c.y_mm + static_cast<std::int64_t>(r * std::sin(a1))};
                draw_line(p0, p1, cam_, 0.45f, 0.85f, 1.0f, 0.85f);
            }
        }
        [[fallthrough]];
    case Tool::ServiceTower:
    case Tool::Maintenance: {
        // What a second X would take down, in red.
        if (SDL_GetTicks() <= remove_site_until_ && remove_site_) {
            const sim::Railway& rw = world_.railway();
            if (tool_ == Tool::Station && *remove_site_ < rw.stations().size()) {
                draw_marker(net.node(rw.station(*remove_site_).node).pos, cam_, 24, 1.0f, 0.15f, 0.1f);
            } else if (tool_ != Tool::Station && *remove_site_ < rw.service_buildings().size()) {
                draw_marker(net.node(rw.service_buildings()[*remove_site_].node).pos, cam_, 16, 1.0f, 0.15f, 0.1f);
            }
        }
        // Show what a click would attach to: a node, a point on track, or open ground.
        switch (hover_pick_.kind) {
        case sim::TrackEnd::Kind::Node: draw_marker(hover_pick_.pos, cam_, 4, 1.0f, 1.0f, 1.0f); break;
        case sim::TrackEnd::Kind::OnEdge: draw_marker(hover_pick_.pos, cam_, 4, 1.0f, 0.85f, 0.2f); break;
        case sim::TrackEnd::Kind::Free: draw_marker(hover_pick_.pos, cam_, 2, 0.6f, 0.6f, 0.6f); break;
        }
        return;
    }
    case Tool::Train: {
        for (std::size_t i = 0; i < stops_.size(); ++i) {
            const sim::MapPoint p = net.node(world_.railway().station(stops_[i].station).node).pos;
            if (i == stop_sel_) draw_marker(p, cam_, 11, 1.0f, 1.0f, 1.0f); // the stop the consist keys change
            if (stops_[i].service) draw_marker(p, cam_, 7, 0.3f, 0.9f, 1.0f);
            else draw_marker(p, cam_, 7, 1.0f, 0.85f, 0.2f);
            if (i + 1 < stops_.size()) {
                draw_line(p, net.node(world_.railway().station(stops_[i + 1].station).node).pos, cam_, 1.0f, 0.85f, 0.2f, 0.8f);
            }
        }
        if (const auto s = station_at_cursor()) draw_marker(net.node(world_.railway().station(*s).node).pos, cam_, 8, 1, 1, 1);
        return;
    }
    case Tool::Inspect: {
        // What the detail panel shows, ringed on the map.
        if (!detail_) return;
        const sim::Railway& rw = world_.railway();
        const sim::Economy& eco = world_.economy();
        switch (detail_->kind) {
        case DetailKind::Train:
            if (detail_train()) draw_marker(rw.train_position(detail_->id), cam_, 12, 1.0f, 0.85f, 0.3f);
            break;
        case DetailKind::Station: draw_marker(net.node(rw.station(detail_->id).node).pos, cam_, 12, 1.0f, 0.85f, 0.3f); break;
        case DetailKind::Industry: {
            const sim::Site& s = eco.sites()[detail_->id];
            draw_marker(eco.node_centre(s.cx, s.cy), cam_, 12, 1.0f, 0.85f, 0.3f);
            break;
        }
        }
        return;
    }
    case Tool::Status:
    case Tool::Lists:
    case Tool::Finance:
    case Tool::Ledger:
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
    if (!id) {
        // What a click here would build, or why it can't.
        const auto types = buildable_types();
        if (types.empty()) return "Open land: no plants can be built yet";
        const sim::IndustryTypeId t = types[build_choice_ % types.size()];
        const sim::BuildIndustry cmd{.type = t, .cx = world_.economy().cell_x(hover_), .cy = world_.economy().cell_y(hover_)};
        if (const auto why = world_.industry_problem(cmd)) return "Cannot build here: " + *why;
        return "Open land: a " + world_.data().industries.get(t).name + " here costs " +
               format_money(world_.construction_cost(sim::industry_build_cost(world_.data().balance.industries)));
    }
    const sim::Site& s = world_.economy().sites()[*id];
    const sim::Balance::Industries& b = world_.data().balance.industries;
    const sim::IndustryType& type = world_.data().industries.get(s.type);
    std::string text = type.name + " (level " + std::to_string(s.level) + "): ";
    if (type.kind == sim::IndustryKind::Sink) return text + "A consumer. Not for sale";
    if (!s.owner) {
        if (type.kind == sim::IndustryKind::Port) text += std::string(sim::port_mode_name(s.port_mode)) + ", ";
        text += "For sale at " + format_money(sim::industry_price(s, b));
    } else if (s.owner == world_.player_company()) {
        text += "Yours";
        if (type.kind == sim::IndustryKind::Warehouse || type.kind == sim::IndustryKind::Port) {
            if (s.port_mode == sim::PortMode::Convert) {
                text += ", converts " + world_.data().cargo.get(s.convert_from).name + " into " +
                        world_.data().cargo.get(s.convert_to).name;
            } else {
                text += std::string(", ") + sim::port_mode_name(s.port_mode);
            }
            text += type.kind == sim::IndustryKind::Warehouse ? " (M mode, C convert)" : " (M mode)";
        }
        if (sim::buildable(type.kind) || type.kind == sim::IndustryKind::Port) {
            text += ", upgrade " + format_money(world_.construction_cost(sim::industry_upgrade_cost(s, b)));
        }
    } else {
        text += "Owned by " + world_.company(*s.owner).name();
    }
    return text + ".  Profit " + format_money(sim::annual_profit(s)) + " a year, running at " +
           std::to_string(s.utilisation_permille / 10) + "%";
}

// The speed-vs-grade chart of the Add Train window [D], as text: the
// chosen engine's speed with the chosen number of freight and of express
// cars, on the flat and up 1%, 2% and 3%.
std::size_t Tools::list_size() const {
    switch (list_kind_) {
    case 0: {
        std::size_t n = 0;
        for (const sim::Train& t : world_.railway().trains()) n += t.in_service();
        return n;
    }
    case 1: return world_.railway().stations().size();
    default: return world_.companies().size();
    }
}

bool Tools::lists_key(SDL_Keycode key) {
    const std::size_t n = list_size();
    switch (key) {
    case SDLK_TAB:
        list_kind_ = (list_kind_ + 1) % 3;
        list_sel_ = 0;
        return true;
    case SDLK_UP:
        if (n > 0) list_sel_ = (list_sel_ + n - 1) % n;
        return true;
    case SDLK_DOWN:
        if (n > 0) list_sel_ = (list_sel_ + 1) % n;
        return true;
    case SDLK_PAGEUP: list_sel_ = list_sel_ > 20 ? list_sel_ - 20 : 0; return true;
    case SDLK_PAGEDOWN: list_sel_ = n == 0 ? 0 : std::min(n - 1, list_sel_ + 20); return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
        if (n == 0) return true;
        const sim::Railway& rw = world_.railway();
        std::optional<Detail> open;
        if (list_kind_ == 0) {
            std::size_t i = 0;
            for (const sim::Train& t : rw.trains()) {
                if (!t.in_service()) continue;
                if (i++ == list_sel_) {
                    jump_ = rw.train_position(t.id);
                    open = Detail{DetailKind::Train, t.id};
                }
            }
        } else if (list_kind_ == 1) {
            jump_ = rw.track().node(rw.stations()[list_sel_].node).pos;
            open = Detail{DetailKind::Station, static_cast<std::uint32_t>(list_sel_)};
        } else {
            // A company: its market entry.
            market_choice_ = static_cast<sim::CompanyId>(list_sel_);
            select(Tool::Market);
            return true;
        }
        if (jump_) hover_ = *jump_;
        select(Tool::Inspect);
        if (open) open_detail(open->kind, open->id);
        return true;
    }
    default: return false;
    }
}

void Tools::draw_lists_panel() const {
    constexpr float kRow = 16.0f;
    constexpr std::size_t kShown = 36;
    const float x0 = 40, y0 = 56, w = 1180;
    const std::size_t n = list_size();
    const std::size_t first = list_sel_ >= kShown / 2 ? std::min(list_sel_ - kShown / 2, n > kShown ? n - kShown : 0) : 0;
    const float h = kRow * static_cast<float>(std::min(n, kShown) + 3);
    ui::panel(x0, y0, x0 + w, y0 + h);
    float y = y0 + 10;
    // First column left-aligned; the rest right-aligned at these edges.
    std::vector<float> cols;
    const auto cells = [&](const std::vector<std::string>& v, float r, float g, float b) {
        glColor3f(r, g, b);
        draw_text(cols[0], y, v[0], kScale);
        for (std::size_t i = 1; i < v.size() && i < cols.size(); ++i) {
            draw_text(cols[i] - static_cast<float>(text_width(v[i], kScale)), y, v[i], kScale);
        }
        y += kRow;
    };
    const auto row_colour = [&](std::size_t i, bool mine, const std::vector<std::string>& v) {
        const bool sel = i == list_sel_;
        add_row(x0 + 6, y - 2, x0 + w - 6, y + kRow - 3, RowPanel::Lists, i);
        if (sel) ui::selection(x0 + 6, y - 2, x0 + w - 6, y + kRow - 3);
        cells(v, sel ? 1.0f : (mine ? 0.9f : 0.65f), sel ? 1.0f : (mine ? 0.9f : 0.65f), sel ? 0.7f : (mine ? 0.9f : 0.7f));
    };
    const auto owner_name = [&](sim::CompanyId c) { return as_written(world_.company(c).name()); };
    const auto mine = [&](sim::CompanyId c) { return world_.player_company() && c == *world_.player_company(); };
    const sim::Railway& rw = world_.railway();
    if (list_kind_ == 0) {
        cols = {x0 + 14, x0 + 90, x0 + 500, x0 + 720, x0 + 880, x0 + 960, x0 + 1160};
        cells({"Trains", "", "Owner", "Engine", "State", "Stops", "Profit 12M"}, 1.0f, 0.9f, 0.5f);
        std::size_t i = 0;
        for (const sim::Train& t : rw.trains()) {
            if (!t.in_service()) continue;
            if (i >= first && i < first + kShown) {
                row_colour(i, mine(t.owner),
                           {std::to_string(t.id + 1), "", owner_name(t.owner), as_written(world_.data().locomotives.get(t.loco).name),
                            state_name(t.state), std::to_string(t.route.size()), format_money(t.profit_last_year())});
            }
            ++i;
        }
    } else if (list_kind_ == 1) {
        cols = {x0 + 14, x0 + 500, x0 + 640, x0 + 900, x0 + 1160};
        cells({"Stations", "Owner", "Size", "Town", "Income 12M"}, 1.0f, 0.9f, 0.5f);
        const char* sizes[] = {"Small", "Medium", "Large"};
        for (std::size_t i = first; i < n && i < first + kShown; ++i) {
            const sim::Station& s = rw.stations()[i];
            row_colour(i, mine(s.owner),
                       {as_written(s.name), owner_name(s.owner), sizes[static_cast<int>(s.size)],
                        s.town ? as_written(world_.economy().towns()[*s.town].name) : "", format_money(s.income_last_year())});
        }
    } else {
        cols = {x0 + 14, x0 + 600, x0 + 790, x0 + 960, x0 + 1060, x0 + 1160};
        cells({"Companies", "Cash", "Revenue 12M", "Book value", "Trains", "Stations"}, 1.0f, 0.9f, 0.5f);
        for (std::size_t i = first; i < n && i < first + kShown; ++i) {
            const sim::Company& c = world_.companies()[i];
            std::size_t trains = 0, stations = 0;
            for (const sim::Train& t : rw.trains()) trains += t.in_service() && t.owner == c.id();
            for (const sim::Station& s : rw.stations()) stations += s.owner == c.id();
            if (c.defunct()) {
                row_colour(i, false, {as_written(c.name()) + " (gone)", "", "", "", "", ""});
                continue;
            }
            row_colour(i, mine(c.id()),
                       {as_written(c.name()), format_money(c.cash()), format_money(c.trailing_revenue().value_or(sim::Money{})),
                        format_money(c.book_value()), std::to_string(trains), std::to_string(stations)});
        }
    }
    if (n == 0) cells({"None yet"}, 0.7f, 0.7f, 0.7f);
}

int Tools::train_trouble(const sim::Train& t, std::string* advice) const {
    if (!t.in_service()) return 0;
    const sim::LocomotiveType& loco = world_.data().locomotives.get(t.loco);
    const auto say = [&](const char* text) {
        if (advice) *advice = text;
    };
    if (t.state == sim::TrainState::NoRoute) {
        say("No way to its next stop: lay the missing track, or change its route (E)");
        return 2;
    }
    if (loco.fuel == sim::Fuel::Steam && t.water == 0) {
        say("Out of water, so it crawls: build a service tower (F4) by one of its stations");
        return 2;
    }
    if (t.state == sim::TrainState::BrokenDown) {
        say("Broken down: it will be repaired where it stands. A maintenance shed (F5) on the line means fewer");
        return 1;
    }
    if (t.sand == 0) {
        say("Out of sand, so hills slow it badly: a service tower (F4) on its line refills it");
        return 1;
    }
    if (t.oil == 0) {
        say("Out of oil: it breaks down more often. A maintenance shed (F5) on its line refills it");
        return 1;
    }
    return 0;
}

bool Tools::tower_near(const std::vector<sim::StationId>& route) const {
    const sim::Railway& rw = world_.railway();
    const std::int64_t reach = world_.economy().cells_mm(3);
    for (const sim::ServiceBuilding& sb : rw.service_buildings()) {
        if (sb.type != sim::ServiceType::ServiceTower) continue;
        const sim::MapPoint at = rw.track().node(sb.node).pos;
        for (const sim::StationId s : route) {
            if (s < rw.stations().size() && sim::distance_mm(at, rw.track().node(rw.station(s).node).pos) <= reach) return true;
        }
    }
    return false;
}

std::vector<sim::StationId> Tools::pending_route() const {
    std::vector<sim::StationId> out;
    for (const PendingStop& p : stops_) out.push_back(p.station);
    return out;
}

std::vector<bool> Tools::pending_service() const {
    std::vector<bool> out;
    for (const PendingStop& p : stops_) out.push_back(p.service);
    return out;
}

bool Tools::apply_consists(sim::TrainId id) {
    for (std::size_t i = 0; i < stops_.size(); ++i) {
        const sim::CommandResult r = world_.execute(sim::SetConsist{.train = id, .stop = i, .rule = stops_[i].rule});
        if (!r.ok) {
            show("Stop " + std::to_string(i + 1) + " consist: " + r.error, false);
            return false;
        }
    }
    return true;
}

// The selected stop and what the train takes on there: "STOP 2/3 LAKECREST:
// EXPRESS 0-4 CARS".
std::string Tools::stop_text() const {
    const PendingStop& p = stops_[stop_sel_];
    std::string s = "Stop " + std::to_string(stop_sel_ + 1) + "/" + std::to_string(stops_.size()) + " " +
                    as_written(world_.railway().station(p.station).name) + ": ";
    const auto& cargo = world_.data().cargo;
    if (p.rule.custom) {
        for (std::size_t i = 0; i < p.rule.cars.size(); ++i) s += (i ? "," : "") + as_written(cargo.get(p.rule.cars[i]).name);
        if (p.rule.cars.empty()) s += "No cars";
        s += " cars";
    } else {
        s += as_written(sim::cargo_filter_name(p.rule.filter)) + " " + std::to_string(p.rule.min) + "-" +
             std::to_string(p.rule.max) + " cars";
    }
    if (p.service) s += ", service stop";
    return s;
}

std::string Tools::speed_chart_text() const {
    const auto locos = available_locos();
    if (locos.empty()) return "";
    const sim::LocomotiveType& l = world_.data().locomotives.get(locos[loco_choice_ % locos.size()]);
    const sim::Balance& b = world_.data().balance;
    const std::int32_t year = world_.date().year();
    const std::int64_t mph = std::max<std::int64_t>(1, sim::mph_to_mm_per_tick(1, b));
    const auto row = [&](bool express) {
        std::string out;
        for (std::int32_t grade = 0; grade <= 300; grade += 100) {
            out += (grade == 0 ? "" : "/") +
                   std::to_string(sim::chart_speed_mm_per_tick(l, cars_, express, year, grade, b) / mph);
        }
        return out;
    };
    const std::string appeal = (l.appeal_percent > 0 ? "+" : "") + std::to_string(l.appeal_percent) + "%";
    return "Speed with " + std::to_string(cars_) + " cars, flat/1%/2%/3%: freight " + row(false) + " mph, express " +
           row(true) + " mph.  Appeal " + appeal;
}

std::vector<Tools::Button> Tools::layout_buttons() const {
    static constexpr std::pair<Tool, const char*> kButtons[] = {
        {Tool::Inspect, "F1 Look"},       {Tool::Track, "F2 Track"},       {Tool::Station, "F3 Station"},
        {Tool::ServiceTower, "F4 Tower"}, {Tool::Maintenance, "F5 Shed"},  {Tool::Train, "F6 Train"},
        {Tool::Finance, "F7 Company"},      {Tool::Market, "F8 Market"},     {Tool::Industry, "F9 Industry"},
        {Tool::Status, "F10 Status"},     {Tool::Lists, "F11 Lists"},      {Tool::Ledger, "L Ledger"},
    };
    std::vector<Button> out;
    float x = 6.0f;
    for (const auto& [t, label] : kButtons) {
        const float w = static_cast<float>(text_width(label, kScale)) + 8.0f;
        out.push_back({t, label, x, kToolbarY, x + w, kToolbarY + kButtonH});
        x += w + 3.0f;
    }
    return out;
}

std::array<float, 4> Tools::help_button() const {
    const std::vector<Button> buttons = layout_buttons();
    const float x = buttons.back().x1 + 3.0f;
    return {x, kToolbarY, x + static_cast<float>(text_width("? Help", kScale)) + 8.0f, kToolbarY + kButtonH};
}

namespace {

// What each tool does, for the tooltip under its button.
const char* tool_tip(Tool t) {
    switch (t) {
    case Tool::Inspect: return "Look: click a train, station or industry for its details";
    case Tool::Track: return "Track: click to start, click again to lay track to there; right-click stops";
    case Tool::Station: return "Station: click on your track to build one; [ ] size, B other buildings, X twice removes";
    case Tool::ServiceTower: return "Service tower: water and sand for trains, built by the line; X twice removes";
    case Tool::Maintenance: return "Shed: maintenance for trains, fewer breakdowns; X twice removes";
    case Tool::Train: return "Train: click stations in order, L picks the engine, Enter buys it";
    case Tool::Finance: return "Company: books, bonds, shares, dividend and territories";
    case Tool::Market: return "Market: buy and sell shares, take over or merge railroads";
    case Tool::Industry: return "Industry: buy, build or upgrade mines, farms and works";
    case Tool::Status: return "Status: the scenario's goals and medals";
    case Tool::Lists: return "Lists: every train, station and company; click one to go there";
    case Tool::Ledger: return "Ledger: the year's accounts and reports";
    }
    return "";
}

} // namespace

void Tools::draw_help() const {
    struct Section {
        const char* title;
        std::vector<std::pair<const char*, const char*>> keys;
    };
    const Section sections[] = {
        {"Building",
         {{"F2", "Track: click to start and to lay; Ctrl+Z undo"},
          {"F3", "Station on your track; [ ] size"},
          {"F4  F5", "Service tower and maintenance shed"},
          {"F6", "Train: click stations, L engine, Enter buys"},
          {"F9", "Industries: buy, build, upgrade"}}},
        {"Running the railroad",
         {{"F1", "Look: click for details; E route, R engine, U enlarge"},
          {"F7", "Company: bonds, shares, dividend, territories"},
          {"F8", "Market: shares, takeovers, mergers"},
          {"L  F10  F11", "Ledger, goals, lists"},
          {"M  Ctrl+G", "Messages; the Guide: who wants what, best links"}}},
        {"Getting around",
         {{"Arrows", "Pan (or the mouse at the window's edge)"},
          {"Wheel", "Zoom"},
          {"Shift+arrows", "Turn and tilt (or right-drag)"},
          {"Ctrl+F", "Find a town or station"},
          {"0-9", "Go to a bookmark; Shift+0-9 stores one"},
          {"V", "Flat map or 3D"}}},
        {"Time and view",
         {{"Space", "Pause; + and - set the speed"},
          {"N", "Time of day"},
          {"O  P", "Map overlays"},
          {"Tab", "Hide the interface"},
          {"Ctrl+H", "Hide the trees"},
          {"Esc", "Menu: save, load, settings; Ctrl+S quick save"}}},
    };
    const float w = std::min(1100.0f, static_cast<float>(cam_.width_px) - 20), h = 400;
    const float x0 = (static_cast<float>(cam_.width_px) - w) / 2, y0 = std::max(60.0f, (static_cast<float>(cam_.height_px) - h) / 2);
    fill_rect(0, 0, static_cast<float>(cam_.width_px), static_cast<float>(cam_.height_px), 0.0f, 0.0f, 0.0f, 0.35f);
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 18, y0 + 10, "Help: the main keys", kScale + 1);
    ui::rule(x0 + 10, x0 + w - 10, y0 + 40);
    for (int i = 0; i < 4; ++i) {
        const float cx = x0 + 20 + static_cast<float>(i % 2) * (w / 2);
        float y = y0 + 52 + static_cast<float>(i / 2) * 160;
        glColor3f(1.0f, 0.86f, 0.5f);
        draw_text(cx, y, sections[i].title, kScale);
        y += 24;
        for (const auto& [key, what] : sections[i].keys) {
            glColor3f(1.0f, 0.95f, 0.75f);
            draw_text(cx + 6, y, key, kScale);
            glColor3f(0.88f, 0.86f, 0.8f);
            draw_text(cx + 130, y, what, kScale);
            y += 21;
        }
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 18, y0 + h - 26, "The bottom bar always shows the current tool's keys.   ? or Esc or a click closes this", kScale);
}

std::string Tools::hint() const {
    switch (tool_) {
    case Tool::Inspect: {
        std::string cargo_map = "O/P map: ";
        switch (map_overlay_) {
        case MapOverlay::None: cargo_map += "Off"; break;
        case MapOverlay::Holdings: cargo_map += "Holdings"; break;
        case MapOverlay::Profit: cargo_map += "Profit"; break;
        case MapOverlay::Grade: cargo_map += "Grade"; break;
        case MapOverlay::Traffic: cargo_map += "Traffic"; break;
        case MapOverlay::Travel: cargo_map += "Travel"; break;
        case MapOverlay::Cargo: cargo_map += overlay_ ? world_.data().cargo.get(*overlay_).name : "Off"; break;
        }
        if (link_ && !detail_) return "B build the planned line (gold) for you   Esc let it go   " + cargo_map;
        if (detail_) {
            std::string keys = link_ ? "B build the planned line   Click another to show it  Esc close"
                                     : "Click another to show it  Esc close";
            if (detail_->kind == DetailKind::Station) keys = "U enlarge  Enter rename  Click another to show it  Esc close";
            if (detail_train()) keys = "E route  H priority  F filter  W full  [ ] cars  C caboose  D diner  Y copy  XX retire  R engine  Space follow  Esc close";
            return keys;
        }
        return cargo_map + "   Click a train, station or industry for its details.  Ctrl+F find.  Train: E route H priority Y copy XX retire R engine";
    }
    case Tool::Track:
        return std::string(track_start_ ? "Right-click stops" : "Click to start") + "  D double " +
               (double_track_ ? "On" : "Off") + "  C curves " + (curves_ ? "On" : "Off") + "  T tunnel " +
               as_written(sim::frequency_name(tunnels_)) + "  B bridge " + as_written(sim::frequency_name(bridges_)) + "  G overpass " +
               as_written(sim::frequency_name(overpasses_));
    case Tool::Station:
        if (station_building_) {
            return std::string("Click near a station to build a ") + sim::station_building_name(*station_building_) + " " +
                   format_money(world_.construction_cost(sim::station_building_cost(*station_building_, world_.data().balance))) +
                   ".  B next kind";
        }
        return std::string("Click on track to build a station.  B for buildings.  [ ] size: ") + size_name(station_size_) + " " +
               format_money(sim::station_cost(station_size_)) + ".  Ctrl+Z undo  X twice: take one down";
    case Tool::ServiceTower:
        return "Click on track: service tower (water, sand) " +
               format_money(sim::service_building_cost(sim::ServiceType::ServiceTower)) + ".  X twice: take one down";
    case Tool::Maintenance:
        return "Click on track: maintenance facility (oil) " +
               format_money(sim::service_building_cost(sim::ServiceType::MaintenanceFacility)) + ".  X twice: take one down";
    case Tool::Train: {
        const auto locos = available_locos();
        std::string loco = "None available";
        if (!locos.empty()) {
            const auto& l = world_.data().locomotives.get(locos[loco_choice_ % locos.size()]);
            loco = l.name + " " + std::to_string(l.top_speed_mph) + "Mph " + format_money(l.cost);
        }
        if (stops_.empty()) {
            return std::string(editing_ ? "Train " + std::to_string(*editing_ + 1) + ": " : "") +
                   "Click stations in order.  L engine: " + loco + "  [ ] cars: " + std::to_string(cars_);
        }
        const bool dry = !locos.empty() && stops_.size() >= 2 &&
                         world_.data().locomotives.get(locos[loco_choice_ % locos.size()]).fuel == sim::Fuel::Steam &&
                         !tower_near(pending_route());
        // An electric engine on a route not wired all the way: say so, as the panel does.
        const sim::LocoTypeId engine = editing_ ? world_.railway().train(*editing_).loco
                                                : (locos.empty() ? 0 : locos[loco_choice_ % locos.size()]);
        if (stops_.size() >= 2 && (editing_ || !locos.empty()) &&
            world_.data().locomotives.get(engine).fuel == sim::Fuel::Electric) {
            if (const auto why = world_.railway().electric_route_problem(pending_route())) {
                return stop_text() + "   Not wired" + why->substr(why->find(" from ")) + ": electrify it (Track tool, E)";
            }
        }
        return std::string(editing_ ? "Train " + std::to_string(*editing_ + 1) + " " : "") + stop_text() +
               (editing_ ? "  Enter saves" : "  Enter buys " + loco) + (dry ? "   No service tower on this route (F4)!" : "");
    }
    case Tool::Industry: {
        const auto types = buildable_types();
        const std::string plant =
            types.empty() ? std::string("None yet") : world_.data().industries.get(types[build_choice_ % types.size()]).name;
        return "Click an industry to buy it, yours to upgrade, open land to build.  [ ] " + plant + " " +
               format_money(world_.construction_cost(sim::industry_build_cost(world_.data().balance.industries)));
    }
    case Tool::Market:
        return "Up/Down choose  A/S buy/sell, below 0 is short  T takeover  M merge +20% (Shift +50%)  QQ resign  N new "
               "Company";
    case Tool::Status:
        return world_.scenario() ? "Scenario goals, checked at each month end" : "No scenario: a free game";
    case Tool::Lists:
        return "Tab trains/stations/companies   Up/Down, PgUp/PgDn, wheel or click choose   Enter or click again: go to it";
    case Tool::Finance:
        if (company_tab_ == 3) return "Tab next page   Up/Down choose a territory   T buy access to it";
        return "Tab next page   B/R bond issue/repay   A/S buy/sell 1,000 shares (Shift 5,000)   I/Y issue/buy back stock   "
               "[ ] dividend  D suggested   KK bankruptcy";
    case Tool::Ledger: return "Tab (or click) next page   L or Esc close";
    }
    return {};
}

std::optional<sim::MapPoint> Tools::take_jump() {
    if (jump_) return std::exchange(jump_, std::nullopt);
    if (follow_) {
        if (const auto id = detail_train()) return world_.railway().train_position(*id);
        follow_ = false;
    }
    return std::nullopt;
}

void Tools::open_detail(DetailKind kind, std::uint32_t id) {
    if (!detail_ || detail_->kind != kind || detail_->id != id) follow_ = false;
    detail_ = Detail{kind, id};
    renaming_ = false;
}

std::optional<Tools::Detail> Tools::detail_under_cursor() const {
    const sim::Railway& rw = world_.railway();
    for (const sim::Train& t : rw.trains()) {
        if (t.in_service() && sim::distance_mm(rw.train_position(t.id), hover_) <= snap_mm()) return Detail{DetailKind::Train, t.id};
    }
    if (const auto st = station_at_cursor()) return Detail{DetailKind::Station, *st};
    if (const auto site = site_under_cursor()) return Detail{DetailKind::Industry, *site};
    return std::nullopt;
}

bool Tools::open_detail_under_cursor() {
    const auto d = detail_under_cursor();
    if (!d) return false;
    open_detail(d->kind, d->id);
    return true;
}

std::vector<std::string> Tools::hover_card_lines(const Detail& d) const {
    const sim::Railway& rw = world_.railway();
    const sim::Economy& eco = world_.economy();
    const auto& cargo = world_.data().cargo;
    std::vector<std::string> out;
    switch (d.kind) {
    case DetailKind::Train: {
        const sim::Train& t = rw.train(d.id);
        out.push_back("Train " + std::to_string(t.id + 1) + " - " + world_.data().locomotives.get(t.loco).name);
        std::string state = state_name(t.state);
        if (!t.route.empty() && t.stop_index < t.route.size()) {
            const std::string at = rw.station(t.route[t.stop_index]).name;
            state = t.state == sim::TrainState::Dwelling ? "At " + at : state + ", for " + at;
        }
        out.push_back(state);
        if (t.owner != world_.company().id()) out.push_back(world_.company(t.owner).name());
        out.push_back("This year: earned " + format_money(t.accounts.revenue()) + ", profit " + format_money(t.accounts.profit()));
        std::string advice;
        if (train_trouble(t, &advice) > 0) out.push_back("! " + advice);
        break;
    }
    case DetailKind::Station: {
        const sim::Station& s = rw.station(d.id);
        out.push_back(s.name);
        out.push_back(std::string(size_name(s.size)) + " station" + (s.town ? ", " + eco.towns()[*s.town].name : std::string()) +
                      (s.owner != world_.company().id() ? ", " + world_.company(s.owner).name() : std::string()));
        // Whole loads waiting, the most first.
        std::vector<std::int64_t> milli(cargo.all().size(), 0);
        for (std::size_t c = 0; c < s.waiting.size() && c < milli.size(); ++c) milli[c] += s.waiting[c].milli;
        for (const sim::ExpressWaiting& e : s.express)
            if (e.cargo < milli.size()) milli[e.cargo] += e.milli;
        std::vector<std::pair<std::int64_t, std::string>> waiting;
        for (std::size_t c = 0; c < milli.size(); ++c)
            if (milli[c] >= sim::kMilli) waiting.emplace_back(milli[c] / sim::kMilli, cargo.get(static_cast<sim::CargoId>(c)).name);
        std::sort(waiting.begin(), waiting.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
        std::string w = "Waiting:";
        for (std::size_t i = 0; i < std::min<std::size_t>(waiting.size(), 4); ++i)
            w += std::string(i ? ", " : " ") + std::to_string(waiting[i].first) + " " + waiting[i].second;
        out.push_back(waiting.empty() ? "Nothing waiting" : w);
        out.push_back("Income this month " + format_money(s.income_this_month));
        break;
    }
    case DetailKind::Industry: {
        const sim::Site& s = eco.sites()[d.id];
        const sim::IndustryType& t = world_.data().industries.get(s.type);
        out.push_back(site_name(d.id));
        std::string makes, wants;
        for (const sim::CargoId c : t.outputs) makes += (makes.empty() ? "" : ", ") + cargo.get(c).name;
        for (const sim::IndustryInput& in : t.inputs) wants += (wants.empty() ? "" : ", ") + cargo.get(in.cargo).name;
        if (!makes.empty()) out.push_back("Makes " + makes);
        if (!wants.empty()) out.push_back("Wants " + wants);
        out.push_back(s.closed ? std::string("Closed") : s.owner ? "Owned by " + world_.company(*s.owner).name() : std::string("Not owned"));
        break;
    }
    }
    return out;
}

void Tools::draw_hover_card() const {
    if (tool_ != Tool::Inspect || founding_ || menu_open_ || help_open_ || guide_open_ || find_open_ || console_open_) return;
    if (SDL_GetTicks() - hover_moved_at_ < 350) return; // only once the mouse rests
    const float h = static_cast<float>(cam_.height_px);
    if (hover_sy_ < kToolbarY + kButtonH || hover_sy_ > h - kBottomBarH) return;
    if (detail_ && hover_sx_ >= detail_box_[0] && hover_sx_ <= detail_box_[2] && hover_sy_ >= detail_box_[1] &&
        hover_sy_ <= detail_box_[3]) {
        return; // over the detail panel
    }
    const auto d = detail_under_cursor();
    if (!d || (detail_ && detail_->kind == d->kind && detail_->id == d->id)) return; // its panel says it all
    const std::vector<std::string> lines = hover_card_lines(*d);
    float w = 0;
    for (const std::string& l : lines) w = std::max(w, static_cast<float>(text_width(l, kScale)));
    w += 20;
    const float lh = 21, ht = 12 + lh * static_cast<float>(lines.size());
    float x = hover_sx_ + 18, y = hover_sy_ + 18;
    if (x + w > static_cast<float>(cam_.width_px) - 6) x = hover_sx_ - 18 - w;
    if (y + ht > h - kBottomBarH - 4) y = hover_sy_ - 18 - ht;
    ui::chip(x, y, x + w, y + ht, 0.88f);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i == 0) glColor3f(1.0f, 0.86f, 0.5f);
        else if (lines[i].rfind("! ", 0) == 0) glColor3f(1.0f, 0.5f, 0.4f);
        else glColor3f(0.92f, 0.9f, 0.84f);
        draw_text(x + 10, y + 6 + lh * static_cast<float>(i), lines[i], kScale);
    }
}

std::optional<sim::TrainId> Tools::detail_train() const {
    if (!detail_ || detail_->kind != DetailKind::Train) return std::nullopt;
    if (detail_->id >= world_.railway().trains().size() || !world_.railway().train(detail_->id).in_service()) return std::nullopt;
    return detail_->id;
}

namespace {

// A line of a detail panel: a heading, a brass rule, a gauge, or cells in
// columns (the first left-aligned, the rest right-aligned at the panel's
// column edges).
struct DetailRow {
    enum class Style { Title, Heading, Text, Rule, Gauge, Bars } style = Style::Text;
    std::vector<std::string> cells{};
    float r = 0.9f, g = 0.88f, b = 0.82f;
    float fraction = 0; // gauges
    bool inverted = false; // a gauge where full is bad
    std::vector<double> bars{}; // Bars: a value each (dollars), oldest first
};

DetailRow title(std::string s) { return {DetailRow::Style::Title, {std::move(s)}, 1.0f, 0.86f, 0.5f}; }
DetailRow heading(std::string s) { return {DetailRow::Style::Heading, {std::move(s)}, 0.95f, 0.8f, 0.45f}; }
DetailRow rule_row() { return {DetailRow::Style::Rule}; }
DetailRow line(std::vector<std::string> cells) { return {DetailRow::Style::Text, std::move(cells)}; }
DetailRow dim(std::vector<std::string> cells) { return {DetailRow::Style::Text, std::move(cells), 0.68f, 0.66f, 0.62f}; }
DetailRow gauge(std::string name, std::int32_t value) {
    return {DetailRow::Style::Gauge, {std::move(name), percent(value)}, 0.9f, 0.88f, 0.82f,
            static_cast<float>(value) / static_cast<float>(sim::kGaugeFull)};
}
DetailRow money_row(std::string label, sim::Money m) {
    DetailRow r = line({std::move(label), format_money(m)});
    if (m < sim::Money{}) {
        r.r = 1.0f;
        r.g = 0.55f;
        r.b = 0.5f;
    }
    return r;
}

// Thousandths of a carload as loads, to one place under ten.
std::string loads(std::int64_t milli) {
    if (milli >= 10 * sim::kMilli) return format_count((milli + sim::kMilli / 2) / sim::kMilli);
    const std::int64_t tenths = (milli + sim::kMilli / 20) / (sim::kMilli / 10);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

const char* reliability_name(std::int32_t r) {
    if (r >= 150) return "excellent";
    if (r >= 115) return "good";
    if (r >= 85) return "average";
    if (r >= 60) return "poor";
    return "very poor";
}

// A cargo price in thousands, as RT3 shows them: "$34K".
std::string price_k(std::int64_t dollars) {
    if (dollars < 10'000) return format_money(sim::Money::dollars(dollars));
    return "$" + format_count((dollars + 500) / 1000) + "K";
}

std::string years(std::int32_t days) {
    const std::int32_t y = std::max(0, days) / 365;
    return y == 0 ? "new" : std::to_string(y) + (y == 1 ? " year" : " years");
}

} // namespace

void Tools::draw_detail_panel() const {
    const sim::Railway& rw = world_.railway();
    const sim::Economy& eco = world_.economy();
    const auto& cargo = world_.data().cargo;
    const std::int32_t today = world_.date().days_since_epoch();
    const auto owner_name = [&](sim::CompanyId c) { return as_written(world_.company(c).name()); };
    std::vector<DetailRow> rows;

    if (detail_->kind == DetailKind::Train) {
        const auto id = detail_train();
        if (!id) {
            rows.push_back(title("Train " + std::to_string(detail_->id + 1)));
            rows.push_back(dim({"Retired or wrecked"}));
        } else {
            const sim::Train& t = rw.train(*id);
            const sim::LocomotiveType& loco = world_.data().locomotives.get(t.loco);
            rows.push_back(title("Train " + std::to_string(t.id + 1)));
            rows.push_back(dim({owner_name(t.owner) + (follow_ ? "  (following)" : "")}));
            rows.push_back(rule_row());
            std::string state = state_name(t.state);
            if (!t.route.empty() && t.stop_index < t.route.size()) {
                const std::string at = as_written(rw.station(t.route[t.stop_index]).name);
                state = t.state == sim::TrainState::Dwelling ? "at " + at : state + ", for " + at;
            }
            if (t.holding) state += ", waiting for a full load";
            rows.push_back(line({"Status", state}));
            if (std::string advice; train_trouble(t, &advice) > 0) {
                // The trouble and what to do, wrapped across the panel, in red.
                for (const std::string& l : wrap("Problem: " + advice, kScale, 370.0f)) {
                    DetailRow problem = line({l});
                    problem.r = 1.0f;
                    problem.g = 0.55f;
                    problem.b = 0.45f;
                    rows.push_back(problem);
                }
            }
            rows.push_back(line({"Engine", as_written(loco.name)}));
            rows.push_back(line({"Age", years(today - t.built_day)}));
            rows.push_back(line({"Reliability", std::string(reliability_name(loco.reliability)) + ", " +
                                                    std::to_string(t.breakdowns) + (t.breakdowns == 1 ? " breakdown" : " breakdowns")}));
            rows.push_back(line({"Priority", std::to_string(t.priority)}));
            rows.push_back(heading("Gauges"));
            if (loco.fuel == sim::Fuel::Steam) rows.push_back(gauge("Water", t.water));
            rows.push_back(gauge("Sand", t.sand));
            rows.push_back(gauge("Oil", t.oil));
            {
                // Breakdown risk against a new average engine: the bar fills at four times.
                const std::int64_t risk = world_.railway().breakdown_risk_permille(t, loco);
                DetailRow r = gauge("Breakdown risk", 0);
                r.cells[1] = std::to_string(risk / 1000) + "." + std::to_string(risk % 1000 / 100) + "x";
                r.fraction = static_cast<float>(std::min<std::int64_t>(risk, 4000)) / 4000.0f;
                r.inverted = true;
                rows.push_back(r);
            }
            rows.push_back(heading("Route"));
            for (std::size_t i = 0; i < t.route.size(); ++i) {
                std::string what;
                if (i < t.rules.size()) {
                    const sim::ConsistRule& r = t.rules[i];
                    what = (r.custom ? std::string("custom") : std::string(sim::cargo_filter_name(r.filter))) + ", " +
                           std::to_string(r.capacity()) + " cars";
                    if (r.min > 0) what += ", full";
                }
                if (i < t.service_stop.size() && t.service_stop[i]) what += ", service";
                DetailRow r = line({(i == t.stop_index ? "> " : "   ") + as_written(rw.station(t.route[i]).name), what});
                if (i == t.stop_index) {
                    r.r = 1.0f;
                    r.g = 0.95f;
                    r.b = 0.7f;
                }
                rows.push_back(r);
            }
            if (t.route.empty()) rows.push_back(dim({"No stops"}));
            rows.push_back(heading("Cars"));
            std::vector<std::pair<std::string, std::int32_t>> load;
            for (const sim::Car& car : t.cars) {
                const std::string what = car.cargo ? cargo.get(*car.cargo).name : "Empty";
                auto it = std::find_if(load.begin(), load.end(), [&](const auto& p) { return p.first == what; });
                if (it == load.end()) load.emplace_back(what, 1);
                else ++it->second;
            }
            for (const auto& [what, n] : load) rows.push_back(line({what, std::to_string(n)}));
            if (t.caboose || t.diner) {
                rows.push_back(dim({std::string(t.caboose ? "Caboose" : "") + (t.caboose && t.diner ? ", " : "") +
                                    (t.diner ? "dining car" : "")}));
            }
            // The cost window: this year's earnings and running costs, and last year's.
            const sim::TrainAccounts& now = t.accounts;
            const sim::TrainAccounts& last = t.accounts_last_year;
            const std::int32_t year = world_.date().year();
            // A train bought this year has no last year.
            const bool had_last = sim::Date::from_days(t.built_day).year() < year;
            const auto pair = [&](const std::string& label, sim::Money a, sim::Money b, bool cost) {
                const auto cell = [&](sim::Money m) { return (cost && m != sim::Money{} ? "-" : "") + format_money(m); };
                DetailRow r = line({label, cell(a), had_last ? cell(b) : std::string("-")});
                if (cost) {
                    r.r = 0.95f;
                    r.g = 0.66f;
                    r.b = 0.6f;
                }
                rows.push_back(r);
            };
            rows.push_back({DetailRow::Style::Heading, {"Money", std::to_string(year) + " so far", std::to_string(year - 1)}, 0.95f, 0.8f, 0.45f});
            pair("Freight", now.freight, last.freight, false);
            pair("Passengers and mail", now.express, last.express, false);
            pair("Maintenance", now.maintenance, last.maintenance, true);
            pair("Fuel", now.fuel, last.fuel, true);
            pair("Repairs", now.repairs, last.repairs, true);
            {
                DetailRow r = line({"Profit", format_money(now.profit()), had_last ? format_money(last.profit()) : std::string("-")});
                r.r = r.g = r.b = 1.0f;
                rows.push_back(r);
            }
            rows.push_back(money_row("Earned since bought", t.revenue));
            // Its profit month by month: the last 12, then this one so far.
            if (!t.profit_months.empty()) {
                DetailRow bars{DetailRow::Style::Bars, {"Profit by month"}};
                for (const sim::Money& m : t.profit_months) bars.bars.push_back(static_cast<double>(m.whole_dollars()));
                bars.bars.push_back(static_cast<double>(t.profit_this_month.whole_dollars()));
                rows.push_back(bars);
            }
        }
    } else if (detail_->kind == DetailKind::Station) {
        const sim::Station& s = rw.station(detail_->id);
        const bool blink = (SDL_GetTicks() / 500) % 2 == 0;
        rows.push_back(title(renaming_ ? rename_text_ + (blink ? "|" : " ") : as_written(s.name)));
        if (renaming_) rows.push_back(dim({"Type a new name: Enter keeps it, Esc cancels"}));
        else if (s.owner == world_.company().id()) rows.push_back(dim({owner_name(s.owner) + "  (Enter renames)"}));
        else rows.push_back(dim({owner_name(s.owner)}));
        rows.push_back(rule_row());
        rows.push_back(line({"Size", std::string(size_name(s.size)) + ", reaches " +
                                         std::to_string(sim::catchment_radius(s.size, world_.data().balance)) + " cells"}));
        if (s.size != sim::StationSize::Large && s.owner == world_.company().id()) {
            const auto next = static_cast<sim::StationSize>(static_cast<int>(s.size) + 1);
            rows.push_back(dim({std::string("Enlarge to ") + size_name(next) + ": " +
                                format_money(world_.station_upgrade_cost(s.id)) + " (U)"}));
        }
        rows.push_back(line({"Town", s.town ? as_written(eco.towns()[*s.town].name) : std::string("open country")}));
        rows.push_back(line({"Age", years(today - s.built_day)}));
        std::size_t calling = 0;
        for (const sim::Train& t : rw.trains()) {
            calling += t.in_service() && std::find(t.route.begin(), t.route.end(), s.id) != t.route.end();
        }
        rows.push_back(line({"Trains calling", std::to_string(calling)}));
        rows.push_back(money_row("Income this month", s.income_this_month));
        if (!s.income_months.empty()) {
            rows.push_back(money_row("Income, last " + std::to_string(s.income_months.size()) + " months", s.income_last_year()));
            DetailRow bars{DetailRow::Style::Bars, {"Income by month"}};
            for (const sim::Money& m : s.income_months) bars.bars.push_back(static_cast<double>(m.whole_dollars()));
            bars.bars.push_back(static_cast<double>(s.income_this_month.whole_dollars()));
            rows.push_back(bars);
        }

        // Cargo: what waits, what the catchment makes and wants a year, and
        // the best price it fetches within reach.
        rows.push_back(heading("Cargo"));
        rows.push_back(dim({"", "Waiting", "Made", "Wanted", "Price"}));
        std::vector<std::int32_t> express(cargo.all().size(), 0);
        for (const sim::ExpressWaiting& e : s.express) express[e.cargo] += e.milli;
        bool any = false;
        for (const sim::CargoType& c : cargo.all()) {
            if (c.available_year > world_.date().year()) continue;
            const bool is_express = c.cargo_class == sim::CargoClass::Express;
            const std::int32_t waiting = is_express ? express[c.id] : (c.id < s.waiting.size() ? s.waiting[c.id].milli : 0);
            const std::int64_t made = sim::catchment_rate(eco, rw, world_.data().industries, s, c.id, true);
            const std::int64_t wanted = sim::catchment_rate(eco, rw, world_.data().industries, s, c.id, false);
            if (waiting < sim::kMilli / 10 && made == 0 && wanted == 0) continue;
            const std::string price =
                is_express ? std::string("fare") : price_k(sim::catchment_prices(eco, rw, s, c.id).best);
            DetailRow r = line({c.name, waiting > 0 ? loads(waiting) : "", made > 0 ? loads(made) : "",
                                wanted > 0 ? loads(wanted) : "", price});
            if (wanted > 0 && made == 0) {
                r.r = 0.75f; // a market here
                r.g = 0.92f;
                r.b = 0.7f;
            }
            rows.push_back(r);
            any = true;
        }
        if (!any) rows.push_back(dim({"Nothing made or wanted within reach"}));
        rows.push_back(dim({"Made and wanted: loads a year"}));
        // Travellers who gave up on the railway here (rt3-clone-spec §8.3).
        for (std::size_t c = 0; c < s.avoided_until.size(); ++c) {
            if (s.avoided_until[c] <= today) continue;
            DetailRow r = line({cargo.get(static_cast<sim::CargoId>(c)).name + " avoid the railway here",
                                "until " + sim::Date::from_days(s.avoided_until[c]).month_year_label()});
            r.r = 1.0f;
            r.g = 0.6f;
            r.b = 0.5f;
            rows.push_back(r);
            rows.push_back(dim({"Too many gave up waiting: run trains here more often"}));
        }

        // Buildings by the station: its own service buildings, and the
        // post offices, hotels, restaurants and taverns within range.
        std::vector<std::string> near;
        for (const sim::ServiceBuilding& b : rw.service_buildings()) {
            if (b.node == s.node) near.emplace_back(b.type == sim::ServiceType::ServiceTower ? "Service tower" : "Maintenance shop");
        }
        const sim::MapPoint at = rw.track().node(s.node).pos;
        for (const sim::StationBuilding& b : rw.station_buildings()) {
            if (sim::distance_mm(b.pos, at) <= world_.data().balance.stations.building_range_mm) {
                std::string n = sim::station_building_name(b.type);
                if (!n.empty()) n[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
                near.push_back(n + (b.owner != s.owner ? " (" + owner_name(b.owner) + ")" : std::string()));
            }
        }
        rows.push_back(heading("Buildings"));
        for (const std::string& n : near) rows.push_back(line({n}));
        if (near.empty()) rows.push_back(dim({"None (F3, then B, to build one)"}));
    } else {
        const sim::Site& s = eco.sites()[detail_->id];
        const sim::IndustryType& type = world_.data().industries.get(s.type);
        const sim::Balance::Industries& b = world_.data().balance.industries;
        rows.push_back(title(as_written(type.name)));
        if (!s.owner) {
            rows.push_back(dim({type.kind == sim::IndustryKind::Sink ? std::string("A consumer, not for sale")
                                                                     : "For sale at " + format_money(sim::industry_price(s, b))}));
        } else {
            rows.push_back(dim({owner_name(*s.owner)}));
        }
        rows.push_back(rule_row());
        rows.push_back(line({"Level", std::to_string(s.level)}));
        if (s.closed) rows.push_back(line({"Status", "closed"}));
        else rows.push_back(line({"Running at", std::to_string(s.utilisation_permille / 10) + "%"}));
        if (s.pace_permille != 1000) rows.push_back(line({"Pace, by local prices", std::to_string(s.pace_permille / 10) + "%"}));
        if (type.kind == sim::IndustryKind::Port || type.kind == sim::IndustryKind::Warehouse) {
            rows.push_back(line({"Mode", s.port_mode == sim::PortMode::Convert
                                             ? cargo.get(s.convert_from).name + " into " + cargo.get(s.convert_to).name
                                             : std::string(sim::port_mode_name(s.port_mode))}));
        }
        const std::int32_t cx = s.cx, cy = s.cy;
        if (!type.outputs.empty()) {
            rows.push_back(heading("Makes"));
            rows.push_back(dim({"", "This year", "Price here"}));
            for (std::size_t i = 0; i < type.outputs.size(); ++i) {
                const sim::CargoId c = type.outputs[i];
                const std::int64_t made = i < s.made_milli.size() ? s.made_milli[i] : 0;
                rows.push_back(line({cargo.get(c).name, loads(made), price_k(eco.price(c, cx, cy))}));
            }
        }
        if (!type.inputs.empty()) {
            rows.push_back(heading(type.kind == sim::IndustryKind::Raw ? "Helped by" : "Uses"));
            rows.push_back(dim({"", "Held", "Price here"}));
            for (std::size_t i = 0; i < type.inputs.size(); ++i) {
                const sim::IndustryInput& in = type.inputs[i];
                if (in.until_year && *in.until_year < world_.date().year()) continue;
                const std::int32_t held = i < s.buffer.size() ? s.buffer[i] : 0;
                rows.push_back(line({cargo.get(in.cargo).name, loads(held),
                                     price_k(eco.price(in.cargo, cx, cy))}));
            }
        }
        if (s.received_year_milli > 0) rows.push_back(line({"Received this year", loads(s.received_year_milli) + " loads"}));
        rows.push_back(heading("Money"));
        rows.push_back(money_row("Profit a year", sim::annual_profit(s)));
        if (s.owner) rows.push_back(line({"Paid for it", format_money(s.owner_paid)}));
        rows.push_back(line({"Made in all", loads(s.produced_milli) + " loads"}));
    }

    // Lay the rows out in a panel at the right of the screen.
    constexpr float kW = 400.0f, kRow = 18.0f;
    const auto w = static_cast<float>(cam_.width_px);
    const float x0 = w - kW - 12, y0 = kToolbarY + kButtonH + 10;
    float h = 16;
    const auto row_h = [&](const DetailRow& r) {
        switch (r.style) {
        case DetailRow::Style::Title: return 24.0f;
        case DetailRow::Style::Rule: return 8.0f;
        case DetailRow::Style::Heading: return kRow + 6;
        case DetailRow::Style::Bars: return kRow + 64;
        default: return kRow;
        }
    };
    for (const DetailRow& r : rows) h += row_h(r);
    const float max_h = static_cast<float>(cam_.height_px) - kBottomBarH - y0 - 10;
    ui::panel(x0, y0, x0 + kW, y0 + std::min(h, max_h));
    detail_box_ = {x0, y0, x0 + kW, y0 + std::min(h, max_h)};
    float y = y0 + 8;
    for (const DetailRow& r : rows) {
        if (y + kRow > y0 + max_h) break;
        switch (r.style) {
        case DetailRow::Style::Title:
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + 14, y, r.cells[0], kScale + 1);
            y += 24;
            break;
        case DetailRow::Style::Rule:
            ui::rule(x0 + 10, x0 + kW - 10, y + 2);
            y += 8;
            break;
        case DetailRow::Style::Heading:
            y += 6;
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + 14, y, r.cells[0], kScale);
            if (r.cells.size() == 3) {
                draw_text(x0 + 250 - static_cast<float>(text_width(r.cells[1], kScale)), y, r.cells[1], kScale);
                draw_text(x0 + kW - 14 - static_cast<float>(text_width(r.cells[2], kScale)), y, r.cells[2], kScale);
            }
            y += kRow;
            break;
        case DetailRow::Style::Bars: {
            // A bar a month, green up from the zero line, red down below it;
            // the month still running dimmed.
            if (y + row_h(r) > y0 + max_h) break;
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + 14, y, r.cells[0], kScale);
            double top = 1, bottom = 0;
            for (const double v : r.bars) top = std::max(top, v), bottom = std::min(bottom, v);
            const float by0 = y + kRow + 2, by1 = y + kRow + 58;
            const auto at = [&](double v) { return by1 - static_cast<float>((v - bottom) / (top - bottom)) * (by1 - by0); };
            const float zero = at(0), bx0 = x0 + 14, bx1 = x0 + kW - 14;
            const float slot = (bx1 - bx0) / static_cast<float>(std::max<std::size_t>(13, r.bars.size()));
            for (std::size_t i = 0; i < r.bars.size(); ++i) {
                const float sx = bx0 + slot * static_cast<float>(i);
                const float alpha = i + 1 == r.bars.size() ? 0.55f : 0.95f;
                const float yv = at(r.bars[i]);
                if (r.bars[i] >= 0) fill_rect(sx + 2, std::min(yv, zero), sx + slot - 2, zero, 0.35f, 0.85f, 0.4f, alpha);
                else fill_rect(sx + 2, zero, sx + slot - 2, std::max(yv, zero), 0.95f, 0.35f, 0.3f, alpha);
            }
            fill_rect(bx0, zero, bx1, zero + 1, 0.8f, 0.78f, 0.7f, 0.8f);
            const std::string best = format_money(sim::Money::dollars(static_cast<std::int64_t>(top)));
            glColor3f(0.68f, 0.66f, 0.62f);
            draw_text(bx1 - static_cast<float>(text_width(best, kScale)), y, best, kScale);
            y += row_h(r);
            break;
        }
        case DetailRow::Style::Gauge: {
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + 24, y, r.cells[0], kScale);
            const float gx0 = x0 + 150, gx1 = x0 + kW - 70, gy = y + 4;
            fill_rect(gx0, gy, gx1, gy + 9, 0.0f, 0.0f, 0.0f, 0.45f);
            const float f = std::clamp(r.fraction, 0.0f, 1.0f);
            const float health = r.inverted ? 1.0f - f : f;
            if (health < 0.25f) fill_rect(gx0, gy, gx0 + (gx1 - gx0) * f, gy + 9, 0.9f, 0.3f, 0.2f, 0.95f);
            else if (health < 0.5f) fill_rect(gx0, gy, gx0 + (gx1 - gx0) * f, gy + 9, 0.95f, 0.75f, 0.25f, 0.95f);
            else fill_rect(gx0, gy, gx0 + (gx1 - gx0) * f, gy + 9, 0.45f, 0.8f, 0.4f, 0.95f);
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + kW - 14 - static_cast<float>(text_width(r.cells[1], kScale)), y, r.cells[1], kScale);
            y += kRow;
            break;
        }
        case DetailRow::Style::Text: {
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + 14, y, r.cells[0], kScale);
            // Two cells: the second at the right edge; more: the table's columns.
            static const std::vector<float> kTwo = {kW - 14};
            static const std::vector<float> kThree = {250, kW - 14};
            static const std::vector<float> kFive = {200, 262, 324, kW - 14};
            const std::vector<float>& edges = r.cells.size() <= 2 ? kTwo : r.cells.size() == 3 ? kThree : kFive;
            for (std::size_t i = 1; i < r.cells.size() && i - 1 < edges.size(); ++i) {
                draw_text(x0 + edges[i - 1] - static_cast<float>(text_width(r.cells[i], kScale)), y, r.cells[i], kScale);
            }
            y += kRow;
            break;
        }
        }
    }
}

std::string Tools::inspect_text() const {
    const sim::Railway& rw = world_.railway();
    const std::int64_t snap = snap_mm();
    for (const sim::Train& t : rw.trains()) {
        if (sim::distance_mm(rw.train_position(t.id), hover_) > snap) continue;
        const auto& loco = world_.data().locomotives.get(t.loco);
        std::string s = "Train " + std::to_string(t.id + 1) + ": " + loco.name + ", " + state_name(t.state) + ". ";
        const auto service = std::count(t.service_stop.begin(), t.service_stop.end(), std::uint8_t{1});
        s += std::to_string(t.route.size()) + " stops";
        if (service > 0) s += " (" + std::to_string(service) + " service)";
        s += ", priority " + std::to_string(t.priority) + ". ";
        if (t.owner != world_.company().id()) s = world_.company(t.owner).name() + " " + s;
        // Load, grouped by cargo: "2 coal, 1 steel, 1 empty".
        std::vector<std::pair<std::string, int>> load;
        for (const sim::Car& car : t.cars) {
            std::string what = car.cargo ? world_.data().cargo.get(*car.cargo).name : "empty";
            for (char& ch : what) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            auto it = std::find_if(load.begin(), load.end(), [&](const auto& p) { return p.first == what; });
            if (it == load.end()) load.emplace_back(what, 1);
            else ++it->second;
        }
        for (std::size_t i = 0; i < load.size(); ++i) {
            s += (i ? ", " : "") + std::to_string(load[i].second) + " " + load[i].first;
        }
        const std::int32_t age = (world_.date().days_since_epoch() - t.built_day) / 365;
        s += ". Engine " + std::to_string(age) + (age == 1 ? " year" : " years") + " old. Earned " + format_money(t.revenue) +
             ", profit " + format_money(t.profit_last_year()) + " in " + std::to_string(t.profit_months.size()) + " months.";
        if (!t.rules.empty()) {
            const sim::ConsistRule& r = t.rules.front();
            std::string what = r.custom ? std::string("Custom") : std::string(sim::cargo_filter_name(r.filter));
            s += " " + what + " " + std::to_string(r.min) + "-" + std::to_string(r.capacity()) + " cars";
            if (std::any_of(t.rules.begin(), t.rules.end(), [&](const sim::ConsistRule& x) { return !(x == r); })) {
                s += " at the first stop (varies)";
            }
            if (t.holding) s += ", waiting for a full load";
            if (t.caboose) s += ", caboose";
            if (t.diner) s += ", diner";
            s += ".";
        }
        if (loco.fuel == sim::Fuel::Steam) s += " water " + percent(t.water);
        s += " sand " + percent(t.sand) + " oil " + percent(t.oil);
        return s;
    }
    if (const auto st = station_at_cursor()) {
        if (map_overlay_ == MapOverlay::Travel) return cell_text(); // where its travellers want to go
        const sim::Station& s = rw.station(*st);
        std::string text = "Station: " + s.name + " (" + size_name(s.size) + ")" +
                           (s.owner != world_.company().id() ? ", " + world_.company(s.owner).name() : std::string()) +
                           ". Waiting:";
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
        return any ? text : text + " nothing";
    }
    return cell_text();
}

void Tools::draw_ui(const std::string& status) const {
    row_boxes_.clear();
    const auto w = static_cast<float>(cam_.width_px);
    const auto h = static_cast<float>(cam_.height_px);
    if (founding_) {
        ui::bar(0, 0, w, kTopBarH, true);
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
    ui::bar(0, 0, w, kTopBarH, true);
    glColor3f(0.95f, 0.95f, 0.9f);
    draw_text(8, 4, status, kScale);
    const std::string spent = (world_.sandbox() ? std::string("Sandbox   ") : std::string()) + "Company " +
                              format_money(world_.company().cash()) + "   net worth " +
                              format_money(sim::net_worth(world_.investor(), world_.market()));
    draw_text(w - static_cast<float>(text_width(spent, kScale)) - 8, 4, spent, kScale);

    for (const Button& b : layout_buttons()) {
        const bool on = b.tool == tool_;
        ui::tab(b.x0, b.y0, b.x1, b.y1, on);
        if (on) glColor3f(0.12f, 0.08f, 0.03f);
        else glColor3f(0.92f, 0.88f, 0.8f);
        draw_text(b.x0 + 4, b.y0 + 4, b.label, kScale);
    }
    {
        const auto gb = guide_button();
        ui::tab(gb[0], gb[1], gb[2], gb[3], guide_open_);
        if (guide_open_) glColor3f(0.12f, 0.08f, 0.03f);
        else glColor3f(0.92f, 0.88f, 0.8f);
        draw_text(gb[0] + 4, gb[1] + 4, "Ctrl+G Guide", kScale);
    }
    {
        const auto hb = help_button();
        ui::tab(hb[0], hb[1], hb[2], hb[3], help_open_);
        if (help_open_) glColor3f(0.12f, 0.08f, 0.03f);
        else glColor3f(0.92f, 0.88f, 0.8f);
        draw_text(hb[0] + 4, hb[1] + 4, "? Help", kScale);
    }

    // Bottom bar: what the tool does, and the last result or what is under the cursor.
    ui::bar(0, h - kBottomBarH, w, h, false);
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
        good = line2.rfind("Cannot", 0) != 0;
    } else if (tool_ == Tool::Station && !station_building_) {
        line2 = station_reach_text();
    } else if (tool_ == Tool::Train && !stops_.empty()) {
        const auto& cargo = world_.data().cargo.all();
        line2 = "Up/Down stop  F kind  W full  [ ] cars  K custom  < > " +
                (cargo.empty() ? std::string() : as_written(cargo[cargo_choice_ % cargo.size()].name)) +
                "  S service  bksp drop  L engine";
    } else if (tool_ == Tool::Train) {
        line2 = speed_chart_text();
    } else if (tool_ == Tool::Track) {
        line2 = "E wire (Shift: all)  U double Up  Ctrl+Z undo  X twice remove";
        if (const auto cmd = pending_track()) {
            const sim::PlanResult plan = world_.preview(*cmd);
            if (plan.plan && !plan.plan->crossings.empty()) {
                const auto& c = plan.plan->crossings;
                const auto over = static_cast<std::size_t>(
                    std::count_if(c.begin(), c.end(), [](const sim::TrackPlan::Crossing& x) { return x.overpass; }));
                const std::size_t level = c.size() - over;
                line2.clear();
                if (over > 0) line2 = std::to_string(over) + (over == 1 ? " overpass" : " overpasses") + " at 4x the piece's cost";
                if (level > 0) {
                    if (!line2.empty()) line2 += ", ";
                    line2 += std::to_string(level) + (level == 1 ? " level crossing" : " level crossings") +
                             ": trains will take turns (G for more overpasses)";
                }
            }
        }
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

    tab_boxes_.clear();
    if (tool_ == Tool::Finance) draw_finance_panel(); // on top of everything on the map
    if (tool_ == Tool::Ledger) draw_ledger_panel();
    if (tool_ == Tool::Market) draw_market_panel();
    if (tool_ == Tool::Status) draw_status_panel();
    if (tool_ == Tool::Lists) draw_lists_panel();
    if (tool_ == Tool::Train) draw_train_panel();
    if (tool_ == Tool::Track || tool_ == Tool::Station) draw_tool_options();
    if (tool_ == Tool::Industry) draw_industry_options();
    if (tool_ == Tool::Inspect && detail_) draw_detail_panel();
    if (log_open_) draw_log_panel();
    // Money rising from paid trains.
    for (const Floater& f : floaters_) {
        float sx = 0, sy = 0;
        if (!cam_.to_screen(f.at, sx, sy)) continue;
        const float age = static_cast<float>(SDL_GetTicks() - f.born) / 2600.0f;
        sy -= 40 + 50 * age;
        const float fade = age < 0.7f ? 1.0f : 1.0f - (age - 0.7f) / 0.3f;
        const float tw = static_cast<float>(text_width(f.text, kScale + 1));
        glEnable(GL_BLEND);
        // A dark outline, so it reads on snow and grass alike.
        glColor4f(0.05f, 0.1f, 0.03f, 0.85f * fade);
        for (const auto& [ox, oy] : {std::pair{-1.5f, 0.0f}, std::pair{1.5f, 0.0f}, std::pair{0.0f, -1.5f}, std::pair{0.0f, 1.5f}}) {
            draw_text(sx - tw / 2 + ox, sy + oy, f.text, kScale + 1);
        }
        glColor4f(0.5f, 1.0f, 0.4f, fade);
        draw_text(sx - tw / 2, sy, f.text, kScale + 1);
    }
    // Your trains in trouble: a "!" badge floating over each, pulsing red when
    // stuck or crawling, amber when slowed.
    if (const auto me = world_.player_company(); me && !menu_open_) {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(SDL_GetTicks()) / 220.0f);
        for (const sim::Train& t : world_.railway().trains()) {
            if (t.owner != *me) continue;
            const int trouble = train_trouble(t);
            if (trouble == 0) continue;
            float sx = 0, sy = 0;
            if (!cam_.to_screen(world_.railway().train_position(t.id), sx, sy)) continue;
            sy -= 30;
            if (sy < kToolbarY + kButtonH || sy > h - kBottomBarH) continue;
            const float r = 10.0f;
            const float cr = 1.0f, cg = trouble == 2 ? 0.22f + 0.2f * pulse : 0.62f, cb = trouble == 2 ? 0.15f : 0.1f;
            fill_rect(sx - 1, sy + r - 2, sx + 1, sy + 28, cr, cg, cb, 0.8f); // a stalk down to the train
            fill_rect(sx - r - 2, sy - r - 2, sx + r + 2, sy + r + 2, 0.1f, 0.06f, 0.04f, 0.85f);
            fill_rect(sx - r, sy - r, sx + r, sy + r, cr * (trouble == 2 ? pulse : 1.0f), cg, cb, 1.0f);
            glColor3f(1.0f, 1.0f, 1.0f);
            draw_text(sx - static_cast<float>(text_width("!", kScale)) / 2, sy - 9, "!", kScale);
        }
    }
    if (find_open_) draw_find_panel();
    draw_hover_card();
    // A tooltip under the toolbar button the mouse is on.
    for (const Button& b : layout_buttons()) {
        if (hover_sx_ < b.x0 || hover_sx_ >= b.x1 || hover_sy_ < b.y0 || hover_sy_ >= b.y1 || menu_open_ || help_open_) continue;
        const std::string tip = tool_tip(b.tool);
        const float tw = static_cast<float>(text_width(tip, kScale)) + 16;
        const float tx = std::min(b.x0, static_cast<float>(cam_.width_px) - tw - 6), ty = b.y1 + 6;
        ui::chip(tx, ty, tx + tw, ty + 24);
        glColor3f(0.95f, 0.93f, 0.86f);
        draw_text(tx + 8, ty + 4, tip, kScale);
    }
    if (guide_open_) draw_guide();
    if (help_open_) draw_help();
    if (console_open_) draw_console();
    draw_choice_dialog();
    if (paper_open_) draw_newspaper();
    if (menu_open_) draw_menu();

    // Price or problem next to the cursor while laying track.
    if (const auto cmd = pending_track()) {
        const sim::PlanResult plan = world_.preview(*cmd);
        const std::string label = plan.plan ? format_money(plan.plan->total_cost) : plan.error;
        if (plan.plan) glColor3f(1.0f, 1.0f, 0.6f);
        else glColor3f(1.0f, 0.45f, 0.4f);
        draw_text(hover_sx_ + 14, hover_sy_ + 10, label, kScale);
    }
    // Over track in the Station tool: the name the station would take and
    // its price, or why it can't go there.
    if (tool_ == Tool::Station && !station_building_ && hover_pick_.kind != sim::TrackEnd::Kind::Free &&
        world_.player_company() && !menu_open_ && !help_open_ && !guide_open_ && hover_sy_ > kToolbarY + kButtonH &&
        hover_sy_ < static_cast<float>(cam_.height_px) - kBottomBarH) {
        const sim::BuildStation cmd{.at = hover_pick_, .size = station_size_};
        const auto why = world_.station_problem(cmd);
        const std::string label = why ? "Cannot: " + *why
                                      : world_.station_name_for(hover_pick_.pos) + "  " + size_name(station_size_) + " " +
                                            format_money(world_.station_build_cost(cmd));
        const float tw = static_cast<float>(text_width(label, kScale)) + 16;
        const float tx = hover_sx_ + 16, ty = hover_sy_ + 8;
        ui::chip(tx, ty, tx + tw, ty + 24);
        if (why) glColor3f(1.0f, 0.5f, 0.45f);
        else glColor3f(0.95f, 0.93f, 0.8f);
        draw_text(tx + 8, ty + 4, label, kScale);
    }
}

std::array<float, 4> Tools::guide_button() const {
    const auto hb = help_button();
    const float x = hb[2] + 3.0f;
    return {x, kToolbarY, x + static_cast<float>(text_width("Ctrl+G Guide", kScale)) + 8.0f, kToolbarY + kButtonH};
}

std::string Tools::site_name(sim::SiteId id) const {
    const sim::Economy& eco = world_.economy();
    const sim::Site& s = eco.sites()[id];
    // Its nearest town.
    const sim::Town* near = nullptr;
    std::int64_t best = 0;
    for (const sim::Town& t : eco.towns()) {
        const std::int64_t d = std::int64_t{t.cx - s.cx} * (t.cx - s.cx) + std::int64_t{t.cy - s.cy} * (t.cy - s.cy);
        if (!near || d < best) near = &t, best = d;
    }
    const std::string town = near ? as_written(near->name) : std::string("the wilds");
    if (s.kind == sim::IndustryKind::House) return town;
    return as_written(world_.data().industries.get(s.type).name) + " near " + town;
}

std::vector<Tools::Opportunity> Tools::find_opportunities() const {
    const sim::Economy& eco = world_.economy();
    const sim::IndustryRegistry& types = world_.data().industries;
    const sim::Railway& rw = world_.railway();
    const std::int32_t year = world_.date().year();
    const auto me = world_.player_company();
    // Whether one of your stations reaches a site.
    const auto served = [&](const sim::Site& site) {
        for (const sim::Station& st : rw.stations()) {
            if (!me || st.owner != *me) continue;
            const sim::MapPoint at = rw.track().node(st.node).pos;
            if (eco.reaches(site, at, eco.cells_mm(sim::catchment_radius(st.size, world_.data().balance)))) return true;
        }
        return false;
    };
    const auto wants = [&](const sim::Site& d, sim::CargoId c) {
        if (d.closed || d.kind == sim::IndustryKind::Raw) return false;
        for (const sim::IndustryInput& in : types.get(d.type).inputs) {
            if (in.cargo == c && (!in.until_year || year <= *in.until_year)) return true;
        }
        return false;
    };
    const sim::MapPoint one = eco.node_centre(0, 0), next = eco.node_centre(1, 0);
    const double cell_miles = static_cast<double>(sim::distance_mm(one, next)) / 1'609'344.0;
    std::vector<Opportunity> out;
    for (const sim::Site& from : eco.sites()) {
        if (from.closed || from.kind == sim::IndustryKind::House || from.kind == sim::IndustryKind::Sink) continue;
        const sim::IndustryType& t = types.get(from.type);
        for (const sim::CargoId c : t.outputs) {
            const sim::CargoType& cargo = world_.data().cargo.get(c);
            if (cargo.cargo_class != sim::CargoClass::Freight || cargo.available_year > year) continue;
            // Mines, farms and camps always produce; works and ports only as
            // far as they are running (fed, or trading).
            std::int64_t made = t.rate_milli * std::max(1, from.level);
            if (from.kind != sim::IndustryKind::Raw) made = made * from.utilisation_permille / 1000;
            if (made <= 0) continue;
            std::optional<Opportunity> best;
            for (const sim::Site& to : eco.sites()) {
                if (to.id == from.id || !wants(to, c)) continue;
                const double dx = to.cx - from.cx, dy = to.cy - from.cy;
                const double miles = std::sqrt(dx * dx + dy * dy) * cell_miles;
                if (miles < 3.0 || miles > 35.0) continue;
                const std::int64_t gain = eco.price(c, to.cx, to.cy) - eco.price(c, from.cx, from.cy);
                if (gain <= 0) continue;
                if (!best || gain * made > best->per_load * best->loads_milli) {
                    best = Opportunity{c, from.id, to.id, gain, made, static_cast<std::int64_t>(miles * 10), false};
                }
            }
            if (best) {
                best->served = served(from) && served(eco.sites()[best->to]);
                out.push_back(*best);
            }
        }
    }
    // Passengers and mail between towns: what both towns' houses make a
    // year, at the fare for the distance (a fair floor: a fast line between
    // towns that also feed others does better).
    {
        const auto& towns = eco.towns();
        const std::int32_t reach = sim::catchment_radius(sim::StationSize::Medium, world_.data().balance);
        std::vector<std::int64_t> houses(towns.size(), 0);
        std::int64_t house_rate = 0;
        for (const sim::IndustryType& t : types.all())
            if (t.kind == sim::IndustryKind::House) house_rate = t.rate_milli;
        for (const sim::Site& s : eco.sites()) {
            if (s.kind != sim::IndustryKind::House) continue;
            for (std::size_t i = 0; i < towns.size(); ++i) {
                if (std::abs(s.cx - towns[i].cx) <= reach && std::abs(s.cy - towns[i].cy) <= reach) houses[i] += s.level;
            }
        }
        const std::int32_t era = sim::express_era_percent(year, world_.data().balance);
        const auto served_town = [&](const sim::Town& t) {
            sim::Site probe;
            probe.cx = t.cx;
            probe.cy = t.cy;
            return served(probe);
        };
        for (std::size_t i = 0; i < towns.size(); ++i) {
            for (std::size_t j = i + 1; j < towns.size(); ++j) {
                const double dx = towns[j].cx - towns[i].cx, dy = towns[j].cy - towns[i].cy;
                const double miles = std::sqrt(dx * dx + dy * dy) * cell_miles;
                if (miles < 5.0 || miles > 30.0) continue;
                const std::int64_t made = (houses[i] + houses[j]) * house_rate * era / 100; // milli-loads, each of passengers and mail
                if (made <= 0) continue;
                const std::int64_t length = sim::distance_mm(eco.node_centre(towns[i].cx, towns[i].cy), eco.node_centre(towns[j].cx, towns[j].cy));
                std::int64_t fares = 0; // a load of each, dollars
                std::int32_t kinds = 0;
                for (const sim::CargoType& c : world_.data().cargo.all()) {
                    if (c.cargo_class != sim::CargoClass::Express || c.available_year > year || c.key == "troops") continue;
                    fares += sim::express_base_fare(c, length, world_.data().balance).whole_dollars();
                    ++kinds;
                }
                if (kinds == 0) continue;
                // Bigger first: from the larger town.
                const bool swap = houses[j] > houses[i];
                Opportunity o{*world_.data().cargo.find("passengers"), static_cast<sim::SiteId>(swap ? j : i), static_cast<sim::SiteId>(swap ? i : j), fares / kinds,
                              made * kinds, static_cast<std::int64_t>(miles * 10), served_town(towns[i]) && served_town(towns[j]), true};
                out.push_back(o);
            }
        }
    }
    // Unserved first, then by what a year of it would bring.
    std::sort(out.begin(), out.end(), [](const Opportunity& a, const Opportunity& b) {
        if (a.served != b.served) return !a.served;
        const std::int64_t va = a.per_load * a.loads_milli / 1000 / 1000, vb = b.per_load * b.loads_milli / 1000 / 1000;
        if (va != vb) return va > vb; // to the thousand dollars
        return a.tenths_of_miles < b.tenths_of_miles; // then the shorter line, cheaper to build
    });
    if (out.size() > 40) out.resize(40);
    return out;
}

void Tools::open_guide() {
    guide_open_ = true;
    guide_sel_ = 0;
    guide_opps_ = find_opportunities();
}

bool Tools::guide_key(SDL_Keycode key) {
    const std::size_t n = guide_tab_ == 0 ? guide_opps_.size() : world_.data().cargo.all().size();
    switch (key) {
    case SDLK_ESCAPE:
    case SDLK_g: guide_open_ = false; return true;
    case SDLK_TAB:
        guide_tab_ = 1 - guide_tab_;
        guide_sel_ = 0;
        return true;
    case SDLK_UP: guide_sel_ = guide_sel_ > 0 ? guide_sel_ - 1 : 0; return true;
    case SDLK_DOWN: guide_sel_ = n == 0 ? 0 : std::min(n - 1, guide_sel_ + 1); return true;
    case SDLK_PAGEUP: guide_sel_ = guide_sel_ > 10 ? guide_sel_ - 10 : 0; return true;
    case SDLK_PAGEDOWN: guide_sel_ = n == 0 ? 0 : std::min(n - 1, guide_sel_ + 10); return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (guide_tab_ == 0 && guide_sel_ < guide_opps_.size()) {
            // Go to where the cargo is made, its panel open, and say where it sells.
            const Opportunity& o = guide_opps_[guide_sel_];
            const auto [cx, cy] = opp_cell(o, true);
            jump_ = world_.economy().node_centre(cx, cy);
            hover_ = *jump_;
            select(Tool::Inspect);
            if (o.towns) {
                detail_.reset();
            } else {
                open_detail(DetailKind::Industry, o.from);
            }
            link_ = o;
            link_armed_until_ = 0;
            const bool town = o.towns || world_.economy().sites()[o.to].kind == sim::IndustryKind::House;
            show((o.towns ? "Passengers and mail from " + opp_from_name(o) + " to "
                          : as_written(world_.data().cargo.get(o.cargo).name) + " from here sells " + (town ? "in " : "to the ")) +
                     opp_to_name(o) + ", " +
                     std::to_string(o.tenths_of_miles / 10) + " miles away, for " + price_k(o.per_load) +
                     " more a load.  B builds the line for you",
                 true);
            guide_open_ = false;
        }
        return true;
    default: return true;
    }
}

void Tools::draw_guide() const {
    constexpr float kRow = 19.0f;
    constexpr std::size_t kShown = 24;
    const float w = std::min(1180.0f, static_cast<float>(cam_.width_px) - 20), x0 = (static_cast<float>(cam_.width_px) - w) / 2;
    const float y0 = 56, h = 120 + kRow * kShown;
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 16, y0 + 10, "Guide", kScale + 1);
    // The two tabs.
    const char* tabs[] = {"Opportunities", "Who wants what"};
    float tx = x0 + 120;
    for (int i = 0; i < 2; ++i) {
        const float tw = static_cast<float>(text_width(tabs[i], kScale)) + 20;
        if (i == guide_tab_) ui::selection(tx, y0 + 10, tx + tw - 6, y0 + 34);
        if (i == guide_tab_) glColor3f(1.0f, 0.95f, 0.7f);
        else glColor3f(0.65f, 0.63f, 0.58f);
        draw_text(tx + 7, y0 + 13, tabs[i], kScale);
        tx += tw + 4;
    }
    ui::rule(x0 + 10, x0 + w - 10, y0 + 40);
    float y = y0 + 48;
    const auto& cargo = world_.data().cargo;
    const std::size_t first = guide_sel_ >= kShown ? guide_sel_ + 1 - kShown : 0;
    const auto right = [&](float x, float yy, const std::string& t) {
        draw_text(x - static_cast<float>(text_width(t, kScale)), yy, t, kScale);
    };
    if (guide_tab_ == 0) {
        glColor3f(0.75f, 0.73f, 0.68f);
        draw_text(x0 + 16, y,
                  "Lines worth building: passengers and mail between towns, and freight to where it sells for more. Estimates; served ones last.",
                  kScale);
        y += kRow + 4;
        const float c_from = x0 + 165, c_to = x0 + 500, c_miles = x0 + 860, c_load = x0 + 960, c_loads = x0 + 1060,
                    c_year = x0 + w - 16;
        glColor3f(0.68f, 0.66f, 0.62f);
        draw_text(x0 + 16, y, "Cargo", kScale);
        draw_text(c_from, y, "From", kScale);
        draw_text(c_to, y, "To", kScale);
        right(c_miles, y, "Miles");
        right(c_load, y, "A load");
        right(c_loads, y, "Loads/yr");
        right(c_year, y, "A year");
        y += kRow;
        if (guide_opps_.empty()) {
            glColor3f(0.68f, 0.66f, 0.62f);
            draw_text(x0 + 16, y, "Nothing found within 35 miles yet: look again as industries open and prices move", kScale);
        }
        for (std::size_t i = first; i < guide_opps_.size() && i < first + kShown; ++i) {
            const Opportunity& o = guide_opps_[i];
            add_row(x0 + 8, y - 2, x0 + w - 8, y + kRow - 3, RowPanel::Guide, i);
            const bool sel = i == guide_sel_;
            if (sel) ui::selection(x0 + 8, y - 2, x0 + w - 8, y + kRow - 3);
            if (sel) glColor3f(1.0f, 0.95f, 0.7f);
            else if (o.served) glColor3f(0.6f, 0.58f, 0.55f);
            else glColor3f(0.9f, 0.88f, 0.82f);
            draw_text(x0 + 16, y, o.towns ? std::string("Passengers, mail") : as_written(cargo.get(o.cargo).name), kScale);
            draw_text(c_from, y, opp_from_name(o), kScale);
            draw_text(c_to, y, opp_to_name(o) + (o.served ? "  (served)" : ""), kScale);
            right(c_miles, y, std::to_string(o.tenths_of_miles / 10));
            right(c_load, y, "+" + price_k(o.per_load));
            right(c_loads, y, loads(o.loads_milli));
            right(c_year, y, price_k(o.per_load * o.loads_milli / 1000));
            y += kRow;
        }
    } else {
        glColor3f(0.75f, 0.73f, 0.68f);
        draw_text(x0 + 16, y, "Each cargo: who makes it and who pays for it. Towns want passengers and mail, and goods as they grow.",
                  kScale);
        y += kRow + 4;
        const float c_made = x0 + 190, c_wanted = x0 + 610;
        glColor3f(0.68f, 0.66f, 0.62f);
        draw_text(x0 + 16, y, "Cargo", kScale);
        draw_text(c_made, y, "Made by", kScale);
        draw_text(c_wanted, y, "Wanted by", kScale);
        y += kRow;
        const auto& types = world_.data().industries.all();
        const std::int32_t year = world_.date().year();
        const auto names = [&](sim::CargoId c, bool made) {
            std::string out;
            bool towns = false;
            for (const sim::IndustryType& t : types) {
                bool has = false;
                if (made) {
                    has = std::find(t.outputs.begin(), t.outputs.end(), c) != t.outputs.end();
                } else if (t.kind != sim::IndustryKind::Raw) {
                    for (const sim::IndustryInput& in : t.inputs) has = has || (in.cargo == c && (!in.until_year || year <= *in.until_year));
                }
                if (!has) continue;
                if (t.kind == sim::IndustryKind::House) {
                    towns = true;
                    continue;
                }
                out += (out.empty() ? "" : ", ") + as_written(t.name);
            }
            if (towns) out = std::string("towns") + (out.empty() ? "" : ", ") + out;
            return out.empty() ? std::string("-") : out;
        };
        const auto clip = [&](std::string t, float width) {
            bool cut = false;
            while (!t.empty() && static_cast<float>(text_width(t + (cut ? "..." : ""), kScale)) > width) {
                t.pop_back();
                cut = true;
            }
            return cut ? t + "..." : t;
        };
        for (std::size_t i = first; i < cargo.all().size() && i < first + kShown; ++i) {
            const sim::CargoType& c = cargo.all()[i];
            add_row(x0 + 8, y - 2, x0 + w - 8, y + kRow - 3, RowPanel::Guide, i);
            const bool sel = i == guide_sel_;
            if (sel) ui::selection(x0 + 8, y - 2, x0 + w - 8, y + kRow - 3);
            const bool later = c.available_year > year;
            if (sel) glColor3f(1.0f, 0.95f, 0.7f);
            else if (later) glColor3f(0.55f, 0.53f, 0.5f);
            else glColor3f(0.9f, 0.88f, 0.82f);
            draw_text(x0 + 16, y, as_written(c.name) + (later ? " (" + std::to_string(c.available_year) + ")" : ""), kScale);
            draw_text(c_made, y, clip(names(c.id, true), c_wanted - c_made - 16), kScale);
            draw_text(c_wanted, y, clip(names(c.id, false), x0 + w - 16 - c_wanted), kScale);
            y += kRow;
        }
    }
    glColor3f(0.6f, 0.58f, 0.54f);
    draw_text(x0 + 16, y0 + h - 26,
              guide_tab_ == 0 ? "Tab other page   Up/Down, wheel or click choose   Enter or click again: go there   Esc close"
                              : "Tab other page   Up/Down or wheel scroll   Esc close",
              kScale);
}


namespace {

// Where a planned line ends near a site: one cell short of it, towards the
// other end, so the station sits beside the works rather than on it.
sim::MapPoint beside(const sim::Economy& eco, const sim::Site& s, const sim::Site& other) {
    const int dx = other.cx > s.cx ? 1 : other.cx < s.cx ? -1 : 0;
    const int dy = other.cy > s.cy ? 1 : other.cy < s.cy ? -1 : 0;
    return eco.node_centre(s.cx + dx, s.cy + dy);
}

} // namespace

std::string Tools::opp_from_name(const Opportunity& o) const {
    return o.towns ? as_written(world_.economy().towns()[o.from].name) : site_name(o.from);
}
std::string Tools::opp_to_name(const Opportunity& o) const {
    return o.towns ? as_written(world_.economy().towns()[o.to].name) : site_name(o.to);
}
std::pair<std::int32_t, std::int32_t> Tools::opp_cell(const Opportunity& o, bool from) const {
    const sim::Economy& eco = world_.economy();
    const std::size_t i = from ? o.from : o.to;
    if (o.towns) return {eco.towns()[i].cx, eco.towns()[i].cy};
    return {eco.sites()[i].cx, eco.sites()[i].cy};
}
sim::MapPoint Tools::opp_station_point(const Opportunity& o, bool from) const {
    const sim::Economy& eco = world_.economy();
    if (o.towns) {
        const auto [cx, cy] = opp_cell(o, from);
        return eco.node_centre(cx, cy); // in the middle of town
    }
    return beside(eco, eco.sites()[from ? o.from : o.to], eco.sites()[from ? o.to : o.from]);
}

Tools::LinkPlan Tools::plan_link(const Opportunity& o) const {
    LinkPlan out;
    const sim::Economy& eco = world_.economy();
    const sim::Railway& rw = world_.railway();
    const auto me = world_.player_company();
    if (!me) {
        out.error = "you run no company";
        return out;
    }
    // Each end: one of your stations already near it, else track there to join, else open ground.
    std::int32_t new_stations = 0;
    const auto end_for = [&](bool from) -> sim::TrackEnd {
        const auto [cx, cy] = opp_cell(o, from);
        for (const sim::Station& st : rw.stations()) {
            if (st.owner != *me) continue;
            const sim::MapPoint at = rw.track().node(st.node).pos;
            if (std::abs(eco.cell_x(at) - cx) <= 2 && std::abs(eco.cell_y(at) - cy) <= 2) {
                return {sim::TrackEnd::Kind::Node, st.node, 0, at};
            }
        }
        ++new_stations;
        return sim::pick_track_end(rw.track(), opp_station_point(o, from), 300'000);
    };
    out.track.start = end_for(true);
    out.track.end = end_for(false);
    const sim::PlanResult plan = world_.preview(out.track);
    if (!plan.plan) {
        out.error = plan.error.empty() ? "no way through for track" : plan.error;
        return out;
    }
    out.track_cost = plan.plan->total_cost;
    out.stations_cost = world_.construction_cost(sim::station_cost(sim::StationSize::Medium, world_.data().balance)) *
                        new_stations;
    // Water and sand: a service tower at the first station, unless one of
    // yours already stands near either end (without one, steam engines crawl).
    out.tower = true;
    for (const sim::ServiceBuilding& sb : rw.service_buildings()) {
        if (sb.owner != *me || sb.type != sim::ServiceType::ServiceTower) continue;
        const sim::MapPoint at = rw.track().node(sb.node).pos;
        for (const sim::MapPoint end : {out.track.start.pos, out.track.end.pos}) {
            if (sim::distance_mm(at, end) <= eco.cells_mm(3)) out.tower = false;
        }
    }
    if (out.tower) {
        out.tower_cost = world_.construction_cost(sim::service_building_cost(sim::ServiceType::ServiceTower, world_.data().balance));
    }
    // The fastest engine the money left will buy.
    const sim::Money left = world_.company().cash() - out.track_cost - out.stations_cost - out.tower_cost;
    std::int32_t best_speed = -1;
    for (const sim::LocomotiveType& l : world_.data().locomotives.all()) {
        if (!l.available_in(world_.date().year()) || l.fuel == sim::Fuel::Electric || l.cost > left) continue;
        if (l.top_speed_mph > best_speed) best_speed = l.top_speed_mph, out.loco = l.id, out.train_cost = l.cost;
    }
    if (!out.loco) out.error = "not enough money left for an engine";
    return out;
}

void Tools::build_link() {
    const LinkPlan plan = plan_link(*link_);
    if (!plan.error.empty()) {
        show("Cannot build the line: " + plan.error, false);
        return;
    }
    if (SDL_GetTicks() > link_armed_until_) {
        link_armed_until_ = SDL_GetTicks() + 6000;
        show("Build it all for " + format_money(plan.total()) + "? Track " + format_money(plan.track_cost) + ", stations " +
                 format_money(plan.stations_cost) + (plan.tower ? ", service tower " + format_money(plan.tower_cost) : "") +
                 ", train " + format_money(plan.train_cost) + ".  B again to build",
             true);
        return;
    }
    link_armed_until_ = 0;
    const sim::CommandResult track = world_.execute(plan.track);
    if (!track.ok) {
        show("Cannot build the track: " + track.error, false);
        return;
    }
    world_.execute(sim::EndTrackUndo{});
    const sim::TrackNetwork& net = world_.railway().track();
    const auto station_at = [&](const sim::TrackEnd& end, sim::NodeId fallback) -> std::optional<sim::StationId> {
        const sim::NodeId node = end.kind == sim::TrackEnd::Kind::Node ? end.node : fallback;
        for (const sim::Station& st : world_.railway().stations())
            if (st.node == node) return st.id;
        const sim::CommandResult r = world_.execute(
            sim::BuildStation{.at = {sim::TrackEnd::Kind::Node, node, 0, net.node(node).pos}, .size = sim::StationSize::Medium});
        if (!r.ok) {
            show("Cannot build a station: " + r.error, false);
            return std::nullopt;
        }
        return r.created_id;
    };
    const sim::NodeId start_node =
        plan.track.start.kind == sim::TrackEnd::Kind::Node ? plan.track.start.node : *net.nearest_node(plan.track.start.pos, 1);
    const auto sa = station_at(plan.track.start, start_node);
    const auto sb = station_at(plan.track.end, track.created_id);
    if (!sa || !sb) {
        world_.execute(sim::EndBuildUndo{});
        return;
    }
    if (plan.tower) {
        const sim::NodeId node = world_.railway().station(*sa).node;
        const sim::CommandResult t = world_.execute(sim::BuildServiceBuilding{
            .at = {sim::TrackEnd::Kind::Node, node, 0, net.node(node).pos}, .type = sim::ServiceType::ServiceTower});
        if (!t.ok) show("Line built, but no service tower: " + t.error, false);
    }
    world_.execute(sim::EndBuildUndo{}); // the Guide's line is not undone piece by piece
    const sim::CommandResult train = world_.execute(sim::BuyTrain{.loco = *plan.loco, .cars = 4, .route = {*sa, *sb}});
    if (!train.ok) {
        show("Line built, but cannot buy the train: " + train.error, false);
        return;
    }
    show("Line built: " + (link_->towns ? std::string("passengers and mail") : as_written(world_.data().cargo.get(link_->cargo).name)) +
             " from " + opp_from_name(*link_) + " to " + opp_to_name(*link_) + ", with Train " + std::to_string(train.created_id + 1) +
             ". Start the clock (Space) to run it",
         true);
    link_.reset();
}

void Tools::draw_link() const {
    const sim::Economy& eco = world_.economy();
    const auto [ax, ay] = opp_cell(*link_, true);
    const auto [bx, by] = opp_cell(*link_, false);
    const sim::MapPoint pa = eco.node_centre(ax, ay), pb = eco.node_centre(bx, by);
    // Rings at both ends, and gold dots along the way, gently pulsing.
    const float pulse = 0.75f + 0.25f * std::sin(static_cast<float>(SDL_GetTicks()) / 300.0f);
    draw_marker(pa, cam_, 16, 1.0f, 0.85f * pulse, 0.3f);
    draw_marker(pb, cam_, 16, 1.0f, 0.85f * pulse, 0.3f);
    const std::int64_t length = sim::distance_mm(pa, pb);
    const std::int64_t step = 400'000;
    for (std::int64_t d = step; d < length; d += step) {
        const sim::MapPoint p{pa.x_mm + (pb.x_mm - pa.x_mm) * d / length, pa.y_mm + (pb.y_mm - pa.y_mm) * d / length};
        draw_marker(p, cam_, 4, 1.0f, 0.8f, 0.25f);
    }
}

std::string Tools::station_reach_text() const {
    const sim::Economy& eco = world_.economy();
    const auto& types = world_.data().industries;
    const auto& cargo = world_.data().cargo;
    const sim::MapPoint p = hover_pick_.kind == sim::TrackEnd::Kind::Free ? hover_ : hover_pick_.pos;
    const std::int64_t r_mm = eco.cells_mm(sim::catchment_radius(station_size_, world_.data().balance));
    const std::int32_t year = world_.date().year();
    std::vector<std::int64_t> made(cargo.all().size(), 0), wanted(cargo.all().size(), 0);
    for (const sim::Site& site : eco.sites()) {
        if (site.closed || !eco.reaches(site, p, r_mm)) continue; // industries anywhere on their footprint
        const sim::IndustryType& t = types.get(site.type);
        const std::int64_t rate = t.rate_milli * std::max(1, site.level);
        for (const sim::CargoId c : t.outputs) made[c] += rate;
        if (t.kind == sim::IndustryKind::Raw) continue; // a raw site's inputs only help it
        for (const sim::IndustryInput& in : t.inputs)
            if (!in.until_year || year <= *in.until_year) wanted[in.cargo] += rate;
    }
    const auto top = [&](const std::vector<std::int64_t>& v, bool amounts, std::size_t n) {
        std::vector<sim::CargoId> ids;
        for (const sim::CargoType& c : cargo.all())
            if (v[c.id] > 0 && c.available_year <= year && c.key != "waste") ids.push_back(c.id);
        std::sort(ids.begin(), ids.end(), [&](sim::CargoId a, sim::CargoId b) { return v[a] > v[b]; });
        std::string out;
        for (std::size_t i = 0; i < ids.size() && i < n; ++i) {
            out += (i ? ", " : "") + as_written(cargo.get(ids[i]).name) + (amounts ? " " + loads(v[ids[i]]) : "");
        }
        if (ids.size() > n) out += " +" + std::to_string(ids.size() - n) + " more";
        return out.empty() ? std::string("nothing") : out;
    };
    return "In reach (loads a year): makes " + top(made, true, 5) + ".  Wants " + top(wanted, false, 5);
}

namespace {
// What the Train panel's rows do, as their indexes: engines from 0, stops
// from kStopRow, and the buttons.
constexpr std::size_t kStopRow = 1000;
enum TrainButton : std::size_t { kFewer = 2000, kMore, kKind, kFull, kService, kDrop, kBuy, kClear };
} // namespace

void Tools::train_panel_click(std::size_t index) {
    if (index < kStopRow) {
        loco_choice_ = index;
        return;
    }
    if (index < kFewer) {
        stop_sel_ = std::min(index - kStopRow, stops_.empty() ? 0 : stops_.size() - 1);
        return;
    }
    // The buttons do what their keys do.
    switch (index) {
    case kFewer: on_key(SDLK_LEFTBRACKET, 0); break;
    case kMore: on_key(SDLK_RIGHTBRACKET, 0); break;
    case kKind: on_key(SDLK_f, 0); break;
    case kFull: on_key(SDLK_w, 0); break;
    case kService: on_key(SDLK_s, 0); break;
    case kDrop: on_key(SDLK_BACKSPACE, 0); break;
    case kBuy: on_key(SDLK_RETURN, 0); break;
    case kClear: cancel(); break;
    default: break;
    }
}

void Tools::draw_train_panel() const {
    if (founding_ || menu_open_ || help_open_ || guide_open_) {
        side_panel_box_ = {};
        return;
    }
    const auto locos = available_locos();
    const auto& data = world_.data();
    // Top left: the radar is below it and detail panels open on the right.
    const float w = 360, x0 = 10, y0 = kToolbarY + kButtonH + 10;
    constexpr float kRowH = 22;
    // Engines: up to eight about the one chosen.
    const std::size_t chosen = locos.empty() ? 0 : loco_choice_ % locos.size();
    const std::size_t shown = std::min<std::size_t>(8, locos.size());
    const std::size_t first = locos.size() <= shown ? 0 : std::min(chosen > 3 ? chosen - 3 : 0, locos.size() - shown);
    const float h = 40 + kRowH * static_cast<float>(shown) + 30 + kRowH * static_cast<float>(std::max<std::size_t>(1, stops_.size())) + 30 +
                    2 * (kRowH + 6) + 40 + 22; // and room for a hint under Buy
    side_panel_box_ = {x0, y0, x0 + w, y0 + h};
    ui::panel(x0, y0, x0 + w, y0 + h);
    float y = y0 + 10;
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y, editing_ ? "Train " + std::to_string(*editing_ + 1) + ": a new route" : std::string("A new train"), kScale + 1);
    y += 30;
    // The engines.
    for (std::size_t i = first; i < first + shown; ++i) {
        const sim::LocomotiveType& l = data.locomotives.get(locos[i]);
        if (i == chosen) ui::selection(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4);
        glColor3f(i == chosen ? 1.0f : 0.85f, i == chosen ? 0.95f : 0.85f, i == chosen ? 0.8f : 0.8f);
        draw_text(x0 + 14, y, l.name, kScale);
        const char* fuel = l.fuel == sim::Fuel::Steam ? "steam" : l.fuel == sim::Fuel::Diesel ? "diesel" : "electric";
        const std::string right = std::string(fuel) + "  " + std::to_string(l.top_speed_mph) + " mph  " + format_money(l.cost);
        draw_text(x0 + w - 14 - static_cast<float>(text_width(right, kScale)), y, right, kScale);
        add_row(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4, RowPanel::TrainTool, i);
        y += kRowH;
    }
    if (locos.empty()) {
        glColor3f(1.0f, 0.5f, 0.45f);
        draw_text(x0 + 14, y, "No engines available this year", kScale);
        y += kRowH;
    }
    ui::rule(x0 + 10, x0 + w - 10, y + 4);
    y += 12;
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y, "Route", kScale);
    y += kRowH;
    // The stops, each with what the train takes on there.
    if (stops_.empty()) {
        glColor3f(0.7f, 0.68f, 0.62f);
        draw_text(x0 + 14, y, "Click stations on the map, in order", kScale);
        y += kRowH;
    }
    for (std::size_t i = 0; i < stops_.size(); ++i) {
        const PendingStop& p = stops_[i];
        if (i == stop_sel_) ui::selection(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4);
        glColor3f(0.92f, 0.9f, 0.84f);
        draw_text(x0 + 14, y, std::to_string(i + 1) + ". " + world_.railway().station(p.station).name, kScale);
        std::string what = p.rule.custom ? std::to_string(p.rule.cars.size()) + " custom"
                                         : std::string(sim::cargo_filter_name(p.rule.filter)) + " " + std::to_string(p.rule.min) + "-" +
                                               std::to_string(p.rule.max);
        if (p.service) what += ", service";
        glColor3f(0.75f, 0.73f, 0.67f);
        draw_text(x0 + w - 14 - static_cast<float>(text_width(what, kScale)), y, what, kScale);
        add_row(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4, RowPanel::TrainTool, kStopRow + i);
        y += kRowH;
    }
    y += 8;
    // Buttons: a row of small ones for the selected stop, then Buy and Clear.
    const auto button = [&](float bx0, float bx1, float by, const std::string& label, std::size_t action, bool live) {
        ui::tab(bx0, by, bx1, by + kRowH + 2, false);
        if (live) glColor3f(0.95f, 0.92f, 0.82f);
        else glColor3f(0.5f, 0.48f, 0.44f);
        draw_text((bx0 + bx1 - static_cast<float>(text_width(label, kScale))) / 2, by + 4, label, kScale);
        if (live) add_row(bx0, by, bx1, by + kRowH + 2, RowPanel::TrainTool, action);
    };
    const bool has = !stops_.empty();
    const float bw = (w - 20 - 3 * 6) / 4;
    const sim::ConsistRule* r = has ? &stops_[stop_sel_].rule : nullptr;
    button(x0 + 10, x0 + 10 + bw, y, "- car", kFewer, true);
    button(x0 + 10 + (bw + 6), x0 + 10 + bw * 2 + 6, y, "+ car", kMore, true);
    button(x0 + 10 + 2 * (bw + 6), x0 + 10 + bw * 3 + 12, y, r ? std::string(sim::cargo_filter_name(r->filter)) : "Kind", kKind, has);
    button(x0 + 10 + 3 * (bw + 6), x0 + w - 10, y, r && r->min > 0 ? "Full: on" : "Full: off", kFull, has);
    y += kRowH + 8;
    const float bw3 = (w - 20 - 2 * 6) / 3;
    button(x0 + 10, x0 + 10 + bw3, y, has && stops_[stop_sel_].service ? "Service: on" : "Service: off", kService, has);
    button(x0 + 10 + bw3 + 6, x0 + 10 + 2 * bw3 + 6, y, "Drop last", kDrop, has);
    button(x0 + 10 + 2 * (bw3 + 6), x0 + w - 10, y, "Clear", kClear, has || editing_.has_value());
    y += kRowH + 12;
    // Buy (or save the route being edited).
    std::string buy = "Click two stations or more";
    bool ready = stops_.size() >= 2 && !locos.empty();
    // An electric engine needs its route wired all the way (rt3-clone-spec
    // §10.2 [D]): say where it is not, before the click.
    std::string unwired;
    const sim::LocoTypeId engine = editing_ ? world_.railway().train(*editing_).loco : (locos.empty() ? 0 : locos[chosen]);
    if (ready && data.locomotives.get(engine).fuel == sim::Fuel::Electric) {
        if (const auto why = world_.railway().electric_route_problem(pending_route())) {
            ready = false;
            const auto at = why->find(" from ");
            unwired = at == std::string::npos ? *why : "Not wired" + why->substr(at);
        }
    }
    if (!unwired.empty()) buy = unwired;
    else if (editing_) buy = ready ? "Save the new route" : buy;
    else if (ready) buy = "Buy " + data.locomotives.get(locos[chosen]).name + " for " +
                          format_money(data.locomotives.get(locos[chosen]).cost);
    if (!unwired.empty()) {
        // The way out, under the button.
        glColor3f(0.75f, 0.73f, 0.67f);
        draw_text(x0 + 14, y + kRowH + 12, "Electrify it in the Track tool (E, or Shift+E for all)", kScale);
    }
    ui::tab(x0 + 10, y, x0 + w - 10, y + kRowH + 6, ready);
    if (ready) glColor3f(0.12f, 0.08f, 0.04f);
    else if (!unwired.empty()) glColor3f(1.0f, 0.5f, 0.42f);
    else glColor3f(0.55f, 0.52f, 0.46f);
    draw_text(x0 + (w - static_cast<float>(text_width(buy, kScale))) / 2, y + 6, buy, kScale);
    if (ready) add_row(x0 + 10, y, x0 + w - 10, y + kRowH + 6, RowPanel::TrainTool, kBuy);
}

namespace {
// The options panel's rows: the Track tool's from 0, the Station tool's
// sizes from kSizeRow and what it builds from kBuildRow.
enum TrackOption : std::size_t { kDouble = 0, kCurves, kTunnels, kBridges, kOverpasses, kUndo, kUndoBuild, kWireAll };
constexpr std::size_t kSizeRow = 100, kBuildRow = 200, kPlantRow = 300;
constexpr sim::StationBuildingType kBuildings[] = {sim::StationBuildingType::PostOffice, sim::StationBuildingType::Hotel,
                                                   sim::StationBuildingType::Restaurant, sim::StationBuildingType::Tavern};
} // namespace

void Tools::tool_options_click(std::size_t index) {
    switch (index) {
    case kDouble: on_key(SDLK_d, 0); return;
    case kCurves: on_key(SDLK_c, 0); return;
    case kTunnels: on_key(SDLK_t, 0); return;
    case kBridges: on_key(SDLK_b, 0); return;
    case kOverpasses: on_key(SDLK_g, 0); return;
    case kUndo: on_key(SDLK_z, KMOD_CTRL); return;
    case kUndoBuild: undo_build(); return;
    case kWireAll: on_key(SDLK_e, KMOD_SHIFT); return;
    default: break;
    }
    if (index >= kPlantRow) {
        build_choice_ = index - kPlantRow;
        return;
    }
    if (index >= kSizeRow && index < kSizeRow + 3) {
        station_size_ = static_cast<sim::StationSize>(index - kSizeRow);
        station_building_.reset();
    } else if (index == kBuildRow) {
        station_building_.reset();
    } else if (index > kBuildRow && index <= kBuildRow + std::size(kBuildings)) {
        station_building_ = kBuildings[index - kBuildRow - 1];
    }
}

void Tools::draw_tool_options() const {
    if (founding_ || menu_open_ || help_open_ || guide_open_) {
        side_panel_box_ = {};
        return;
    }
    constexpr float kRowH = 24;
    const float w = 300, x0 = 10, y0 = kToolbarY + kButtonH + 10;
    const bool track = tool_ == Tool::Track;
    const float h = track ? 40 + kRowH * 5 + 44 + kRowH + 8 : 40 + 22 + kRowH + 16 + 22 + kRowH * 5 + 44;
    side_panel_box_ = {x0, y0, x0 + w, y0 + h};
    ui::panel(x0, y0, x0 + w, y0 + h);
    float y = y0 + 10;
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y, track ? "Track" : "Station", kScale + 1);
    y += 30;
    // A row: its name, then its setting at the right; the chosen one highlighted.
    const auto row = [&](const std::string& name, const std::string& value, std::size_t index, bool chosen) {
        if (chosen) ui::selection(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4);
        glColor3f(0.92f, 0.9f, 0.84f);
        draw_text(x0 + 14, y + 1, name, kScale);
        glColor3f(chosen ? 1.0f : 0.8f, chosen ? 0.95f : 0.78f, chosen ? 0.75f : 0.7f);
        draw_text(x0 + w - 14 - static_cast<float>(text_width(value, kScale)), y + 1, value, kScale);
        add_row(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4, RowPanel::ToolOptions, index);
        y += kRowH;
    };
    if (track) {
        const auto setting = [](sim::Frequency f) {
            std::string out = sim::frequency_name(f);
            if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
            return out;
        };
        row("Double track", double_track_ ? "On" : "Off", kDouble, double_track_);
        row("Curves", curves_ ? "On" : "Off", kCurves, curves_);
        row("Tunnels", setting(tunnels_), kTunnels, false);
        row("Bridges", setting(bridges_), kBridges, false);
        row("Overpasses", setting(overpasses_), kOverpasses, false);
        y += 8;
        ui::tab(x0 + 10, y, x0 + w - 10, y + kRowH + 2, false);
        glColor3f(0.95f, 0.92f, 0.82f);
        const std::string undo = "Undo the last track (Ctrl+Z)";
        draw_text(x0 + (w - static_cast<float>(text_width(undo, kScale))) / 2, y + 5, undo, kScale);
        add_row(x0 + 10, y, x0 + w - 10, y + kRowH + 2, RowPanel::ToolOptions, kUndo);
        y += kRowH + 8;
        ui::tab(x0 + 10, y, x0 + w - 10, y + kRowH + 2, false);
        glColor3f(0.95f, 0.92f, 0.82f);
        const std::string wire = "Electrify all your track (Shift+E)";
        draw_text(x0 + (w - static_cast<float>(text_width(wire, kScale))) / 2, y + 5, wire, kScale);
        add_row(x0 + 10, y, x0 + w - 10, y + kRowH + 2, RowPanel::ToolOptions, kWireAll);
        return;
    }
    // The station's size, as three buttons with their prices.
    glColor3f(0.75f, 0.73f, 0.67f);
    draw_text(x0 + 14, y, "Size and the ground it serves", kScale);
    y += 22;
    const float bw = (w - 20 - 2 * 6) / 3;
    for (int i = 0; i < 3; ++i) {
        const auto size = static_cast<sim::StationSize>(i);
        const bool on = !station_building_ && station_size_ == size;
        const float bx = x0 + 10 + static_cast<float>(i) * (bw + 6);
        ui::tab(bx, y, bx + bw, y + kRowH + 2, on);
        const std::string label = size_name(size);
        if (on) glColor3f(0.12f, 0.08f, 0.04f);
        else glColor3f(0.92f, 0.9f, 0.84f);
        draw_text(bx + (bw - static_cast<float>(text_width(label, kScale))) / 2, y + 5, label, kScale);
        add_row(bx, y, bx + bw, y + kRowH + 2, RowPanel::ToolOptions, kSizeRow + static_cast<std::size_t>(i));
    }
    y += kRowH + 16;
    glColor3f(0.75f, 0.73f, 0.67f);
    draw_text(x0 + 14, y, "Build", kScale);
    y += 22;
    row("A station", format_money(sim::station_cost(station_size_)), kBuildRow, !station_building_);
    for (std::size_t i = 0; i < std::size(kBuildings); ++i) {
        const sim::StationBuildingType t = kBuildings[i];
        row(std::string(sim::station_building_name(t)) + " by a station",
            format_money(world_.construction_cost(sim::station_building_cost(t, world_.data().balance))), kBuildRow + 1 + i,
            station_building_ == t);
    }
    y += 8;
    ui::tab(x0 + 10, y, x0 + w - 10, y + kRowH + 2, false);
    glColor3f(0.95f, 0.92f, 0.82f);
    const std::string undo = "Undo the last built (Ctrl+Z)";
    draw_text(x0 + (w - static_cast<float>(text_width(undo, kScale))) / 2, y + 5, undo, kScale);
    add_row(x0 + 10, y, x0 + w - 10, y + kRowH + 2, RowPanel::ToolOptions, kUndoBuild);
}

void Tools::draw_industry_options() const {
    if (founding_ || menu_open_ || help_open_ || guide_open_) {
        side_panel_box_ = {};
        return;
    }
    constexpr float kRowH = 22;
    const auto types = buildable_types();
    const auto& data = world_.data();
    const std::size_t chosen = types.empty() ? 0 : build_choice_ % types.size();
    const std::size_t shown = std::min<std::size_t>(12, types.size());
    const std::size_t first = types.size() <= shown ? 0 : std::min(chosen > 5 ? chosen - 5 : 0, types.size() - shown);
    const float w = 380, x0 = 10, y0 = kToolbarY + kButtonH + 10;
    const float h = 40 + 3 * 20 + 10 + 22 + kRowH * static_cast<float>(std::max<std::size_t>(1, shown)) + 10;
    side_panel_box_ = {x0, y0, x0 + w, y0 + h};
    ui::panel(x0, y0, x0 + w, y0 + h);
    float y = y0 + 10;
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y, "Industry", kScale + 1);
    y += 30;
    // What a click on the map does.
    glColor3f(0.8f, 0.78f, 0.72f);
    for (const char* line : {"Click an industry to buy it, or yours to upgrade it;", "open land builds the plant chosen below.",
                             "M and C: what a warehouse or port under the mouse does."}) {
        draw_text(x0 + 14, y, line, kScale);
        y += 20;
    }
    y += 10;
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y, "Plants to build, " + format_money(world_.construction_cost(sim::industry_build_cost(data.balance.industries))) + " each",
              kScale);
    y += 22;
    if (types.empty()) {
        glColor3f(0.7f, 0.68f, 0.62f);
        draw_text(x0 + 14, y, "None can be built yet", kScale);
        return;
    }
    for (std::size_t i = first; i < first + shown; ++i) {
        const sim::IndustryType& t = data.industries.get(types[i]);
        if (i == chosen) ui::selection(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4);
        glColor3f(0.92f, 0.9f, 0.84f);
        draw_text(x0 + 14, y, t.name, kScale);
        std::string makes;
        for (const sim::CargoId c : t.outputs) makes += (makes.empty() ? "" : ", ") + data.cargo.get(c).name;
        if (t.kind == sim::IndustryKind::Warehouse) makes = "keeps cargo";
        glColor3f(0.75f, 0.73f, 0.67f);
        draw_text(x0 + w - 14 - static_cast<float>(text_width(makes, kScale)), y, makes, kScale);
        add_row(x0 + 8, y - 2, x0 + w - 8, y + kRowH - 4, RowPanel::ToolOptions, kPlantRow + i);
        y += kRowH;
    }
}

} // namespace railmaster::client
