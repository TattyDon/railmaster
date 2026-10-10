#include "railmaster/sim/scenario.hpp"

#include "railmaster/sim/world.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace railmaster::sim {

const char* medal_name(Medal m) {
    switch (m) {
    case Medal::Bronze: return "Bronze";
    case Medal::Silver: return "Silver";
    case Medal::Gold: return "Gold";
    }
    return "?";
}

namespace {

Date parse_date(const std::string& s) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) {
        throw std::runtime_error("bad date '" + s + "' (expected YYYY-MM-DD)");
    }
    return Date::from_ymd(y, m, d);
}

GoalKind parse_kind(const std::string& k) {
    if (k == "connect_towns") return GoalKind::ConnectTowns;
    if (k == "territories") return GoalKind::Territories;
    if (k == "book_value") return GoalKind::BookValue;
    if (k == "company_cash") return GoalKind::CompanyCash;
    if (k == "revenue") return GoalKind::Revenue;
    if (k == "personal_net_worth") return GoalKind::PersonalNetWorth;
    if (k == "industry_profit") return GoalKind::IndustryProfit;
    if (k == "deliver") return GoalKind::Deliver;
    if (k == "only_railroad") return GoalKind::OnlyRailroad;
    if (k == "highest_value") return GoalKind::HighestValue;
    throw std::runtime_error("unknown goal kind '" + k + "'");
}

std::string dollars(Money m) {
    const std::string digits = std::to_string(m.whole_dollars() < 0 ? -m.whole_dollars() : m.whole_dollars());
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (m.whole_dollars() < 0 ? "-$" : "$") + out;
}

} // namespace

Scenario Scenario::from_json(std::string_view json_text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("scenario: ") + e.what());
    }
    Scenario s;
    try {
        s.key = j.at("key").get<std::string>();
        s.name = j.at("name").get<std::string>();
        s.briefing = j.value("briefing", std::string());
        s.start = parse_date(j.at("start").get<std::string>());
        s.seed = j.value("seed", std::uint64_t{1});
        s.map_size = j.value("map", std::string("small"));
        if (s.map_size != "small" && s.map_size != "medium" && s.map_size != "large") {
            throw std::runtime_error("map must be small, medium or large");
        }
        s.rivals = j.value("rivals", 0);
        s.territories = j.value("territories", 0);
        if (j.contains("founder_fortune")) s.founder_fortune = j.at("founder_fortune").get<std::int64_t>();
        if (j.contains("founder_investment")) s.founder_investment = j.at("founder_investment").get<std::int64_t>();
        if (j.contains("outside_investment")) s.outside_investment = j.at("outside_investment").get<std::int64_t>();
        s.chairman_can_resign = j.value("chairman_can_resign", true);
        s.chairman_can_be_fired = j.value("chairman_can_be_fired", true);
        for (const auto& t : j.value("towns", nlohmann::json::array())) {
            s.towns.push_back({t.at("name").get<std::string>(), t.at("x").get<std::int32_t>(), t.at("y").get<std::int32_t>(),
                               t.value("houses", 20)});
        }
        const auto& medals = j.at("medals");
        const char* keys[] = {"bronze", "silver", "gold"};
        for (std::size_t m = 0; m < 3; ++m) {
            const auto& tier = medals.at(keys[m]);
            s.medals[m].deadline = parse_date(tier.at("by").get<std::string>());
            for (const auto& g : tier.at("goals")) {
                Goal goal;
                goal.kind = parse_kind(g.at("kind").get<std::string>());
                goal.towns = g.value("towns", std::vector<std::string>{});
                goal.cargo = g.value("cargo", std::string());
                goal.count = g.value("count", std::int64_t{0});
                goal.amount = Money::dollars(g.value("amount", std::int64_t{0}));
                for (const std::string& name : goal.towns) {
                    if (std::none_of(s.towns.begin(), s.towns.end(), [&](const ScenarioTown& t) { return t.name == name; })) {
                        throw std::runtime_error("goal names town '" + name + "', which the scenario does not place");
                    }
                }
                if (goal.kind == GoalKind::ConnectTowns && goal.towns.size() < 2) {
                    throw std::runtime_error("connect_towns needs at least two towns");
                }
                if (goal.kind == GoalKind::Deliver && (goal.cargo.empty() || goal.count <= 0)) {
                    throw std::runtime_error("deliver needs a cargo and a count");
                }
                s.medals[m].goals.push_back(std::move(goal));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("scenario: ") + e.what());
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        throw std::runtime_error(what.rfind("scenario:", 0) == 0 ? what : "scenario: " + what);
    }
    if (s.founder_fortune && s.founder_investment && *s.founder_investment > *s.founder_fortune) {
        throw std::runtime_error("scenario: founder_investment is more than founder_fortune");
    }
    if (s.medals[0].goals.empty()) throw std::runtime_error("scenario: bronze needs at least one goal");
    for (const MedalTier& t : s.medals) {
        if (t.deadline.days_since_epoch() <= s.start.days_since_epoch()) {
            throw std::runtime_error("scenario: every deadline must be after the start");
        }
    }
    return s;
}

