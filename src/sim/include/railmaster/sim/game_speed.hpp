#pragma once

#include "railmaster/sim/balance.hpp"

#include <cstdint>

namespace railmaster::sim {

// The six game speeds (rt3-clone-spec §2 [D]). Every game starts Paused.
// `+` and `-` step through them; Pause stops and resumes.
enum class GameSpeed : std::uint8_t { Paused, VerySlow, Slow, Normal, Fast, VeryFast };

const char* game_speed_name(GameSpeed s);
GameSpeed faster(GameSpeed s); // VeryFast stays VeryFast
GameSpeed slower(GameSpeed s); // down to Paused

// Pause and resume: tracks the speed to return to.
class SpeedControl {
public:
    GameSpeed speed() const { return speed_; }
    void faster() { set(sim::faster(speed_)); }
    void slower() { set(sim::slower(speed_)); }
    void toggle_pause();

private:
    void set(GameSpeed s);
    GameSpeed speed_ = GameSpeed::Paused;
    GameSpeed resume_ = GameSpeed::Normal;
};

// Game days per real second, in thousandths: a year in
// Balance::Time::normal_seconds_per_year at Normal [I], times or divided by
// speed_step_percent per step. Zero when paused.
std::int64_t days_per_second_milli(GameSpeed s, const Balance::Time& t);

} // namespace railmaster::sim
