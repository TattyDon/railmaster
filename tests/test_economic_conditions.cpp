#include "railmaster/sim/economic_state.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/stock.hpp"
#include "railmaster/sim/terrain.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;
constexpr std::int32_t kLen = 40;

CargoRegistry coal_only() {
    return CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})");
}

IndustryRegistry coal_industries(const CargoRegistry& cargo) {
    return IndustryRegistry::from_json(R"({"industries": [
        {"key": "coal_mine", "name": "Coal Mine", "kind": "raw", "outputs": ["coal"], "rate": 365},
        {"key": "electric_plant", "name": "Electric Plant", "kind": "sink", "inputs": [{"cargo": "coal"}], "rate": 365}
    ]})",
                                       cargo);
}

enum class Ground { Flat, Water, Mountains };

// A corridor kLen cells long and one cell wide, all of one kind of ground.
Terrain corridor(Ground g) {
    Terrain t(kLen, 1, 1000);
    for (std::int32_t x = 0; x <= kLen; ++x) {
        // Mountains: corners alternate 0 m and 100 m, a 10% grade across every cell.
        const std::int32_t h = g == Ground::Mountains && x % 2 == 1 ? 100 : 0;
        t.set_corner_height(x, 0, h);
        t.set_corner_height(x, 1, h);
    }
    for (std::int32_t x = 0; x < kLen; ++x) t.set_ground(x, 0, g == Ground::Water ? GroundType::Water : GroundType::Grass);
    return t;
}

struct Corridor {
    CargoRegistry cargo = coal_only();
    IndustryRegistry ind = coal_industries(cargo);
    Economy eco{kLen, 1, kKm, cargo};

    explicit Corridor(std::optional<Ground> g) {
        if (g) eco.set_terrain(corridor(*g));
    }
    void days(int n) {
        for (int i = 0; i < n; ++i) eco.step_day(cargo, ind, 1850);
    }
};

// Price `at` cells from a lone consumer at the west end, once settled.
std::int32_t price_away_from_consumer(Ground g, std::int32_t at) {
    Corridor c(g);
    c.eco.add_site(c.ind, *c.ind.find("electric_plant"), 0, 0);
    c.days(3000);
    return c.eco.price(0, at, 0);
}

// Carloads that have drifted at least `from` cells east of a mine at the
// west end, towards a consumer 12 cells away.
std::int32_t stock_beyond(Ground g, std::int32_t from, int days) {
    constexpr std::int32_t kConsumer = 12;
    Corridor c(g);
    c.eco.add_site(c.ind, *c.ind.find("coal_mine"), 0, 0);
    c.eco.add_site(c.ind, *c.ind.find("electric_plant"), kConsumer, 0);
    c.days(days);
    std::int32_t total = 0;
    for (std::int32_t x = from; x < kConsumer; ++x) total += c.eco.stock_milli(0, x, 0);
    return total;
}

} // namespace

TEST_CASE("cells take their conductance from the terrain") {
    const Balance::Economy& b = default_balance().economy;
    Terrain t(4, 1, 1000);
    for (std::int32_t y = 0; y <= 1; ++y) {
        t.set_corner_height(0, y, 0);
        t.set_corner_height(1, y, 0);
        t.set_corner_height(2, y, 0);
        t.set_corner_height(3, y, 50);  // 5% across cell 2: hills
        t.set_corner_height(4, y, 150); // 10% across cell 3: mountains
    }
    for (std::int32_t x = 0; x < 4; ++x) t.set_ground(x, 0, GroundType::Grass);
    t.set_ground(0, 0, GroundType::Water);

    const CargoRegistry cargo = coal_only();
    Economy eco(4, 1, kKm, cargo);
    CHECK(eco.conductance(0, 0) == 1000); // flat until told about the terrain
    eco.set_terrain(t);
    CHECK(eco.conductance(0, 0) == b.water_conductance_permille);
    CHECK(eco.conductance(1, 0) == b.coast_conductance_permille);
    CHECK(eco.conductance(2, 0) == b.hill_conductance_permille);
    CHECK(eco.conductance(3, 0) == b.mountain_conductance_permille);

    Terrain wrong(5, 1, 1000);
    CHECK_THROWS(eco.set_terrain(wrong));
}

TEST_CASE("flat terrain leaves the economy exactly as before") {
    const auto run = [](bool with_terrain) {
        Corridor c(with_terrain ? std::optional<Ground>{Ground::Flat} : std::nullopt);
        c.eco.add_site(c.ind, *c.ind.find("coal_mine"), 3, 0);
        c.eco.add_site(c.ind, *c.ind.find("electric_plant"), 30, 0);
        c.days(200);
        std::array<std::int32_t, 2 * kLen> out{};
        for (std::int32_t x = 0; x < kLen; ++x) {
            out[static_cast<std::size_t>(x)] = c.eco.price(0, x, 0);
            out[static_cast<std::size_t>(kLen + x)] = c.eco.stock_milli(0, x, 0);
        }
        return out;
    };
    CHECK(run(true) == run(false));
}

TEST_CASE("a consumer's pull reaches further along water and less far over mountains [D]") {
    const std::int32_t water = price_away_from_consumer(Ground::Water, 12);
    const std::int32_t flat = price_away_from_consumer(Ground::Flat, 12);
    const std::int32_t mountains = price_away_from_consumer(Ground::Mountains, 12);
    CHECK(water > flat);
    CHECK(flat > mountains);
}

