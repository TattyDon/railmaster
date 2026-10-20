#include "railmaster/sim/scenario.hpp"

#include "railmaster/sim/world.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <functional>
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
    if (k == "average_speed") return GoalKind::AverageSpeed;
    if (k == "variable") return GoalKind::Variable;
    throw std::runtime_error("unknown goal kind '" + k + "'");
}

EffectKind parse_effect(const std::string& k) {
    if (k == "news") return EffectKind::News;
    if (k == "company_cash") return EffectKind::CompanyCash;
    if (k == "player_cash") return EffectKind::PlayerCash;
    if (k == "cargo_price") return EffectKind::CargoPrice;
    if (k == "express_demand") return EffectKind::ExpressDemand;
    if (k == "production") return EffectKind::Production;
    if (k == "build_cost") return EffectKind::BuildCost;
    if (k == "unlock_engine") return EffectKind::UnlockEngine;
    if (k == "territory_price") return EffectKind::TerritoryPrice;
    if (k == "set_variable") return EffectKind::SetVariable;
    if (k == "add_variable") return EffectKind::AddVariable;
    if (k == "briefing") return EffectKind::Briefing;
    if (k == "choice") return EffectKind::Choice;
    if (k == "earthquake") return EffectKind::Earthquake;
    if (k == "sabotage") return EffectKind::Sabotage;
    throw std::runtime_error("unknown effect kind '" + k + "'");
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
        s.landscape = j.value("landscape", std::string());
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
        const auto parse_goal = [&](const nlohmann::json& g) {
            Goal goal;
            goal.kind = parse_kind(g.at("kind").get<std::string>());
            goal.towns = g.value("towns", std::vector<std::string>{});
            goal.cargo = g.value("cargo", std::string());
            goal.from = g.value("from", std::string());
            goal.to = g.value("to", std::string());
            goal.name = g.value("name", std::string());
            if ((!goal.from.empty() || !goal.to.empty()) && goal.kind != GoalKind::Deliver) {
                throw std::runtime_error("from and to are for deliver goals");
            }
            for (const std::string& name : {goal.from, goal.to}) {
                if (name.empty()) continue;
                if (std::none_of(s.towns.begin(), s.towns.end(), [&](const ScenarioTown& t) { return t.name == name; })) {
                    throw std::runtime_error("goal names town '" + name + "', which the scenario does not place");
                }
            }
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
            if (goal.kind == GoalKind::AverageSpeed && goal.count <= 0) {
                throw std::runtime_error("average_speed needs a count in mph");
            }
            if (goal.kind == GoalKind::Variable && goal.name.empty()) throw std::runtime_error("variable needs a name");
            return goal;
        };
        const auto& medals = j.at("medals");
        const char* keys[] = {"bronze", "silver", "gold"};
        for (std::size_t m = 0; m < 3; ++m) {
            const auto& tier = medals.at(keys[m]);
            s.medals[m].deadline = parse_date(tier.at("by").get<std::string>());
            for (const auto& g : tier.at("goals")) {
                Goal goal = parse_goal(g);
                s.medals[m].goals.push_back(std::move(goal));
            }
        }
        if (j.contains("restrictions")) {
            const auto& r = j.at("restrictions");
            Restrictions& out = s.restrictions;
            if (r.contains("track_budget")) {
                const auto& tb = r.at("track_budget");
                out.track_tiles = tb.at("initial").get<std::int64_t>();
                out.tiles_per_year = tb.value("per_year", std::int64_t{0});
                out.tile_refund = tb.value("refund", false);
                if (*out.track_tiles < 0 || out.tiles_per_year < 0) {
                    throw std::runtime_error("track_budget cannot be negative");
                }
            }
            out.no_unconnected_track = r.value("no_unconnected_track", false);
            out.one_company_only = r.value("one_company_only", false);
            for (const auto& what : r.value("no_purchase", std::vector<std::string>{})) {
                if (what == "track") out.no_track = true;
                else if (what == "stations") out.no_stations = true;
                else if (what == "trains") out.no_trains = true;
                else if (what == "industries") out.no_industries = true;
                else throw std::runtime_error("no_purchase: unknown '" + what + "' (track, stations, trains, industries)");
            }
        }
        for (const auto& ev : j.value("events", nlohmann::json::array())) {
            ScenarioEvent event;
            event.id = ev.at("id").get<std::string>();
            for (const ScenarioEvent& other : s.events)
                if (other.id == event.id) throw std::runtime_error("two events are called '" + event.id + "'");
            event.repeat_months = ev.value("repeat_months", 0);
            const auto& when = ev.value("when", nlohmann::json::object());
            if (when.contains("after")) event.when.after = parse_date(when.at("after").get<std::string>());
            if (when.contains("before")) event.when.before = parse_date(when.at("before").get<std::string>());
            for (const auto& g : when.value("goals", nlohmann::json::array())) event.when.goals.push_back(parse_goal(g));
            if (when.contains("variable")) {
                event.when.variable = when.at("variable").at("name").get<std::string>();
                event.when.at_least = when.at("variable").value("at_least", std::int64_t{1});
            }
            event.when.chance_percent = when.value("chance_percent", 100);
            if (event.when.chance_percent < 0 || event.when.chance_percent > 100) {
                throw std::runtime_error("event '" + event.id + "': chance_percent must be 0 to 100");
            }
            // Effects, and a Choice's options' effects (no choices inside choices).
            std::function<EventEffect(const nlohmann::json&, bool)> parse_effect_json = [&](const nlohmann::json& ef,
                                                                                            bool in_choice) {
                EventEffect effect;
                effect.kind = parse_effect(ef.at("kind").get<std::string>());
                effect.target = ef.value("target", std::string());
                effect.text = ef.value("text", std::string());
                effect.percent = ef.value("percent", 100);
                effect.months = ef.value("months", effect.kind == EffectKind::Choice ? 1 : 0);
                effect.amount = Money::dollars(ef.value("amount", std::int64_t{0}));
                // Defaults: an earthquake reaches 3 cells, sabotage hits one train.
                const std::int64_t default_value =
                    effect.kind == EffectKind::Earthquake ? 3 : effect.kind == EffectKind::Sabotage ? 1 : 0;
                effect.value = ef.value("value", default_value);
                const bool needs_target = effect.kind != EffectKind::News && effect.kind != EffectKind::CompanyCash &&
                                          effect.kind != EffectKind::PlayerCash && effect.kind != EffectKind::BuildCost &&
                                          effect.kind != EffectKind::Briefing && effect.kind != EffectKind::Choice &&
                                          effect.kind != EffectKind::Sabotage;
                if (needs_target && effect.target.empty()) {
                    throw std::runtime_error("event '" + event.id + "': this effect needs a target");
                }
                if ((effect.kind == EffectKind::News || effect.kind == EffectKind::Briefing ||
                     effect.kind == EffectKind::Choice) &&
                    effect.text.empty()) {
                    throw std::runtime_error("event '" + event.id + "': this effect needs text");
                }
                if (effect.percent < 0 || effect.months < 0 || effect.value < 0) {
                    throw std::runtime_error("event '" + event.id + "': percent, months and value cannot be negative");
                }
                if (effect.kind == EffectKind::Choice) {
                    if (in_choice) throw std::runtime_error("event '" + event.id + "': a choice cannot offer another choice");
                    if (effect.months < 1) throw std::runtime_error("event '" + event.id + "': a choice needs at least a month to answer");
                    for (const auto& o : ef.at("options")) {
                        ChoiceOption option{o.at("label").get<std::string>(), {}};
                        for (const auto& oe : o.value("effects", nlohmann::json::array())) {
                            option.effects.push_back(parse_effect_json(oe, true));
                        }
                        effect.options.push_back(std::move(option));
                    }
                    if (effect.options.empty() || effect.options.size() > 9) {
                        throw std::runtime_error("event '" + event.id + "': a choice needs 1 to 9 options");
                    }
                }
                if (effect.kind == EffectKind::Earthquake && (effect.value < 1 || effect.percent > 100)) {
                    throw std::runtime_error("event '" + event.id + "': an earthquake needs a radius of 1 cell or more and percent up to 100");
                }
                return effect;
            };
            for (const auto& ef : ev.at("effects")) event.effects.push_back(parse_effect_json(ef, false));
            if (event.effects.empty()) throw std::runtime_error("event '" + event.id + "' does nothing");
            s.events.push_back(std::move(event));
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
        const bool placed = !g.from.empty() || !g.to.empty();
        std::int64_t loads = 0;
        if (co && cargo) loads = (placed ? w.place_delivered_milli(*co, g) : w.company().delivered_milli(*cargo)) / kMilli;
        std::string what = "Deliver " + g.cargo;
        if (!g.from.empty()) what += " from " + g.from;
        if (!g.to.empty()) what += " to " + g.to;
        return {loads >= g.count, what + ": " + std::to_string(loads) + " of " + std::to_string(g.count) + " loads"};
    }
    case GoalKind::OnlyRailroad: {
        std::int32_t others = 0;
        for (const Company& c : w.companies())
            if (!c.defunct() && (!co || c.id() != *co)) ++others;
        return {co.has_value() && others == 0, "Rival railroads still running: " + std::to_string(others)};
    }
    case GoalKind::AverageSpeed: {
        const std::int64_t mph = co ? w.average_speed_mph(*co) : 0;
        return {mph >= g.count, "Average train speed " + std::to_string(mph) + " of " + std::to_string(g.count) + " mph"};
    }
    case GoalKind::Variable: {
        const std::int64_t have = w.variable(g.name);
        return {have >= g.count, g.name + " " + std::to_string(have) + " of " + std::to_string(g.count)};
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
