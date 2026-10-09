#include "railmaster/sim/money.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

TEST_CASE("money arithmetic is exact") {
    const Money a = Money::dollars(1000);
    const Money b = Money::cents(1);
    CHECK((a + b).in_cents() == 100001);
    CHECK((a - b).in_cents() == 99999);
    CHECK((a * 3).whole_dollars() == 3000);
    CHECK(a.scaled(3, 4) == Money::dollars(750));
    CHECK(-a < b);
}
