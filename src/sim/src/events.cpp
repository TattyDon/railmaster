// Scenario events (rt3-clone-spec §14.4 [C]): checked at each month end, in
// file order. An event fires when its date window is open, its goals are
// met, its counter is high enough, and then its chance comes up. Effects
// that last a while (prices, demand, output, building costs) are kept as
// modifiers and applied to a clean copy of the game data, so they can lapse
// and so a loaded game rebuilds them the same way.

#include "railmaster/sim/world.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace railmaster::sim {

void World::check_event_targets() const {
    for (const ScenarioEvent& e : scenario_->events) {
        const std::function<void(const std::vector<EventEffect>&)> check = [&](const std::vector<EventEffect>& effects) {
            for (const EventEffect& f : effects) {
                const auto unknown = [&](const char* what) {
                    throw std::invalid_argument("scenario event '" + e.id + "': unknown " + what + " '" + f.target + "'");
                };
                switch (f.kind) {
                case EffectKind::CargoPrice:
                case EffectKind::ExpressDemand:
                    if (!data_.cargo.find(f.target)) unknown("cargo");
                    break;
                case EffectKind::Production:
                    if (!data_.industries.find(f.target)) unknown("industry");
                    break;
                case EffectKind::UnlockEngine:
                    if (!data_.locomotives.find(f.target)) unknown("engine");
                    break;
                case EffectKind::TerritoryPrice: {
                    bool found = false;
                    for (const Territory& t : territories_.territories) found = found || t.name == f.target;
                    if (!found) unknown("territory");
                    break;
                }
                case EffectKind::Earthquake: {
                    const auto& towns = economy_.towns();
                    if (std::none_of(towns.begin(), towns.end(), [&](const Town& t) { return t.name == f.target; })) unknown("town");
                    break;
                }
                case EffectKind::Choice:
                    for (const ChoiceOption& o : f.options) check(o.effects);
                    break;
                default: break;
                }
            }
        };
        check(e.effects);
        for (const Goal& g : e.when.goals)
            if (g.kind == GoalKind::Deliver && !data_.cargo.find(g.cargo)) {
                throw std::invalid_argument("scenario event '" + e.id + "': unknown cargo '" + g.cargo + "'");
            }
    }
}

void World::run_events() {
    if (!scenario_) return;
    const std::int32_t now = month_index();
    // Lapsed effects go first.
    const auto lapsed = std::remove_if(modifiers_.begin(), modifiers_.end(),
                                       [&](const Modifier& m) { return m.until_month > 0 && m.until_month <= now; });
    if (lapsed != modifiers_.end()) {
        modifiers_.erase(lapsed, modifiers_.end());
        apply_modifiers();
    }
    // Questions left unanswered lapse, and nothing comes of them.
    for (auto it = pending_choices_.begin(); it != pending_choices_.end();) {
        if (it->until_month > now) {
            ++it;
            continue;
        }
        news_.push_back("No answer given: " + choice_effect(*it).text);
        it = pending_choices_.erase(it);
    }
    for (std::size_t i = 0; i < scenario_->events.size(); ++i) {
        const ScenarioEvent& e = scenario_->events[i];
        std::int32_t& next = event_month_[i];
        if (next < 0 || now < next) continue;
        if (e.when.after && date_ < *e.when.after) continue;
        if (e.when.before && date_ > *e.when.before) {
            if (e.repeat_months == 0) next = -1; // missed its window
            continue;
        }
        bool met = true;
        for (const Goal& g : e.when.goals) met = met && check_goal(*this, g).met;
        if (e.when.variable) met = met && variable(*e.when.variable) >= e.when.at_least;
        if (!met) continue;
        if (e.when.chance_percent < 100 && !rng_.chance(static_cast<std::uint32_t>(e.when.chance_percent), 100)) continue;
        fire(e);
        next = e.repeat_months > 0 ? now + e.repeat_months : -1;
    }
}

void World::fire(const ScenarioEvent& e) {
    apply_effects(e.effects, static_cast<std::size_t>(&e - scenario_->events.data()), true);
}

void World::apply_effects(const std::vector<EventEffect>& effects, std::size_t event, bool top_level) {
    const std::int32_t now = month_index();
    bool remodel = false;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        const EventEffect& f = effects[i];
        switch (f.kind) {
        case EffectKind::News: news_.push_back(f.text); break;
        case EffectKind::CompanyCash:
            if (player_company()) company().post(Ledger::Grants, f.amount);
            break;
        case EffectKind::PlayerCash: investor().cash += f.amount; break;
        case EffectKind::CargoPrice:
        case EffectKind::ExpressDemand:
        case EffectKind::Production:
        case EffectKind::BuildCost:
        case EffectKind::UnlockEngine:
            modifiers_.push_back(
                {f.kind, f.target, f.percent, f.kind == EffectKind::UnlockEngine || f.months == 0 ? 0 : now + f.months});
            remodel = true;
            break;
        case EffectKind::TerritoryPrice:
            for (Territory& t : territories_.territories)
                if (t.name == f.target) t.access_cost = f.amount;
            break;
        case EffectKind::SetVariable: variables_[f.target] = f.value; break;
        case EffectKind::AddVariable: variables_[f.target] += f.value; break;
        case EffectKind::Briefing: scenario_->briefing = f.text; break;
        case EffectKind::Choice:
            // Parsing keeps choices to an event's own effects.
            if (top_level) {
                pending_choices_.push_back({event, i, now + f.months});
                news_.push_back(f.text);
            }
            break;
        case EffectKind::Earthquake: earthquake(f); break;
        case EffectKind::Sabotage: sabotage(f); break;
        }
    }
    if (remodel) apply_modifiers();
}

