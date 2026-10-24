#include "railmaster/sim/economic_state.hpp"

#include "railmaster/sim/random.hpp"

namespace railmaster::sim {

const char* economic_state_name(EconomicState s) {
    static constexpr const char* kNames[] = {"Depression", "Recession", "Normal", "Prosperity", "Boom"};
    return kNames[index_of(s)];
}

EconomicState next_economic_state(EconomicState s, Random& rng, const Balance::EconomicStates& b) {
    const auto roll = static_cast<std::int32_t>(rng.below(100));
    if (roll < b.stay_percent) return s;
    const int now = static_cast<int>(s);
    constexpr int kNormal = static_cast<int>(EconomicState::Normal);
    int next;
    if (now == kNormal) {
        // Split what is left evenly between a step up and a step down.
        next = roll < b.stay_percent + (100 - b.stay_percent) / 2 ? now + 1 : now - 1;
    } else {
        const int toward = now < kNormal ? 1 : -1;
        next = roll < b.stay_percent + b.toward_normal_percent ? now + toward : now - toward;
    }
    if (next < 0 || next > static_cast<int>(EconomicState::Boom)) return s;
    return static_cast<EconomicState>(next);
}

} // namespace railmaster::sim
