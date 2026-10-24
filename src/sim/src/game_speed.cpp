#include "railmaster/sim/game_speed.hpp"

#include <algorithm>

namespace railmaster::sim {

const char* game_speed_name(GameSpeed s) {
    switch (s) {
    case GameSpeed::Paused: return "Paused";
    case GameSpeed::VerySlow: return "Very Slow";
    case GameSpeed::Slow: return "Slow";
    case GameSpeed::Normal: return "Normal";
    case GameSpeed::Fast: return "Fast";
    case GameSpeed::VeryFast: return "Very Fast";
    }
    return "?";
}

GameSpeed faster(GameSpeed s) {
    return s == GameSpeed::VeryFast ? s : static_cast<GameSpeed>(static_cast<int>(s) + 1);
}

GameSpeed slower(GameSpeed s) {
    return s == GameSpeed::Paused ? s : static_cast<GameSpeed>(static_cast<int>(s) - 1);
}

void SpeedControl::set(GameSpeed s) {
    speed_ = s;
    if (s != GameSpeed::Paused) resume_ = s;
}

void SpeedControl::toggle_pause() { speed_ = speed_ == GameSpeed::Paused ? resume_ : GameSpeed::Paused; }

std::int64_t days_per_second_milli(GameSpeed s, const Balance::Time& t) {
    if (s == GameSpeed::Paused) return 0;
    std::int64_t milli = 365'000 / std::max(1, t.normal_seconds_per_year);
    const std::int64_t step = std::max(1, t.speed_step_percent);
    for (int i = static_cast<int>(s); i > static_cast<int>(GameSpeed::Normal); --i) milli = milli * step / 100;
    for (int i = static_cast<int>(s); i < static_cast<int>(GameSpeed::Normal); ++i) milli = milli * 100 / step;
    return milli;
}

} // namespace railmaster::sim
