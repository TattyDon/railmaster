#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

TEST_CASE("grade is rise over run in basis points") {
    Terrain t(4, 4, 100);
    t.set_corner_height(1, 0, 2); // 2 m rise over 100 m = 2.00%
    CHECK(t.grade_bp(0, 0, 1, 0) == 200);
    CHECK(t.grade_bp(1, 0, 0, 0) == -200);
}

TEST_CASE("diagonal grade uses true horizontal distance") {
    Terrain t(4, 4, 100);
    t.set_corner_height(1, 1, 3); // run = 141 m, 3/141 = 2.12%
    CHECK(t.grade_bp(0, 0, 1, 1) == 212);
}

TEST_CASE("out-of-range access throws") {
    Terrain t(2, 2, 100);
    CHECK_NOTHROW(t.corner_height(2, 2));
    CHECK_THROWS(t.corner_height(3, 0));
    CHECK_THROWS(t.ground(2, 0));
}

TEST_CASE("terrain generation is deterministic") {
    Terrain a(32, 32, 100), b(32, 32, 100);
    Random ra(99), rb(99);
    a.generate_rolling_hills(ra, 300);
    b.generate_rolling_hills(rb, 300);
    for (int y = 0; y <= 32; ++y)
        for (int x = 0; x <= 32; ++x) CHECK(a.corner_height(x, y) == b.corner_height(x, y));
}
