#include "railmaster/sim/stock.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

namespace {

Company founded(std::int64_t cash = 6'000'000, CompanyId id = 0) {
    return Company("Test " + std::to_string(id), Money::dollars(cash), 1850, default_balance(), id);
}

// One company, its founder (player 0), and an outside investor (player 1)
// with the same starting cash and no shares.
Market one_company(std::int64_t cash = 6'000'000) {
    Market m;
    m.companies.push_back(founded(cash));
    // The founder put in half the capital, from the usual fortune.
    Investor founder = Investor::founder("Founder", 0, default_balance(), Money::dollars(cash / 2));
    founder.cash = Money::dollars(500'000);
    m.investors.push_back(founder);
    Investor outsider;
    outsider.name = "Outsider";
    outsider.cash = Money::dollars(500'000);
    m.investors.push_back(outsider);
    return m;
}

GameData no_data() { return {}; }

} // namespace

TEST_CASE("a new company's shares are priced at its cash per share; the founder holds half") {
    const Market m = one_company();
    const Company& c = m.companies[0];
    const Investor& inv = m.investors[0];
    CHECK(c.shares_outstanding() == 600'000);
    CHECK(c.share_price() == Money::dollars(10));
    CHECK(inv.shares_in(0) == 300'000);
    CHECK(public_float(m, 0) == 300'000);
    CHECK(net_worth(inv, m) == Money::dollars(500'000 + 3'000'000));
    CHECK(purchasing_power(inv, m) == Money::dollars(500'000 + 1'500'000));
}

TEST_CASE("buying a block raises the price, and the block trades at the raised price") {
    Market m = one_company();
    REQUIRE_FALSE(buy_shares(m, 0, 0, 1).has_value());
    // 1,000 of 600,000 shares, at 2% per 1%: +1/300 of the price.
    const Money expected_price = Money::dollars(10) + Money::dollars(10).scaled(2'000, 600'000);
    CHECK(m.companies[0].share_price() == expected_price);
    // Plus 1% to the broker.
    CHECK(m.investors[0].cash == Money::dollars(500'000) - (expected_price * 1000).scaled(101, 100));
    CHECK(m.investors[0].shares_in(0) == 301'000);
}

TEST_CASE("big trades move the price more, block by block") {
    Market small = one_company(), big = one_company();
    REQUIRE_FALSE(buy_shares(small, 1, 0, 1).has_value());
    REQUIRE_FALSE(buy_shares(big, 1, 0, 30).has_value());
    CHECK(big.companies[0].share_price() > small.companies[0].share_price());
    CHECK(Money::dollars(500'000) - big.investors[1].cash >
          (Money::dollars(500'000) - small.investors[1].cash) * 30); // later blocks cost more

    Market m = one_company();
    REQUIRE_FALSE(sell_shares(m, 0, 0, 30).has_value());
    CHECK(m.companies[0].share_price() < Money::dollars(10));
    CHECK(m.investors[0].cash - Money::dollars(500'000) < Money::dollars(10) * 30'000); // sold into a falling price
}

TEST_CASE("trading limits: the public float, your holding, and purchasing power") {
    Market m = one_company();
    CHECK(buy_shares(m, 0, 0, 301).has_value()); // only 300,000 in public hands
    CHECK(sell_shares(m, 0, 0, 301).has_value());
    CHECK(buy_shares(m, 0, 0, 0).has_value());
    CHECK(buy_shares(m, 0, 1, 1).has_value()); // no such company
    CHECK(sell_shares(m, 0, 0, 301).has_value()); // the founder may not sell their own company short

    // Margin: cash may go negative, but purchasing power may not.
    m.investors[0].cash = Money::dollars(100'000);
    REQUIRE_FALSE(buy_shares(m, 0, 0, 50).has_value()); // ~$520K on $100K cash + margin
    CHECK(m.investors[0].cash < Money{});
    CHECK(purchasing_power(m.investors[0], m) >= Money{});
    // A big buy is checked against purchasing power before the trade, so the
    // price rise it causes cannot pay for it.
    const auto refused = buy_shares(m, 0, 0, 200);
    REQUIRE(refused.has_value());
    CHECK(refused->find("purchasing power") != std::string::npos);
}

TEST_CASE("each month: salary for chairmen, margin interest out, and forced sales if underwater") {
    Market m = one_company();
    m.investors[0].cash = Money::dollars(-120'000);
    const Money before = m.investors[0].cash;
    monthly_market(m);
    // Salary in, then 10% a year on what is still owed.
    const Money salary = Money::dollars(default_balance().stock.salary_per_year).scaled(1, 12);
    const Money owed = -(before + salary);
    CHECK(m.investors[0].cash == before + salary - owed.scaled(1000, 120'000));
    CHECK(m.investors[1].cash == Money::dollars(500'000)); // no company, no salary

    // A collapse in the share price leaves purchasing power negative: shares are sold.
    Market crash = one_company();
    crash.investors[0].cash = Money::dollars(-1'000'000);
    crash.companies[0].set_share_price(Money::dollars(5)); // holdings $1.5M count for $750K
    const std::int64_t sold = monthly_market(crash)[0];
    CHECK(sold > 0);
    CHECK(sold % default_balance().stock.share_block == 0);
    CHECK(purchasing_power(crash.investors[0], crash) >= Money{});
}

TEST_CASE("issuing stock raises cash, dilutes, depresses the price, and is limited to twice a year") {
    Company c = founded();
    REQUIRE_FALSE(issue_stock(c).has_value());
    CHECK(c.shares_outstanding() == 660'000);
    CHECK(c.cash() > Money::dollars(6'000'000 + 500'000));
    CHECK(c.share_price() < Money::dollars(10));
    REQUIRE_FALSE(issue_stock(c).has_value());
    const auto third = issue_stock(c);
    REQUIRE(third.has_value());
    CHECK(third->find("twice") != std::string::npos);
    c.start_year(1851);
    CHECK_FALSE(issue_stock(c).has_value());
}

TEST_CASE("a buyback raises the price and lowers book value") {
    Market m = one_company();
    const Money book = m.companies[0].book_value();
    REQUIRE_FALSE(buy_back_stock(m, 0).has_value());
    CHECK(m.companies[0].shares_outstanding() == 540'000);
    CHECK(m.companies[0].share_price() > Money::dollars(10));
    CHECK(m.companies[0].book_value() < book);

    Market poor = one_company(10'000); // $1 shares: 60,000 cost about $60K
    CHECK(buy_back_stock(poor, 0).has_value()); // cannot afford it
}

TEST_CASE("dividends are paid quarterly to every shareholder, or cut if unaffordable") {
    Market m = one_company();
    REQUIRE_FALSE(buy_shares(m, 1, 0, 30).has_value()); // the outsider buys 30,000
    const Money outsider_cash = m.investors[1].cash;
    m.companies[0].set_dividend_per_share(Money::dollars(2)); // $2 a year: $0.50 a quarter
    pay_dividends(m, 0);
    CHECK(m.companies[0].cash() == Money::dollars(6'000'000 - 300'000)); // 600,000 x $0.50
    CHECK(m.investors[0].cash == Money::dollars(500'000 + 150'000));   // half the shares, half the payout
    CHECK(m.investors[1].cash == outsider_cash + Money::dollars(15'000));
    CHECK(m.companies[0].this_year().dividends_paid == Money::dollars(300'000));

    Market broke = one_company(100'000); // 10,000 shares: $50 a year each is $125,000 a quarter
    broke.companies[0].set_dividend_per_share(Money::dollars(50));
    pay_dividends(broke, 0);
    CHECK(broke.companies[0].dividend_per_share() == Money{});
    CHECK(broke.investors[0].cash == Money::dollars(500'000));
}

TEST_CASE("the share price follows earnings, dividends and book value") {
    Market m;
    m.companies = {founded(6'000'000, 0), founded(6'000'000, 1), founded(6'000'000, 2)};
    Company& earner = m.companies[0];
    Company& loser = m.companies[1];
    Company& payer = m.companies[2];
    for (int month = 0; month < 12; ++month) {
        earner.post(Ledger::FreightRevenue, Money::dollars(200'000)); // $2.4M a year: $4 a share
        loser.post(Ledger::Fuel, Money::dollars(100'000));
        for (Company& c : m.companies) c.record_month();
        monthly_market(m);
    }
    const Money payer_before = payer.share_price();
    payer.set_dividend_per_share(Money::dollars(1));
    for (int month = 0; month < 12; ++month) monthly_market(m);
    CHECK(earner.share_price() > Money::dollars(25));
    CHECK(loser.share_price() < Money::dollars(6));
    CHECK(payer.share_price() > payer_before);
    CHECK(target_share_price(earner) > target_share_price(payer));
}

TEST_CASE("a portfolio across companies: net worth, purchasing power and margin calls count every holding") {
    Market m;
    m.companies = {founded(6'000'000, 0), founded(3'000'000, 1)};
    m.investors = {Investor::founder("A", 0), Investor::founder("B", 1, default_balance(), Money::dollars(1'500'000))};
    CHECK(m.companies[1].share_price() == Money::dollars(10));
    CHECK(m.companies[1].shares_outstanding() == 300'000);
    REQUIRE_FALSE(buy_shares(m, 0, 1, 20).has_value()); // A buys 20,000 of B's company
    const Investor& a = m.investors[0];
    CHECK(a.shares_in(1) == 20'000);
    CHECK(public_float(m, 1) == 150'000 - 20'000); // B holds half
    CHECK(holdings_value(a, m) ==
          m.companies[0].share_price() * 300'000 + m.companies[1].share_price() * 20'000);
    CHECK(net_worth(a, m) == a.cash + holdings_value(a, m));

    // Underwater: the most valuable holding (A's own company) is sold first.
    m.investors[0].cash = Money::dollars(-1'700'000);
    const std::int64_t sold = monthly_market(m)[0];
    CHECK(sold > 0);
    CHECK(m.investors[0].shares_in(0) < 300'000);
    CHECK(m.investors[0].shares_in(1) == 20'000);
    CHECK(purchasing_power(m.investors[0], m) >= Money{});
}

TEST_CASE("in the world, dividends land at quarter ends and commands trade shares") {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 16;
    cfg.populate = false;
    World w(cfg, no_data());
    REQUIRE(w.execute(SetDividend{.per_share = Money::dollars(1)}).ok);
    CHECK_FALSE(w.execute(SetDividend{.per_share = Money::dollars(-1)}).ok);
    REQUIRE(w.execute(BuyShares{.blocks = 10}).ok);
    CHECK(w.investor().shares_in(0) == 310'000);
    REQUIRE(w.execute(SellShares{.blocks = 5}).ok);
    CHECK(w.investor().shares_in(0) == 305'000);
    CHECK_FALSE(w.execute(BuyShares{.blocks = 1, .company = 3}).ok); // no such company
    CHECK(w.execute(IssueStock{}).ok);
    CHECK(w.execute(BuyBackStock{}).ok);

    const Money cash_before = w.investor().cash;
    for (int d = 0; d < 95; ++d) // 1 Jan to 6 Apr 1830: paid once, on 1 April
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
    const Money dividend = Money::dollars(1).scaled(305'000, 4);
    CHECK(w.company().this_year().dividends_paid > Money{});
    CHECK(w.investor().cash ==
          cash_before + dividend + Money::dollars(default_balance().stock.salary_per_year).scaled(1, 12) * 3); // three salaries
}

TEST_CASE("selling short: borrowed shares sold now, bought back later [D]") {
    Market m = one_company();
    Investor& outsider = m.investors[1];
    const Money worth = net_worth(outsider, m);
    REQUIRE_FALSE(sell_shares(m, 1, 0, 2).has_value()); // the outsider holds none: sells 2,000 short
    CHECK(m.investors[1].shares_in(0) == -2'000);
    CHECK(m.investors[1].cash > Money::dollars(500'000));
    CHECK(m.companies[0].share_price() < Money::dollars(10)); // selling pushes the price down
    // A short is a debt in shares: net worth counts it at today's price, and
    // purchasing power holds 150% of its value against it.
    const Money owed = m.companies[0].share_price() * 2'000;
    CHECK(net_worth(m.investors[1], m) == m.investors[1].cash - owed);
    // Each block sold above the final price the debt is marked at, which
    // nearly offsets the 1% brokerage.
    CHECK(net_worth(m.investors[1], m) >= worth - owed.scaled(1, 100));
    CHECK(net_worth(m.investors[1], m) < worth);
    CHECK(purchasing_power(m.investors[1], m) == m.investors[1].cash - owed.scaled(150, 100));
    CHECK(public_float(m, 0) == 302'000); // borrowed shares are in public hands

    // Buying covers the short first.
    REQUIRE_FALSE(buy_shares(m, 1, 0, 2).has_value());
    CHECK(m.investors[1].shares_in(0) == 0);
    (void)outsider;
}

TEST_CASE("short selling is limited to half your net worth, and never your own company [C]") {
    Market m = one_company();
    // $500K net worth: at about $10 a share, roughly 25,000 shares may be shorted.
    CHECK_FALSE(sell_shares(m, 1, 0, 20).has_value());
    const auto refused = sell_shares(m, 1, 0, 10);
    REQUIRE(refused.has_value());
    CHECK(refused->find("net worth") != std::string::npos);
    const auto own = sell_shares(m, 0, 0, 301);
    REQUIRE(own.has_value());
    CHECK(own->find("own company") != std::string::npos);
}

TEST_CASE("short sellers pay the dividend, and a rising price forces them to buy back") {
    Market m = one_company();
    REQUIRE_FALSE(sell_shares(m, 1, 0, 20).has_value());
    const Money before = m.investors[1].cash;
    m.companies[0].set_dividend_per_share(Money::dollars(2));
    pay_dividends(m, 0);
    CHECK(m.investors[1].cash == before - Money::dollars(2).scaled(20'000, 4)); // $0.50 a share for the quarter

    // The price triples: the short is now underwater, so it is bought back.
    m.companies[0].set_share_price(m.companies[0].share_price() * 3);
    CHECK(purchasing_power(m.investors[1], m) < Money{});
    const std::int64_t covered = monthly_market(m)[1];
    CHECK(covered > 0);
    CHECK(m.investors[1].shares_in(0) > -20'000);
    CHECK((purchasing_power(m.investors[1], m) >= Money{} || m.investors[1].shares_in(0) == 0));
}

TEST_CASE("every trade pays the broker about 1% [I]") {
    Market m = one_company();
    const Money before = m.investors[1].cash;
    REQUIRE_FALSE(buy_shares(m, 1, 0, 10).has_value());
    const Money paid = before - m.investors[1].cash;
    REQUIRE_FALSE(sell_shares(m, 1, 0, 10).has_value());
    const Money back = m.investors[1].cash - (before - paid);
    // A round trip loses the price impact both ways plus two commissions.
    CHECK(paid - back > paid.scaled(2, 100) - Money::dollars(1));

    Balance free = default_balance();
    free.stock.brokerage_permille = 0;
    Market n;
    n.companies.push_back(Company("Free", Money::dollars(6'000'000), 1850, free, 0));
    Investor buyer;
    buyer.cash = Money::dollars(500'000);
    n.investors.push_back(buyer);
    REQUIRE_FALSE(buy_shares(n, 0, 0, 1).has_value());
    CHECK(n.investors[0].cash == Money::dollars(500'000) - n.companies[0].share_price() * 1000);
}
