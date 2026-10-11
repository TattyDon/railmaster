// Scenario events (rt3-clone-spec §14.4 [C]): checked at each month end, in
// file order. An event fires when its date window is open, its goals are
// met, its counter is high enough, and then its chance comes up. Effects
// that last a while (prices, demand, output, building costs) are kept as
// modifiers and applied to a clean copy of the game data, so they can lapse
// and so a loaded game rebuilds them the same way.

#include "railmaster/sim/world.hpp"

#include <stdexcept>

namespace railmaster::sim {

void World::check_event_targets() const {
    for (const ScenarioEvent& e : scenario_->events) {
        for (const EventEffect& f : e.effects) {
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
            default: break;
            }
        }
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
    const std::int32_t now = month_index();
    bool remodel = false;
    for (const EventEffect& f : e.effects) {
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
        }
    }
    if (remodel) apply_modifiers();
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