std::vector<Goal> Scenario::goals_for(Medal m) const {
    std::vector<Goal> out;
    for (std::size_t i = 0; i <= static_cast<std::size_t>(m); ++i) out.insert(out.end(), medals[i].goals.begin(), medals[i].goals.end());
    return out;
}

Date Scenario::last_deadline() const {
    Date last = medals[0].deadline;
    for (const MedalTier& t : medals)
        if (t.deadline.days_since_epoch() > last.days_since_epoch()) last = t.deadline;
    return last;
}

GoalProgress check_goal(const World& w, const Goal& g) {
    const auto co = w.player_company();
    const auto against = [](const char* what, Money have, Money want) {
        return GoalProgress{have >= want, std::string(what) + " " + dollars(have) + " of " + dollars(want)};
    };
    switch (g.kind) {
    case GoalKind::ConnectTowns: {
        std::string label = "Connect";
        for (const std::string& t : g.towns) label += " " + t;
        if (!co) return {false, label};
        // A station of yours serving each town, every one joined by track
        // to the first town's.
        const Railway& rw = w.railway();
        std::vector<std::vector<NodeId>> served;
        for (const std::string& name : g.towns) {
            std::vector<NodeId> nodes;
            for (std::size_t t = 0; t < w.economy().towns().size(); ++t) {
                if (w.economy().towns()[t].name != name) continue;
                for (const Station& s : rw.stations())
                    if (s.owner == *co && s.town == t) nodes.push_back(s.node);
            }
            served.push_back(std::move(nodes));
        }
        std::int32_t joined = 0;
        for (std::size_t i = 0; i < served.size(); ++i) {
            if (served[i].empty()) continue;
            if (i == 0) {
                ++joined;
                continue;
            }
            bool linked = false;
            for (NodeId a : served[0])
                for (NodeId b : served[i]) linked = linked || rw.track().shortest_path(a, b).has_value();
            joined += linked;
        }
        const auto n = static_cast<std::int32_t>(g.towns.size());
        return {joined == n && !served[0].empty(), label + ": " + std::to_string(joined) + " of " + std::to_string(n)};
    }
    case GoalKind::Territories: {
        std::vector<TerritoryId> seen;
        if (co) {
            for (const Station& s : w.railway().stations()) {
                if (s.owner != *co) continue;
                const TerritoryId t = w.territory_at(w.railway().track().node(s.node).pos);
                if (t != kNoTerritory && std::find(seen.begin(), seen.end(), t) == seen.end()) seen.push_back(t);
            }
        }
        const auto have = static_cast<std::int64_t>(seen.size());
        return {have >= g.count, "Stations in " + std::to_string(have) + " of " + std::to_string(g.count) + " territories"};
    }
    case GoalKind::BookValue: return against("Book value", co ? w.company().book_value() : Money{}, g.amount);
    case GoalKind::CompanyCash: return against("Company cash", co ? w.company().cash() : Money{}, g.amount);
    case GoalKind::Revenue:
        return against("Revenue, last 12 months", co ? w.company().trailing_revenue().value_or(Money{}) : Money{}, g.amount);
    case GoalKind::PersonalNetWorth: return against("Personal net worth", net_worth(w.investor(), w.market()), g.amount);
    case GoalKind::IndustryProfit: {
        Money profit;
        if (co) {
            for (const YearAccounts& y : w.company().history())
                profit += y.lines[static_cast<std::size_t>(Ledger::IndustryIncome)] -
                          y.lines[static_cast<std::size_t>(Ledger::IndustryCosts)];
        }
        return against("Industry profit", profit, g.amount);
    }
    case GoalKind::Deliver: {
        const auto cargo = w.data().cargo.find(g.cargo);
        const std::int64_t loads = co && cargo ? w.company().delivered_milli(*cargo) / kMilli : 0;
        return {loads >= g.count, "Deliver " + g.cargo + ": " + std::to_string(loads) + " of " + std::to_string(g.count) + " loads"};
    }
    case GoalKind::OnlyRailroad: {
        std::int32_t others = 0;
        for (const Company& c : w.companies())
            if (!c.defunct() && (!co || c.id() != *co)) ++others;
        return {co.has_value() && others == 0, "Rival railroads still running: " + std::to_string(others)};
    }
    case GoalKind::HighestValue: {
        Money best;
        for (const Company& c : w.companies())
            if (!c.defunct() && (!co || c.id() != *co)) best = std::max(best, c.market_cap());
        const Money mine = co ? w.company().market_cap() : Money{};
        return {co.has_value() && mine > best, "Market value " + dollars(mine) + ", best rival " + dollars(best)};
    }
    }
    return {};
}

std::int64_t scenario_score_milli(Medal m, std::int32_t difficulty_percent, std::int32_t years_early) {
    const std::int64_t base = static_cast<std::int64_t>(m) + 1;
    return base * 1000 * difficulty_percent / 100 * (100 + 5 * std::max(0, years_early)) / 100;
}

} // namespace railmaster::sim
