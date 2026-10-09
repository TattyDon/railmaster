#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace railmaster::sim;

namespace {

World plain_world() {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 16;
    cfg.populate = false;
    cfg.business_cycle = false;
    cfg.chairman_can_be_fired = false; // these tests lose money on purpose
    return World(cfg);
}

void run_to_year(World& w, std::int32_t year) {
    while (w.date().year() < year)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

} // namespace

TEST_CASE("bankruptcy needs bond debt and two loss years, or bills it cannot pay [C/I]") {
    World w = plain_world();
    const CommandResult none = w.execute(DeclareBankruptcy{});
    REQUIRE_FALSE(none.ok);
    CHECK(none.error.find("no bond debt") != std::string::npos);

    REQUIRE(w.execute(IssueBond{}).ok);
    const CommandResult healthy = w.execute(DeclareBankruptcy{});
    REQUIRE_FALSE(healthy.ok);
    CHECK(healthy.error.find("loss years") != std::string::npos);

    // Two losing years in a row open the way.
    w.company().post(Ledger::Fuel, Money::dollars(100'000));
    run_to_year(w, 1831);
    CHECK_FALSE(w.execute(DeclareBankruptcy{}).ok); // one is not enough
    w.company().post(Ledger::Fuel, Money::dollars(100'000));
    run_to_year(w, 1832);
    CHECK(w.execute(DeclareBankruptcy{}).ok);
}

TEST_CASE("a company that cannot pay may go bankrupt at once") {
    World w = plain_world();
    REQUIRE(w.execute(IssueBond{}).ok);
    w.company().post(Ledger::Fuel, w.company().cash() + Money::dollars(1)); // the money is gone
    CHECK(w.execute(DeclareBankruptcy{}).ok);
}

TEST_CASE("bankruptcy halves the debt, pays bondholders in new shares and ruins credit [D/I]") {
    World w = plain_world();
    REQUIRE(w.execute(IssueBond{}).ok);
    REQUIRE(w.execute(IssueBond{}).ok);
    w.company().post(Ledger::Fuel, w.company().cash() + Money::dollars(1));
    const Company& c = w.company();
    const Money debt = c.debt();
    const std::int64_t shares = c.shares_outstanding();
    const Money price = c.share_price();
    const std::int64_t mine = w.investor().shares_in(c.id());
    REQUIRE(w.execute(DeclareBankruptcy{}).ok);

    CHECK(c.debt() == debt.scaled(1, 2));
    CHECK(c.this_year().debt_forgiven == debt.scaled(1, 2));
    // $500,000 of forgiven debt at the old price becomes new shares in public hands.
    const std::int64_t issued = debt.scaled(1, 2).in_cents() / price.in_cents();
    CHECK(c.shares_outstanding() == shares + issued);
    CHECK(w.investor().shares_in(c.id()) == mine);
    CHECK(c.share_price() == price.scaled(shares, shares + issued));
    // Rated D for five years: no new bonds; and no second bankruptcy for ten.
    CHECK(c.credit_rating() == CreditRating::D);
    CHECK_FALSE(w.execute(IssueBond{}).ok);
    CHECK(w.execute(DeclareBankruptcy{}).error.find("twice") != std::string::npos);
    CHECK(c.bankrupt_year() == 1830);
    // Even once the books recover, the rating stays at D until the five years are up.
    w.company().post(Ledger::FreightRevenue, Money::dollars(20'000'000));
    run_to_year(w, 1834); // 1830 to 1834: five years at D
    CHECK(c.credit_rating() == CreditRating::D);
    run_to_year(w, 1835);
    CHECK(c.credit_rating() == CreditRating::AAA);
    CHECK(w.execute(DeclareBankruptcy{}).error.find("twice") != std::string::npos);
}

TEST_CASE("founding a company: your money buys shares at $10, investors add theirs [I, RT2]") {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 16;
    cfg.populate = false;
    cfg.found_player_company = false;
    World w(cfg);
    CHECK_FALSE(w.player_company().has_value());
    CHECK(w.investor().cash == Money::dollars(3'500'000));

    const auto found = [&](std::int64_t mine, std::int64_t outside) {
        return w.execute(FoundCompany{.name = "Lone Star", .personal_investment = Money::dollars(mine),
                                      .outside_investment = Money::dollars(outside)});
    };
    CHECK(found(50'000, 1'000'000).error.find("at least") != std::string::npos);
    CHECK(found(4'000'000, 1'000'000).error.find("that much") != std::string::npos);
    CHECK(found(1'000'000, 3'500'000).error.find("at most") != std::string::npos);
    const CommandResult ok = found(2'000'000, 1'000'000);
    REQUIRE(ok.ok);
    const Company& c = w.company(static_cast<CompanyId>(ok.created_id));
    CHECK(w.player_company() == c.id());
    CHECK(c.name() == "Lone Star");
    CHECK(c.cash() == Money::dollars(3'000'000));
    CHECK(c.shares_outstanding() == 300'000);
    CHECK(c.share_price() == Money::dollars(10));
    CHECK(w.investor().shares_in(c.id()) == 200'000); // two thirds: safe from being voted out
    CHECK(w.investor().cash == Money::dollars(1'500'000));
    CHECK(public_float(w.market(), c.id()) == 100'000);
    CHECK(found(500'000, 0).error.find("already run") != std::string::npos);

    // Invalid terms in a world's config are refused outright.
    WorldConfig bad = cfg;
    bad.found_player_company = true;
    bad.founding = FoundCompany{.name = "X", .personal_investment = Money::dollars(1), .outside_investment = {}};
    CHECK_THROWS_AS([&] { const World refused(bad); }(), std::invalid_argument);
}
