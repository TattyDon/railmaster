#include "tools.hpp"
#include "ui.hpp"

#include "font.hpp"

#include "railmaster/sim/freight.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
}

void Tools::draw_ledger_panel() const {
    const sim::Company& co = world_.company();
    const auto& years = co.history();
    const float x0 = 40, y0 = 56, w = std::min(1180.0f, static_cast<float>(cam_.width_px) - 80);
    const float h = static_cast<float>(cam_.height_px) - kBottomBarH - y0 - 12;
    ui::panel(x0, y0, x0 + w, y0 + h);
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, y0 + 8, "Ledger: " + as_written(co.name()), kScale + 1);
    float y = draw_tabs(x0 + 14, y0 + 36, {"Overview", "Income statement", "Balance sheets", "Haulage", "Players"}, ledger_tab_);
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
    default: {
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
    }
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
    // Leaving the Track tool ends free undo [D].
    if (tool_ == Tool::Track && t != Tool::Track) world_.execute(sim::EndTrackUndo{});
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
    cmd.overpasses = overpasses_;
    cmd.tunnels = tunnels_;
    cmd.bridges = bridges_;
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
            if (sx >= b.x0 && sx < b.x1 && sy >= b.y0 && sy < b.y1) (tool_ == Tool::Finance ? company_tab_ : ledger_tab_) = static_cast<int>(i);
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
        const auto s = station_near(hover_);
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
            const int n = tool_ == Tool::Finance ? 4 : 5;
            tab = (tab + ((mod & KMOD_SHIFT) != 0 ? n - 1 : 1)) % n;
            return true;
        }
        break;
    case SDLK_o: cycle_overlay(+1); return true;
    case SDLK_p: cycle_overlay(-1); return true;
    case SDLK_ESCAPE:
        if (track_start_ || !stops_.empty() || editing_) cancel();
        else if (detail_) {
            detail_.reset();
            follow_ = false;
        } else select(Tool::Inspect);
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
                cancel();
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
                cancel();
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
                show("Bought " + world_.data().locomotives.get(loco).name + " for " + format_money(r.cost), true);
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
    const float h = kRow * static_cast<float>(companies.size() + world_.investors().size() + 12);
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
    y += kRow / 2;
    glColor3f(1.0f, 0.9f, 0.5f);
    line("Players");
    for (const sim::Investor& inv : world_.investors()) {
        glColor3f(0.85f, 0.85f, 0.85f);
        line(inv.name + "   net worth " + format_money(sim::net_worth(inv, world_.market())) + "   cash " +
             format_money(inv.cash) + "   " +
             (inv.chairs ? "Runs " + world_.company(*inv.chairs).name() : std::string("No company")));
    }
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
                                {MapOverlay::Traffic, 0}};
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
        for (std::size_t i = 0; i < stops_.size(); ++i) {
            const sim::MapPoint p = net.node(world_.railway().station(stops_[i].station).node).pos;
            if (i == stop_sel_) draw_marker(p, cam_, 11, 1.0f, 1.0f, 1.0f); // the stop the consist keys change
            if (stops_[i].service) draw_marker(p, cam_, 7, 0.3f, 0.9f, 1.0f);
            else draw_marker(p, cam_, 7, 1.0f, 0.85f, 0.2f);
            if (i + 1 < stops_.size()) {
                draw_line(p, net.node(world_.railway().station(stops_[i + 1].station).node).pos, cam_, 1.0f, 0.85f, 0.2f, 0.8f);
            }
        }
        if (const auto s = station_near(hover_)) draw_marker(net.node(world_.railway().station(*s).node).pos, cam_, 8, 1, 1, 1);
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
    if (!id) return "Open land";
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
        case MapOverlay::Cargo: cargo_map += overlay_ ? world_.data().cargo.get(*overlay_).name : "Off"; break;
        }
        if (detail_) {
            std::string keys = "Click another to show it  Esc close";
            if (detail_train()) keys = "E route  H priority  F filter  W full  [ ] cars  C caboose  D diner  Y copy  XX retire  R engine  Space follow  Esc close";
            return keys;
        }
        return cargo_map + "   Click a train, station or industry for its details.  Train: E route H priority Y copy XX retire R engine";
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
               format_money(sim::station_cost(station_size_));
    case Tool::ServiceTower:
        return "Click on track: service tower (water, sand) " +
               format_money(sim::service_building_cost(sim::ServiceType::ServiceTower));
    case Tool::Maintenance:
        return "Click on track: maintenance facility (oil) " +
               format_money(sim::service_building_cost(sim::ServiceType::MaintenanceFacility));
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
        return std::string(editing_ ? "Train " + std::to_string(*editing_ + 1) + " " : "") + stop_text() +
               (editing_ ? "  Enter saves" : "  Enter buys " + loco);
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
        return "Tab trains/stations/companies   Up/Down, PgUp/PgDn choose   Enter go to it";
    case Tool::Finance:
        if (company_tab_ == 3) return "Tab next page   Up/Down choose a territory   T buy access to it";
        return "Tab next page   B/R bond issue/repay   A/S buy/sell 1,000 shares (Shift 5,000)   I/Y issue/buy back stock   "
               "[ ] dividend   KK bankruptcy";
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
}

