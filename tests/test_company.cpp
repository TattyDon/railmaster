#include "railmaster/sim/company.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

constexpr std::int64_t kKm = 1'000'000;

GameData test_data() {
    GameData d;
    d.cargo = CargoRegistry::from_json(R"({"cargo": [{"key": "coal", "name": "Coal", "base_price": 30}]})");
    d.locomotives = LocomotiveRegistry::from_json(R"({"locomotives": [
        {"key": "l", "name": "L", "fuel": "steam", "available_from": 1800,
         "top_speed_mph": 60, "cost": 40000, "maintenance_per_year": 12000}]})");
    return d;
}

World flat_world(std::int64_t cash, bool sandbox = false) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 40;
    cfg.populate = false;
    cfg.starting_cash = cash;
    cfg.sandbox = sandbox;
    World w(cfg, test_data());
    for (int y = 0; y <= 40; ++y)
        for (int x = 0; x <= 40; ++x) w.terrain().set_corner_height(x, y, 10);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) w.terrain().set_ground(x, y, GroundType::Grass);
    return w;
}

TrackEnd free_at(std::int64_t x_km, std::int64_t y_km) { return {TrackEnd::Kind::Free, 0, 0, {x_km * kKm, y_km * kKm}}; }

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

} // namespace

TEST_CASE("revenue and expenses move cash and land on their ledger lines") {
    Company c("Test", Money::dollars(1'000'000), 1850);
    c.post(Ledger::FreightRevenue, Money::dollars(300'000));
    c.post(Ledger::Fuel, Money::dollars(50'000));
    CHECK(c.cash() == Money::dollars(1'250'000));
    CHECK(c.this_year().revenue() == Money::dollars(300'000));
    CHECK(c.this_year().expenses() == Money::dollars(50'000));
    CHECK(c.this_year().profit() == Money::dollars(250'000));
}

TEST_CASE("investment turns cash into assets; book value is assets less debt") {
    Company c("Test", Money::dollars(1'000'000), 1850);
    c.invest_track(Money::dollars(300'000));
    c.invest_train(Money::dollars(100'000));
    CHECK(c.cash() == Money::dollars(600'000));
    CHECK(c.total_assets() == Money::dollars(1'000'000));
    CHECK(c.book_value() == Money::dollars(1'000'000));
    CHECK(c.this_year().track_built == Money::dollars(300'000));
}

TEST_CASE("a new company has a marginal rating and room for two bonds") {
    Company c("Test", Money::dollars(5'000'000), 1850);
    // No debt (+5,000) but no profit record yet: A-.
    CHECK(c.credit_score() == 5000);
    CHECK(c.credit_rating() == CreditRating::AMinus);
    CHECK(c.bond_rate_bp() == 600 + 100); // Normal prime 6% + A- spread 1%
    REQUIRE(c.can_issue_bond());
    c.issue_bond(1850);
    // $500K in, less the 2% underwriting fee.
    CHECK(c.cash() == Money::dollars(5'490'000));
    CHECK(c.this_year().lines[static_cast<std::size_t>(Ledger::BondFees)] == Money::dollars(10'000));
    CHECK(c.debt() == Money::dollars(500'000));
    // Assets 11x debt (+3,456), less 100 for the bond: B, the lowest that may borrow.
    CHECK(c.credit_score() == 3456 - 100);
    CHECK(c.credit_rating() == CreditRating::B);
    REQUIRE(c.can_issue_bond());
    c.issue_bond(1850);
    CHECK(c.credit_rating() == CreditRating::BMinus);
    CHECK_FALSE(c.can_issue_bond());
    // Worse rating, dearer money.
    CHECK(c.bonds()[1].rate_bp > c.bonds()[0].rate_bp);
}

TEST_CASE("a proven, lightly indebted company rates well; heavy debt rates badly") {
    Company c("Test", Money::dollars(10'000'000), 1850);
    c.post(Ledger::FreightRevenue, Money::dollars(1'000'000));
    for (int m = 0; m < 12; ++m) c.record_month();
    c.close_year();
    c.start_year(1851);
    // No debt +5,000, interest cover at its cap +2,500, a profitable year +500.
    CHECK(c.credit_score() == 8000);
    CHECK(c.credit_rating() == CreditRating::APlus);

    Company poor("Poor", Money::dollars(600'000), 1850);
    poor.post(Ledger::Fuel, Money::dollars(100'000)); // a losing year
    poor.close_year();
    poor.start_year(1851);
    poor.issue_bond(1851);
    // Assets $990K, about twice the debt (+985), a loss year -500, a bond -100: C.
    CHECK(poor.credit_score() == 385);
    CHECK(poor.credit_rating() == CreditRating::C);
    CHECK_FALSE(poor.can_issue_bond());
}

TEST_CASE("interest accrues monthly and the dearest bond is repaid first") {
    Company c("Test", Money::dollars(5'000'000), 1850);
    c.issue_bond(1850); // at BB
    c.issue_bond(1850); // at B
    const Money before = c.cash();
    c.charge_interest(3);
    // Each bond's quarter of interest, rounded to the cent separately.
    const Money expected = Money::dollars(500'000).scaled(c.bonds()[0].rate_bp * 3, 10000 * 12) +
                           Money::dollars(500'000).scaled(c.bonds()[1].rate_bp * 3, 10000 * 12);
    CHECK(before - c.cash() == expected);
    const std::int32_t cheaper = c.bonds()[0].rate_bp;
    const Money before_repay = c.cash();
    c.repay_bond();
    REQUIRE(c.bonds().size() == 1);
    CHECK(c.bonds()[0].rate_bp == cheaper);
    CHECK(before_repay - c.cash() == Money::dollars(510'000)); // face value plus 2% for repaying early
}

TEST_CASE("building costs the company cash, and is refused when it cannot pay") {
    World w = flat_world(600'000);
    const CommandResult track = w.execute(BuildTrack{.start = free_at(2, 5), .end = free_at(12, 5)});
    REQUIRE(track.ok);
    CHECK(w.company().cash() == Money::dollars(600'000) - track.cost);
    CHECK(w.company().track_value() == track.cost);

    const CommandResult tooFar = w.execute(BuildTrack{.start = free_at(2, 8), .end = free_at(38, 8)});
    CHECK_FALSE(tooFar.ok);
    CHECK(tooFar.error.find("not enough cash") != std::string::npos);

    const TrackNetwork& net = w.railway().track();
    const auto at = [&](std::int64_t x) { return pick_track_end(net, {x * kKm, 5 * kKm}, 600'000); };
    const Money cash_before = w.company().cash();
    REQUIRE(w.execute(BuildStation{.at = at(2), .size = StationSize::Small}).ok);
    CHECK(w.company().cash() == cash_before - Money::dollars(50'000));
    CHECK(w.company().building_value() == Money::dollars(50'000));
}

TEST_CASE("sandbox games ignore money") {
    World w = flat_world(0, /*sandbox=*/true);
    CHECK(w.execute(BuildTrack{.start = free_at(2, 5), .end = free_at(30, 5)}).ok);
    CHECK(w.company().cash() < Money{});
}

TEST_CASE("bonds through commands follow the rating rules") {
    World w = flat_world(5'000'000);
    CHECK(w.execute(IssueBond{}).ok);
    CHECK(w.execute(IssueBond{}).ok);
    const CommandResult third = w.execute(IssueBond{});
    CHECK_FALSE(third.ok);
    CHECK(third.error.find("credit rating") != std::string::npos);
    CHECK(w.execute(RepayBond{}).ok);
    CHECK(w.execute(RepayBond{}).ok);
    CHECK_FALSE(w.execute(RepayBond{}).ok);
    CHECK(w.company().this_year().lines[static_cast<std::size_t>(Ledger::BondFees)] ==
          Money::dollars(4 * 10'000)); // two issues and two early repayments at 2%
}

TEST_CASE("bonds mature after 30 years, and at most 20 may be outstanding") {
    Company c("Test", Money::dollars(1'000'000'000), 1850);
    c.post(Ledger::FreightRevenue, Money::dollars(500'000'000)); // proven and very rich: AAA
    c.start_year(1851);
    for (int i = 0; i < 20; ++i) {
        REQUIRE(c.can_issue_bond());
        c.issue_bond(1851);
    }
    CHECK_FALSE(c.can_issue_bond());
    c.retire_matured_bonds(1880);
    CHECK(c.bonds().size() == 20);
    const Money before = c.cash();
    c.retire_matured_bonds(1881);
    CHECK(c.bonds().empty());
    CHECK(before - c.cash() == Money::dollars(10'000'000)); // at face value, no penalty
}

TEST_CASE("running costs are charged monthly, and interest quarterly") {
    World w = flat_world(5'000'000);
    REQUIRE(w.execute(BuildTrack{.start = free_at(2, 5), .end = free_at(32, 5)}).ok);
    const TrackNetwork& net = w.railway().track();
    const auto at = [&](std::int64_t x) { return pick_track_end(net, {x * kKm, 5 * kKm}, 600'000); };
    const CommandResult a = w.execute(BuildStation{.at = at(2), .size = StationSize::Small});
    const CommandResult b = w.execute(BuildStation{.at = at(32), .size = StationSize::Small});
    REQUIRE(w.execute(BuyTrain{.loco = 0, .cars = 3, .route = {a.created_id, b.created_id}}).ok);
    REQUIRE(w.execute(IssueBond{}).ok);

    run_days(w, 31); // January 1850, charged on 1 February
    const auto& y = w.company().this_year();
    const auto line = [&](Ledger l) { return y.lines[static_cast<std::size_t>(l)]; };
    CHECK(line(Ledger::TrainMaintenance) == Money::dollars(1'000)); // $12K a year, new
    CHECK(line(Ledger::TrackUpkeep) == w.company().track_value().scaled(5, 1000));
    CHECK(line(Ledger::BuildingUpkeep) == Money::dollars(100'000).scaled(5, 1000));
    CHECK(line(Ledger::Interest) == Money{}); // not until the quarter ends
    run_days(w, 59); // to 1 April
    CHECK(w.company().this_year().lines[static_cast<std::size_t>(Ledger::Interest)] ==
          Money::dollars(500'000).scaled(w.company().bonds()[0].rate_bp * 3, 120000));
    CHECK(line(Ledger::Fuel) > Money{});
}

TEST_CASE("a new year opens new accounts; December's costs stay in the old year") {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 20;
    cfg.populate = false;
    cfg.start_date = Date::from_ymd(1850, 12, 1);
    World w(cfg, test_data());
    REQUIRE(w.execute(IssueBond{}).ok);
    run_days(w, 31);
    REQUIRE(w.company().history().size() == 2);
    CHECK(w.company().history()[0].year == 1850);
    CHECK(w.company().history()[0].lines[static_cast<std::size_t>(Ledger::Interest)] > Money{});
    CHECK(w.company().history()[1].year == 1851);
    CHECK(w.company().history()[1].lines[static_cast<std::size_t>(Ledger::Interest)] == Money{});
}
