#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/fixed_math.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

CargoRegistry coal_only() {
    return CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})");
}

// The shipped rates: a mine makes 2.2 carloads a year, a plant wants 4.4.
IndustryRegistry coal_industries(const CargoRegistry& cargo) {
    return IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 2.2},
        {"key": "electric_plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 4.4}
    ]})",
                                       cargo);
}

} // namespace

TEST_CASE("integer powers for the equilibrium price are close to the real thing") {
    CHECK(log2_permille(8, 1, 30) == 3000);
    CHECK(log2_permille(1, 4, 30) == -2000);
    CHECK(exp2_permille(1000) == 2000);
    CHECK(exp2_permille(-1000) == 500);
    CHECK(exp2_permille(500) == 1414);
    CHECK(pow_ratio_permille(4, 1, 500) == 2000);    // sqrt(4)
    CHECK(pow_ratio_permille(1, 4, 500) == 500);     // sqrt(1/4)
    CHECK(pow_ratio_permille(9, 4, 500) >= 1497);    // sqrt(2.25), within 0.2%
    CHECK(pow_ratio_permille(9, 4, 500) <= 1500);
    CHECK(pow_ratio_permille(5, 5, 500) == 1000);
}

TEST_CASE("the equilibrium price: base x ((D + e) / (S + e)) ^ 0.5, within 30% and 300% of base [I]") {
    const CargoRegistry cargo = coal_only();
    const Economy eco(10, 10, kKm, cargo);
    const CargoType& coal = cargo.get(0);
    CHECK(eco.equilibrium_price(coal, 0, 0) == 30'000);         // nothing here: base
    CHECK(eco.equilibrium_price(coal, 3'000, 0) == 60'000);     // wants 3 loads, has none: x sqrt(4)
    CHECK(eco.equilibrium_price(coal, 0, 3'000) == 15'000);     // makes 3 loads: x sqrt(1/4)
    CHECK(eco.equilibrium_price(coal, 3'000, 3'000) == 30'000); // balanced
    CHECK(eco.equilibrium_price(coal, 1'000'000, 0) == 90'000); // the ceiling
    CHECK(eco.equilibrium_price(coal, 0, 1'000'000) == 9'000);  // the floor
}

TEST_CASE("a new consumer's price rises over months, not at once [I]") {
    const CargoRegistry cargo = coal_only();
    const IndustryRegistry ind = coal_industries(cargo);
    Economy eco(20, 20, kKm, cargo);
    eco.add_site(ind, *ind.find("electric_plant"), 10, 10);
    const std::int32_t target = eco.equilibrium_price(cargo.get(0), 4'400 * 730 / 365, 0);
    const auto gained = [&] { return (eco.price(0, 10, 10) - 30'000) * 100 / (target - 30'000); };
    for (int d = 0; d < 30; ++d) eco.step_day(cargo, ind, 1850);
    CHECK(gained() > 10); // about 15% of the way after a month (1 - e^(-30/180))
    CHECK(gained() < 25);
    for (int d = 30; d < 180; ++d) eco.step_day(cargo, ind, 1850);
    CHECK(gained() > 55); // about 63% after six months
    CHECK(gained() < 70);
}

TEST_CASE("hauling to one buyer again and again narrows the price gap [C]") {
    const CargoRegistry cargo = coal_only();
    const IndustryRegistry ind = coal_industries(cargo);
    Economy eco(20, 20, kKm, cargo);
    eco.add_site(ind, *ind.find("electric_plant"), 10, 10);
    for (int d = 0; d < 365; ++d) eco.step_day(cargo, ind, 1850);
    const std::int32_t starved = eco.price(0, 10, 10);
    // A carload a week, more than the plant burns.
    for (int d = 0; d < 365; ++d) {
        if (d % 7 == 0) eco.add_stock(0, 10, 10, 1'000);
        eco.step_day(cargo, ind, 1850);
    }
    const std::int32_t served = eco.price(0, 10, 10);
    MESSAGE("plant's coal price: starved $" << starved << ", after a year of deliveries $" << served);
    CHECK(served < starved * 9 / 10);
    CHECK(served > 30'000 * 30 / 100);
}

TEST_CASE("far from any site, a cargo sits at its base price; stock lying there lowers it") {
    const CargoRegistry cargo = coal_only();
    const IndustryRegistry ind = coal_industries(cargo);
    Economy eco(30, 30, kKm, cargo);
    eco.add_site(ind, *ind.find("electric_plant"), 2, 2); // keeps coal in play
    eco.add_stock(0, 25, 25, 20'000);
    for (int d = 0; d < 365; ++d) eco.step_day(cargo, ind, 1850);
    CHECK(eco.price(0, 25, 5) > 29'000);
    CHECK(eco.price(0, 25, 25) < eco.price(0, 25, 5));
}
