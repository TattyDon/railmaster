#include "railmaster/sim/random.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

TEST_CASE("PCG32 matches the reference output") {
    // First outputs of the PCG reference demo (pcg32-demo, seed 42, stream 54).
    Random r(42u, 54u);
    CHECK(r.next_u32() == 0xa15c02b7u);
    CHECK(r.next_u32() == 0x7b47f409u);
    CHECK(r.next_u32() == 0xba1d3330u);
}

TEST_CASE("same seed gives same sequence") {
    Random a(7), b(7);
    for (int i = 0; i < 1000; ++i) CHECK(a.next_u32() == b.next_u32());
}

TEST_CASE("bounded draws stay in range") {
    Random r(1);
    for (int i = 0; i < 10000; ++i) {
        const auto v = r.between(-5, 5);
        CHECK(v >= -5);
        CHECK(v <= 5);
    }
}