// Track near the town is wrecked (rt3-clone-spec §14.4 [C]: earthquake
// damage): each piece within the radius, unless a train is on it, goes with
// the effect's chance, written off with nothing back. Trains find another
// way, or wait for the line to be rebuilt.
void World::earthquake(const EventEffect& f) {
    const auto& towns = economy_.towns();
    const auto town = std::find_if(towns.begin(), towns.end(), [&](const Town& t) { return t.name == f.target; });
    if (town == towns.end()) return;
    const MapPoint centre = economy_.node_centre(town->cx, town->cy);
    const std::int64_t reach = economy_.cells_mm(static_cast<std::int32_t>(f.value));
    const TrackNetwork& net = railway_.track();
    std::vector<EdgeId> wrecked;
    std::vector<Money> lost(market_.companies.size());
    for (const TrackEdge& e : net.edges()) {
        if (e.removed || railway_.train_on(e.id)) continue;
        const MapPoint mid{(net.node(e.a).pos.x_mm + net.node(e.b).pos.x_mm) / 2,
                           (net.node(e.a).pos.y_mm + net.node(e.b).pos.y_mm) / 2};
        if (distance_mm(mid, centre) > reach) continue;
        if (f.percent < 100 && !rng_.chance(static_cast<std::uint32_t>(f.percent), 100)) continue;
        wrecked.push_back(e.id);
        if (e.owner < lost.size()) lost[e.owner] += e.cost;
    }
    if (wrecked.empty()) return;
    railway_.remove_edges(wrecked);
    for (std::size_t c = 0; c < lost.size(); ++c) {
        if (lost[c] > Money{}) company(static_cast<CompanyId>(c)).remove_track(lost[c], Money{});
    }
    news_.push_back("Earthquake near " + f.target + ": " + std::to_string(wrecked.size()) + " pieces of track destroyed");
}

// Saboteurs stop some of the player's trains out on the line, as breakdowns
// with their repair bills (rt3-clone-spec §14.4 [C]).
void World::sabotage(const EventEffect& f) {
    const auto co = player_company();
    if (!co) return;
    std::vector<TrainId> running;
    for (const Train& t : railway_.trains()) {
        if (t.owner == *co && t.state == TrainState::Moving && !t.path.empty()) running.push_back(t.id);
    }
    const Balance::Breakdowns& b = data_.balance.breakdowns;
    std::int64_t hit = 0;
    for (; hit < f.value && !running.empty(); ++hit) {
        const std::size_t pick = rng_.below(static_cast<std::uint32_t>(running.size()));
        railway_.break_down(running[pick], rng_.between(b.breakdown_min_days, b.breakdown_max_days));
        running.erase(running.begin() + static_cast<std::ptrdiff_t>(pick));
    }
    if (hit > 0) news_.push_back("Sabotage! " + std::to_string(hit) + (hit == 1 ? " train" : " trains") + " wrecked on the line");
}

std::int64_t World::average_speed_mph(CompanyId c) const {
    std::int64_t distance = 0, ticks = 0;
    for (const Train& t : railway_.trains()) {
        if (t.owner != c) continue;
        distance += t.distance_mm;
        ticks += t.road_ticks;
    }
    // Against the month-end totals of a year ago, once there are 13 of
    // them; before that, since the start.
    if (c < road_samples_.size() && road_samples_[c].size() >= 13) {
        distance -= road_samples_[c].front().first;
        ticks -= road_samples_[c].front().second;
    }
    if (distance <= 0 || ticks <= 0) return 0;
    return distance / ticks / std::max<std::int64_t>(1, mph_to_mm_per_tick(1, data_.balance));
}

void World::sample_road() {
    road_samples_.resize(market_.companies.size());
    for (std::size_t c = 0; c < road_samples_.size(); ++c) {
        std::int64_t distance = 0, ticks = 0;
        for (const Train& t : railway_.trains()) {
            if (t.owner != c) continue;
            distance += t.distance_mm;
            ticks += t.road_ticks;
        }
        auto& s = road_samples_[c];
        s.emplace_back(distance, ticks);
        if (s.size() > 13) s.erase(s.begin());
    }
}

void World::apply_modifiers() {
    data_.cargo = base_cargo_;
    data_.industries = base_industries_;
    data_.locomotives = base_locos_;
    build_cost_percent_ = 100;
    for (const Modifier& m : modifiers_) {
        switch (m.kind) {
        case EffectKind::CargoPrice: {
            CargoType& c = data_.cargo.get_mut(*data_.cargo.find(m.target));
            c.base_price = c.base_price.scaled(m.percent, 100);
            break;
        }
        case EffectKind::ExpressDemand: {
            CargoType& c = data_.cargo.get_mut(*data_.cargo.find(m.target));
            c.demand_percent = c.demand_percent * m.percent / 100;
            break;
        }
        case EffectKind::Production: {
            IndustryType& t = data_.industries.get_mut(*data_.industries.find(m.target));
            t.rate_milli = t.rate_milli * m.percent / 100;
            break;
        }
        case EffectKind::BuildCost: build_cost_percent_ = build_cost_percent_ * m.percent / 100; break;
        case EffectKind::UnlockEngine: {
            LocomotiveType& l = data_.locomotives.get_mut(*data_.locomotives.find(m.target));
            l.available_from = std::min(l.available_from, date_.year());
            l.scenario_only = false;
            break;
        }
        default: break;
        }
    }
}

} // namespace railmaster::sim
