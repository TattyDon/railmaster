#include "goals.hpp"

#include "font.hpp"
#include "tools.hpp"
#include "ui.hpp"

#include <SDL_opengl.h>

#include <algorithm>
#include <set>

namespace railmaster::client {

namespace {

constexpr int kScale = 2;
constexpr float kTop = 54.0f; // under the toolbar

struct Goal {
    const char* title;
    const char* hint;
};

constexpr Goal kGoals[] = {
    {"Run a paying train", "Join two towns with track, put a station in each and buy a train between them (the first steps show how)."},
    {"Reach a third town",
     "Lay track on from one of your stations to another town, build a station there, and add it to a train's route (click "
     "the train, E) or buy a new train."},
    {"Carry freight",
     "Freight pays where it is wanted. Ctrl+G opens the Guide: who makes each cargo and who pays most for it. Put a station "
     "by a farm, mine or mill and run a train to a town or plant that wants what it makes."},
    {"Run four trains", "More trains on the lines that pay: F6, click the stations, Enter. The Ledger (L) shows which trains earn most."},
    {"Make $250,000 a year", "Profit over the last twelve months. Drop or re-route trains that lose money; carry what sells for more."},
    {"Double your share price", "The market pays for steady, growing profits. F7 shows the price; dividends (D) please investors."},
    {"Make $2,000,000 a year", "Link your lines into a network, carry freight to where prices are high, and buy industries that feed it."},
};
constexpr int kCount = sizeof(kGoals) / sizeof(kGoals[0]);

} // namespace

Goals::Progress Goals::progress(const sim::World& world) {
    Progress p;
    p.count = kCount;
    if (!world.player_company()) return p;
    const sim::CompanyId me = *world.player_company();
    const sim::Company& co = world.company(me);
    const sim::Railway& rw = world.railway();
    std::set<std::size_t> towns;
    for (const sim::Station& s : rw.stations())
        if (s.owner == me && s.town) towns.insert(*s.town);
    int trains = 0;
    bool paid = false;
    for (const sim::Train& t : rw.trains()) {
        if (!t.in_service() || t.owner != me) continue;
        ++trains;
        paid |= t.revenue > sim::Money{};
    }
    std::int64_t freight = 0;
    for (const sim::CargoType& c : world.data().cargo.all())
        if (c.cargo_class == sim::CargoClass::Freight) freight += co.delivered_milli(c.id);
    const sim::Money profit = co.trailing_profit().value_or(sim::Money{});
    const auto dollars = [](sim::Money m) { return format_money(m); };
    const sim::Money founding = sim::Money::cents(world.data().balance.stock.founding_share_price_cents);

    struct Check {
        bool met;
        std::string status;
    };
    const Check checks[kCount] = {
        {paid, paid ? "done" : std::to_string(trains) + (trains == 1 ? " train" : " trains") + ", not paid yet"},
        {towns.size() >= 3, std::to_string(towns.size()) + " of 3 towns"},
        {freight >= sim::kMilli, std::to_string(freight / sim::kMilli) + " of 1 load delivered"},
        {trains >= 4, std::to_string(trains) + " of 4 trains"},
        {profit >= sim::Money::dollars(250'000), dollars(profit) + " of $250,000 a year"},
        {co.share_price() >= founding.scaled(2, 1), dollars(co.share_price()) + " of " + dollars(founding.scaled(2, 1))},
        {profit >= sim::Money::dollars(2'000'000), dollars(profit) + " of $2,000,000 a year"},
    };
    p.index = kCount;
    for (int i = 0; i < kCount; ++i) {
        if (!checks[i].met) {
            p.index = i;
            p.title = kGoals[i].title;
            p.hint = kGoals[i].hint;
            p.status = checks[i].status;
            break;
        }
    }
    if (p.index == kCount) {
        p.title = "Every goal met";
        p.hint = "You are a railroad tycoon. Scenarios on the title screen set new challenges, with medals.";
    }
    return p;
}

std::string Goals::update(const sim::World& world) {
    if (world.scenario() || !world.player_company()) return {};
    const int now = progress(world).index;
    std::string met;
    if (last_index_ >= 0 && now > last_index_) met = std::string("Goal met: ") + kGoals[std::min(now - 1, kCount - 1)].title;
    last_index_ = now;
    return met;
}

void Goals::draw(const sim::World& world, float width) const {
    if (world.scenario() || !world.player_company()) return;
    const Progress p = progress(world);
    const std::string head = (p.index < p.count ? "Goal " + std::to_string(p.index + 1) + " of " + std::to_string(p.count) + ": " : std::string()) + p.title +
                             (p.status.empty() ? "" : "  (" + p.status + ")");
    const float w = std::min(width - 40.0f, std::max(360.0f, static_cast<float>(text_width(head, kScale)) + 40.0f));
    const float x0 = (width - w) / 2;
    std::vector<std::string> lines;
    if (expanded_) {
        std::string line;
        for (std::size_t at = 0; at < p.hint.size();) {
            const std::size_t sp = p.hint.find(' ', at);
            const std::string word = p.hint.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
            const std::string longer = line.empty() ? word : line + " " + word;
            if (!line.empty() && static_cast<float>(text_width(longer, kScale)) > w - 28) {
                lines.push_back(line);
                line = word;
            } else {
                line = longer;
            }
            at = sp == std::string::npos ? p.hint.size() : sp + 1;
        }
        if (!line.empty()) lines.push_back(line);
    }
    const float h = 28 + 18.0f * static_cast<float>(lines.size());
    ui::panel(x0, kTop, x0 + w, kTop + h);
    box_[0] = x0;
    box_[1] = kTop;
    box_[2] = x0 + w;
    box_[3] = kTop + h;
    glColor3f(1.0f, 0.86f, 0.5f);
    draw_text(x0 + 14, kTop + 6, head, kScale);
    glColor3f(0.86f, 0.84f, 0.78f);
    float y = kTop + 26;
    for (const std::string& l : lines) {
        draw_text(x0 + 14, y, l, kScale);
        y += 18;
    }
}

bool Goals::click(float x, float y) {
    if (x < box_[0] || x > box_[2] || y < box_[1] || y > box_[3]) return false;
    expanded_ = !expanded_;
    return true;
}

} // namespace railmaster::client
