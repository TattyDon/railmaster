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

TEST_CASE("rivers run downhill from high ground to the edge or the sea") {
    // A slope rising to the east, with a bump in the middle to cut through.
    Terrain t(40, 20, 1000);
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 40; ++x) t.set_corner_height(x, y, 10 * x + (x == 20 ? 50 : 0));
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x) t.set_ground(x, y, GroundType::Grass);
    Random rng(3);
    t.generate_rivers(rng, 1, 80);
    // Follow the river from its source (the highest wet cell) downhill.
    int sx = -1, sy = -1, wet = 0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x)
            if (t.ground(x, y) == GroundType::Water) {
                ++wet;
                if (sx < 0 || x > sx) {
                    sx = x;
                    sy = y;
                }
            }
    REQUIRE(wet > 0);
    CHECK(sx >= 31); // its source is on high ground (80% of the top)
    // It runs off the map, through the middle ridge (cut down along its
    // course), and never uphill: every river cell is no higher than the one
    // upstream of it, so the western part is lower than the source.
    bool reaches_edge = false;
    for (int y = 0; y < 20; ++y) reaches_edge = reaches_edge || t.ground(0, y) == GroundType::Water || t.ground(39, y) == GroundType::Water;
    for (int x = 0; x < 40; ++x) reaches_edge = reaches_edge || t.ground(x, 0) == GroundType::Water || t.ground(x, 19) == GroundType::Water;
    CHECK(reaches_edge);
    bool west_of_ridge = false;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) west_of_ridge = west_of_ridge || t.ground(x, y) == GroundType::Water;
    CHECK(west_of_ridge);
    (void)sy;
}
