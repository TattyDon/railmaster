#include "railmaster/sim/stock.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

Company founded(std::int64_t cash = 6'000'000) { return Company("Test", Money::dollars(cash), 1850); }

GameData no_data() { return {}; }

} // namespace

TEST_CASE("a new company's shares are priced at its cash per share; the player holds half") {
    const Company c = founded();
    const Investor inv;
    CHECK(c.shares_outstanding() == 600'000);
    CHECK(c.share_price() == Money::dollars(10));
    CHECK(inv.shares == 300'000);
    CHECK(public_float(inv, c) == 300'000);
    CHECK(net_worth(inv, c) == Money::dollars(500'000 + 3'000'000));
    CHECK(purchasing_power(inv, c) == Money::dollars(500'000 + 1'500'000));
}

TEST_CASE("buying a block raises the price, and the block trades at the raised price") {
    Company c = founded();
    Investor inv;
    REQUIRE_FALSE(buy_shares(inv, c, 1).has_value());
    // 1,000 of 600,000 shares, at 2% per 1%: +1/300 of the price.
    const Money expected_price = Money::dollars(10) + Money::dollars(10).scaled(2'000, 600'000);
    CHECK(c.share_price() == expected_price);
    CHECK(inv.cash == Money::dollars(500'000) - expected_price * 1000);
    CHECK(inv.shares == 301'000);
}

TEST_CASE("big trades move the price more, block by block") {
    Company small = founded(), big = founded();
    Investor a, b;
    REQUIRE_FALSE(buy_shares(a, small, 1).has_value());
    REQUIRE_FALSE(buy_shares(b, big, 30).has_value());
    CHECK(big.share_price() > small.share_price());
    CHECK(Money::dollars(500'000) - b.cash > (Money::dollars(500'000) - a.cash) * 30); // later blocks cost more

    Company c = founded();
    Investor seller;
    REQUIRE_FALSE(sell_shares(seller, c, 30).has_value());
    CHECK(c.share_price() < Money::dollars(10));
    CHECK(seller.cash - Money::dollars(500'000) < Money::dollars(10) * 30'000); // sold into a falling price
}

TEST_CASE("trading limits: the public float, your holding, and purchasing power") {
    Company c = founded();
    Investor inv;
    CHECK(buy_shares(inv, c, 301).has_value()); // only 300,000 in public hands
    CHECK(sell_shares(inv, c, 301).has_value());
    CHECK(buy_shares(inv, c, 0).has_value());

    // Margin: cash may go negative, but purchasing power may not.
    Investor margin;
    margin.cash = Money::dollars(100'000);
    REQUIRE_FALSE(buy_shares(margin, c, 50).has_value()); // ~$520K on $100K cash + margin
    CHECK(margin.cash < Money{});
    CHECK(purchasing_power(margin, c) >= Money{});
    // A big buy is checked against purchasing power before the trade, so the
    // price rise it causes cannot pay for it.
    const auto refused = buy_shares(margin, c, 200);
    REQUIRE(refused.has_value());
    CHECK(refused->find("purchasing power") != std::string::npos);
}

TEST_CASE("each month: salary in, margin interest out, and forced sales if underwater") {
    Company c = founded();
    Investor inv;
    inv.cash = Money::dollars(-120'000);
    const Money before = inv.cash;
    monthly_market(inv, c);
    // Salary in, then 10% a year on what is still owed.
    const Money salary = Money::dollars(provisional::kSalaryPerYear).scaled(1, 12);
    const Money owed = -(before + salary);
    CHECK(inv.cash == before + salary - owed.scaled(1000, 120'000));

    // A collapse in the share price leaves purchasing power negative: shares are sold.
    Investor bust;
    bust.cash = Money::dollars(-1'000'000);
    c.set_share_price(Money::dollars(5)); // holdings $1.5M count for $750K
    const std::int64_t sold = monthly_market(bust, c);
    CHECK(sold > 0);
    CHECK(sold % kShareBlock == 0);
    CHECK(purchasing_power(bust, c) >= Money{});
}

TEST_CASE("issuing stock raises cash, dilutes, depresses the price, and is limited to twice a year") {
    Company c = founded();
    const Investor inv;
    REQUIRE_FALSE(issue_stock(inv, c).has_value());
    CHECK(c.shares_outstanding() == 660'000);
    CHECK(c.cash() > Money::dollars(6'000'000 + 500'000));
    CHECK(c.share_price() < Money::dollars(10));
    REQUIRE_FALSE(issue_stock(inv, c).has_value());
    const auto third = issue_stock(inv, c);
    REQUIRE(third.has_value());
    CHECK(third->find("twice") != std::string::npos);
    c.start_year(1851);
    CHECK_FALSE(issue_stock(inv, c).has_value());
}

TEST_CASE("a buyback raises the price and lowers book value") {
    Company c = founded();
    const Investor inv;
    const Money book = c.book_value();
    REQUIRE_FALSE(buy_back_stock(inv, c).has_value());
    CHECK(c.shares_outstanding() == 540'000);
    CHECK(c.share_price() > Money::dollars(10));
    CHECK(c.book_value() < book);

    Company poor = founded(10'000); // $1 shares: 60,000 cost about $60K
    CHECK(buy_back_stock(inv, poor).has_value()); // cannot afford it
}

TEST_CASE("dividends are paid quarterly to every shareholder, or cut if unaffordable") {
    Company c = founded();
    Investor inv;
    c.set_dividend_per_share(Money::dollars(2)); // $2 a year: $0.50 a quarter
    pay_dividends(inv, c);
    CHECK(c.cash() == Money::dollars(6'000'000 - 300'000)); // 600,000 x $0.50
    CHECK(inv.cash == Money::dollars(500'000 + 150'000));   // half the shares, half the payout
    CHECK(c.this_year().dividends_paid == Money::dollars(300'000));

    Company broke = founded(100'000);
    broke.set_dividend_per_share(Money::dollars(2));
    Investor other;
    pay_dividends(other, broke);
    CHECK(broke.dividend_per_share() == Money{});
    CHECK(other.cash == Money::dollars(500'000));
}

TEST_CASE("the share price follows earnings, dividends and book value") {
    Company earner = founded(), loser = founded(), payer = founded();
    Investor inv;
    for (int m = 0; m < 12; ++m) {
        earner.post(Ledger::FreightRevenue, Money::dollars(200'000)); // $2.4M a year: $4 a share
        loser.post(Ledger::Fuel, Money::dollars(100'000));
        earner.record_month();
        loser.record_month();
        payer.record_month();
        monthly_market(inv, earner);
        monthly_market(inv, loser);
    }
    payer.set_dividend_per_share(Money::dollars(1));
    for (int m = 0; m < 12; ++m) monthly_market(inv, payer);
    CHECK(earner.share_price() > Money::dollars(25));
    CHECK(loser.share_price() < Money::dollars(6));
    CHECK(payer.share_price() > Money::dollars(10));
    CHECK(target_share_price(earner) > target_share_price(payer));
}

TEST_CASE("in the world, dividends land at quarter ends and commands trade shares") {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 16;
    cfg.populate = false;
    World w(cfg, no_data());
    REQUIRE(w.execute(SetDividend{.per_share = Money::dollars(1)}).ok);
    CHECK_FALSE(w.execute(SetDividend{.per_share = Money::dollars(-1)}).ok);
    REQUIRE(w.execute(BuyShares{.blocks = 10}).ok);
    CHECK(w.investor().shares == 310'000);
    REQUIRE(w.execute(SellShares{.blocks = 5}).ok);
    CHECK(w.investor().shares == 305'000);
    CHECK(w.execute(IssueStock{}).ok);
    CHECK(w.execute(BuyBackStock{}).ok);

    const Money cash_before = w.investor().cash;
    for (int d = 0; d < 95; ++d) // 1 Jan to 6 Apr 1830: paid once, on 1 April
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    const Money dividend = Money::dollars(1).scaled(305'000, 4);
    CHECK(w.company().this_year().dividends_paid > Money{});
    CHECK(w.investor().cash ==
          cash_before + dividend + Money::dollars(provisional::kSalaryPerYear).scaled(1, 12) * 3); // three salaries
}