TEST_CASE("middlemen move freight faster on water and slower over mountains [D]") {
    const std::int32_t water = stock_beyond(Ground::Water, 4, 60);
    const std::int32_t flat = stock_beyond(Ground::Flat, 4, 60);
    const std::int32_t mountains = stock_beyond(Ground::Mountains, 4, 60);
    MESSAGE("carloads past cell 4: water ", water, ", flat ", flat, ", mountains ", mountains);
    CHECK(water > flat);
    CHECK(flat > mountains);
}

TEST_CASE("the economic state moves a step at a time and stays in range") {
    const Balance::EconomicStates& b = default_balance().economic_states;
    Random rng(5);
    std::array<int, 5> seen{};
    EconomicState s = EconomicState::Normal;
    int moves = 0;
    for (int i = 0; i < 20000; ++i) {
        const EconomicState next = next_economic_state(s, rng, b);
        const int step = static_cast<int>(next) - static_cast<int>(s);
        CHECK(step >= -1);
        CHECK(step <= 1);
        if (step != 0) ++moves;
        s = next;
        ++seen[index_of(s)];
    }
    for (int n : seen) CHECK(n > 0); // every state happens
    // Pulled toward Normal: it is the most common state, the extremes the rarest.
    CHECK(seen[index_of(EconomicState::Normal)] > seen[index_of(EconomicState::Prosperity)]);
    CHECK(seen[index_of(EconomicState::Prosperity)] > seen[index_of(EconomicState::Boom)]);
    CHECK(seen[index_of(EconomicState::Recession)] > seen[index_of(EconomicState::Depression)]);
    CHECK(moves > 6000); // about half the checks bring a change
    CHECK(moves < 11000);
}

TEST_CASE("at the ends, the economy can only stay or recover") {
    const Balance::EconomicStates& b = default_balance().economic_states;
    Random rng(9);
    for (int i = 0; i < 1000; ++i) {
        const EconomicState up = next_economic_state(EconomicState::Boom, rng, b);
        CHECK((up == EconomicState::Boom || up == EconomicState::Prosperity));
        const EconomicState down = next_economic_state(EconomicState::Depression, rng, b);
        CHECK((down == EconomicState::Depression || down == EconomicState::Recession));
    }
}

namespace {

World plain_world(bool cycle = true) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.business_cycle = cycle;
    World w(cfg);
    for (std::int32_t y = 0; y <= 20; ++y)
        for (std::int32_t x = 0; x <= 20; ++x) w.terrain().set_corner_height(x, y, 10);
    for (std::int32_t y = 0; y < 20; ++y)
        for (std::int32_t x = 0; x < 20; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

TrackEnd free_at(std::int64_t x_km, std::int64_t y_km) { return {TrackEnd::Kind::Free, 0, 0, {x_km * kKm, y_km * kKm}}; }

} // namespace

TEST_CASE("the economic state sets costs, the prime rate, output and share prices [C]") {
    World w = plain_world(false);
    const BuildTrack track{.start = free_at(2, 2), .end = free_at(8, 2)};
    const auto normal_plan = w.preview(track);
    REQUIRE(normal_plan.plan);
    const Money normal_cost = normal_plan.plan->total_cost;
    const std::int32_t normal_rate = w.company().bond_rate_bp();
    const Money normal_target = target_share_price(w.company());
    CHECK(w.economic_state() == EconomicState::Normal);
    CHECK(w.company().prime_rate_bp() == 600);

    w.set_economic_state(EconomicState::Boom);
    CHECK(w.take_economy_news() == EconomicState::Boom);
    CHECK_FALSE(w.take_economy_news()); // news is told once
    CHECK(w.cost_percent() == 115);
    CHECK(w.preview(track).plan->total_cost == normal_cost.scaled(115, 100));
    CHECK(w.company().prime_rate_bp() == 400);
    CHECK(w.company().bond_rate_bp() == normal_rate - 200);
    CHECK(w.economy().activity_percent() == 125);
    CHECK(target_share_price(w.company()) > normal_target);

    // Building pays the boom-time price.
    const Money before = w.company().cash();
    const CommandResult built = w.execute(track);
    REQUIRE(built.ok);
    CHECK(before - w.company().cash() == normal_cost.scaled(115, 100));

    w.set_economic_state(EconomicState::Depression);
    CHECK(w.company().bond_rate_bp() == normal_rate + 200);
    CHECK(w.economy().activity_percent() == 75);
    CHECK(target_share_price(w.company()) < normal_target);
}

TEST_CASE("the business cycle is checked twice a year, and can be switched off") {
    World fixed = plain_world(false);
    run_days(fixed, 365 * 6);
    CHECK(fixed.economic_state() == EconomicState::Normal);

    World w = plain_world(true);
    int changes = 0;
    for (int d = 0; d < 365 * 12; ++d) {
        run_days(w, 1);
        if (w.take_economy_news()) {
            ++changes;
            const CalendarDate today = w.date().calendar();
            CHECK(today.day == 1);
            CHECK((today.month == 1 || today.month == 7));
        }
    }
    CHECK(changes > 0); // 24 checks, each about even odds of a change
}

TEST_CASE("terrain edits after the world is made reach the economy") {
    World w = plain_world(false);
    w.terrain().set_ground(5, 5, GroundType::Water);
    run_days(w, 1);
    CHECK(w.economy().conductance(5, 5) == default_balance().economy.water_conductance_permille);
    CHECK(w.economy().conductance(5, 6) == default_balance().economy.coast_conductance_permille);
}