bool Tools::open_detail_under_cursor() {
    const sim::Railway& rw = world_.railway();
    for (const sim::Train& t : rw.trains()) {
        if (t.in_service() && sim::distance_mm(rw.train_position(t.id), hover_) <= snap_mm()) {
            open_detail(DetailKind::Train, t.id);
            return true;
        }
    }
    if (const auto st = station_near(hover_)) {
        open_detail(DetailKind::Station, *st);
        return true;
    }
    if (const auto site = site_under_cursor()) {
        open_detail(DetailKind::Industry, *site);
        return true;
    }
    return false;
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
    enum class Style { Title, Heading, Text, Rule, Gauge } style = Style::Text;
    std::vector<std::string> cells{};
    float r = 0.9f, g = 0.88f, b = 0.82f;
    float fraction = 0; // gauges
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
            rows.push_back(line({"Engine", as_written(loco.name)}));
            rows.push_back(line({"Age", years(today - t.built_day)}));
            rows.push_back(line({"Reliability", std::string(reliability_name(loco.reliability)) + ", " +
                                                    std::to_string(t.breakdowns) + (t.breakdowns == 1 ? " breakdown" : " breakdowns")}));
            rows.push_back(line({"Priority", std::to_string(t.priority)}));
            rows.push_back(heading("Gauges"));
            if (loco.fuel == sim::Fuel::Steam) rows.push_back(gauge("Water", t.water));
            rows.push_back(gauge("Sand", t.sand));
            rows.push_back(gauge("Oil", t.oil));
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
            rows.push_back(heading("Money"));
            rows.push_back(money_row("Profit this month", t.profit_this_month));
            if (!t.profit_months.empty()) {
                rows.push_back(money_row("Profit, last " + std::to_string(t.profit_months.size()) + " months", t.profit_last_year()));
            }
            rows.push_back(money_row("Earned in all", t.revenue));
            rows.push_back(line({"Engine upkeep", format_money(loco.maintenance_per_year) + " a year"}));
        }
    } else if (detail_->kind == DetailKind::Station) {
        const sim::Station& s = rw.station(detail_->id);
        rows.push_back(title(as_written(s.name)));
        rows.push_back(dim({owner_name(s.owner)}));
        rows.push_back(rule_row());
        rows.push_back(line({"Size", std::string(size_name(s.size)) + ", reaches " +
                                         std::to_string(sim::catchment_radius(s.size, world_.data().balance)) + " cells"}));
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
    for (const DetailRow& r : rows) h += r.style == DetailRow::Style::Title ? 24 : r.style == DetailRow::Style::Rule ? 8 : r.style == DetailRow::Style::Heading ? kRow + 6 : kRow;
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
            y += kRow;
            break;
        case DetailRow::Style::Gauge: {
            glColor3f(r.r, r.g, r.b);
            draw_text(x0 + 24, y, r.cells[0], kScale);
            const float gx0 = x0 + 110, gx1 = x0 + kW - 70, gy = y + 4;
            fill_rect(gx0, gy, gx1, gy + 9, 0.0f, 0.0f, 0.0f, 0.45f);
            const float f = std::clamp(r.fraction, 0.0f, 1.0f);
            if (f < 0.25f) fill_rect(gx0, gy, gx0 + (gx1 - gx0) * f, gy + 9, 0.9f, 0.3f, 0.2f, 0.95f);
            else if (f < 0.5f) fill_rect(gx0, gy, gx0 + (gx1 - gx0) * f, gy + 9, 0.95f, 0.75f, 0.25f, 0.95f);
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
    if (const auto st = station_near(hover_)) {
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
    if (tool_ == Tool::Inspect && detail_) draw_detail_panel();
    draw_choice_dialog();

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
