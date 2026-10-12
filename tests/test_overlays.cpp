#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace railmaster::sim;

namespace {

std::string read_data(const std::string& name) {
    std::ifstream in(std::string(RAILMASTER_DATA_DIR) + "/" + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData shipped() {
    GameData d;
    d.balance = Balance::from_json(read_data("balance.json"));
    d.cargo = CargoRegistry::from_json(read_data("cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_data("locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_data("industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_data("tycoons.json"));
    return d;
}

void run_to(World& w, Date when) {
    while (w.date() < when) w.tick();
}

} // namespace

TEST_CASE("each train's monthly profit adds up to the company's train income less maintenance and fuel (§15.3)") {
    WorldConfig cfg;
    cfg.seed = 1;
    World w(cfg, shipped());
    (void)build_demo_network(w);
    run_to(w, Date::from_ymd(1831, 1, 1));

    Money profits, revenue;
    for (const Train& t : w.railway().trains()) {
        CHECK(t.profit_months.size() == 12);
        profits += t.profit_last_year();
        revenue += t.revenue;
    }
    const YearAccounts& y = w.company().history().front();
    REQUIRE(y.year == 1830);
    const auto line = [&](Ledger l) { return y.lines[static_cast<std::size_t>(l)]; };
    const Money income = line(Ledger::FreightRevenue) + line(Ledger::PassengerRevenue) + line(Ledger::MailRevenue) +
                         line(Ledger::TroopRevenue);
    const Money costs = line(Ledger::TrainMaintenance) + line(Ledger::Fuel);
    MESSAGE("trains earned " << income.whole_dollars() << ", cost " << costs.whole_dollars() << ", profit "
                             << profits.whole_dollars());
    CHECK(revenue == income);
    // Costs are scaled train by train rather than in total: allow cents.
    CHECK(std::abs((profits - (income - costs)).in_cents()) <= 2 * 12 * 100);

    // Stations: what trains earned arriving there.
    Money stations;
    for (const Station& s : w.railway().stations()) {
        CHECK(s.income_months.size() == 12);
        stations += s.income_last_year() + s.income_this_month;
    }
    CHECK(stations == revenue);
}

TEST_CASE("traffic is counted on the track trains use, and trains held at meets on single track flag it") {
    WorldConfig cfg;
    cfg.seed = 1;
    World w(cfg, shipped());
    (void)build_demo_network(w); // two trains round a single-track triangle, opposite ways
    const Railway& rw = w.railway();
    std::uint32_t used = 0, held = 0;
    for (std::int32_t month = 2; month <= 13; ++month) {
        run_to(w, month <= 12 ? Date::from_ymd(1830, month, 1) : Date::from_ymd(1831, 1, 1));
        for (const TrackEdge& e : rw.track().edges()) {
            if (e.removed) continue;
            used += rw.traffic_last_month(e.id) > 0;
            held += rw.held_last_month(e.id);
            if (rw.needs_double_track(e.id)) {
                CHECK(rw.held_last_month(e.id) >= static_cast<std::uint32_t>(w.data().balance.overlays.held_days_for_double *
                                                                              World::kTicksPerDay));
            }
        }
    }
    MESSAGE(used << " piece-months used in 1830, trains held " << held << " ticks at meets");
    CHECK(used > 0);
    CHECK(held > 0);
}

TEST_CASE("ratings: red at a loss, green at the good return for the months on record, yellow between (§15.3 [I])") {
    const Balance& b = default_balance(); // good: 10% of the price a year
    const Money price = Money::dollars(120'000);
    CHECK(rate(Money::dollars(5'000), 0, price, b) == Rating::Low); // nothing on record yet
    CHECK(rate(Money::dollars(-1), 6, price, b) == Rating::Loss);
    CHECK(rate(Money::dollars(5'999), 6, price, b) == Rating::Low);
    CHECK(rate(Money::dollars(6'000), 6, price, b) == Rating::Good); // half a year: 5%
    CHECK(rate(Money::dollars(12'000), 12, price, b) == Rating::Good);
}
