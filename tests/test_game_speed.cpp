#include "railmaster/sim/game_speed.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace railmaster::sim;

TEST_CASE("six speeds: + and - step through them, Pause stops and resumes [D]") {
    SpeedControl s;
    CHECK(s.speed() == GameSpeed::Paused); // every game starts paused
    s.toggle_pause();
    CHECK(s.speed() == GameSpeed::Normal);
    s.faster();
    s.faster();
    CHECK(s.speed() == GameSpeed::VeryFast);
    s.faster();
    CHECK(s.speed() == GameSpeed::VeryFast); // the top
    s.toggle_pause();
    CHECK(s.speed() == GameSpeed::Paused);
    s.toggle_pause();
    CHECK(s.speed() == GameSpeed::VeryFast); // back where it was
    for (int i = 0; i < 6; ++i) s.slower();
    CHECK(s.speed() == GameSpeed::Paused); // - from Very Slow pauses
    s.faster();
    CHECK(s.speed() == GameSpeed::VerySlow);
    CHECK(std::string(game_speed_name(GameSpeed::VeryFast)) == "Very Fast");
}

TEST_CASE("about four minutes a game year at Normal, doubling or halving per step [I]") {
    const Balance::Time t;
    CHECK(days_per_second_milli(GameSpeed::Paused, t) == 0);
    const std::int64_t normal = days_per_second_milli(GameSpeed::Normal, t);
    CHECK(normal == 365'000 / 240);
    CHECK(days_per_second_milli(GameSpeed::Fast, t) == normal * 2);
    CHECK(days_per_second_milli(GameSpeed::VeryFast, t) == normal * 4);
    CHECK(days_per_second_milli(GameSpeed::Slow, t) == normal / 2);
    CHECK(days_per_second_milli(GameSpeed::VerySlow, t) == normal / 4);
}
