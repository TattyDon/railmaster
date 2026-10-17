#include "legacy_scale.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

TEST_CASE("world advances one day per kTicksPerDay ticks") {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = cfg.height_tiles = 16;
    World w(cfg);
    const Date start = w.date();
    for (int i = 0; i < World::kTicksPerDay - 1; ++i) w.tick();
    CHECK(w.date() == start);
    w.tick();
    CHECK(w.date() == start + 1);
    CHECK(w.tick_of_day() == 0);
}

TEST_CASE("a year of ticks reaches the next new year") {
    WorldConfig cfg;
    testing::legacy_scale(cfg);
    cfg.width_tiles = cfg.height_tiles = 16;
    World w(cfg);
    for (int i = 0; i < 365 * World::kTicksPerDay; ++i) w.tick();
    CHECK(w.date() == Date::from_ymd(1831, 1, 1));
}
