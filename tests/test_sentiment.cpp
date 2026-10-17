#include "legacy_scale.hpp"
#include "railmaster/sim/world.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace railmaster::sim;

namespace {

Company founded() { return Company("Test", Money::dollars(6'000'000), 1850); }

bool any_news(const std::vector<std::string>& news, const std::string& part) {
    return std::any_of(news.begin(), news.end(), [&](const std::string& n) { return n.find(part) != std::string::npos; });
}

void run_days(World& w, int days) {
    for (int d = 0; d < days; ++d)
        for (int i = 0; i < World::kTicksPerDay; ++i) w.tick();
}

// Run to 1 January of `year`, collecting the news on the way.
std::vector<std::string> run_to_year(World& w, std::int32_t year) {
    std::vector<std::string> news;
    while (w.date().year() < year) {
        run_days(w, 1);
        for (std::string& n : w.take_news()) news.push_back(std::move(n));
    }
    return news;
}

GameData one_spare_tycoon() {
    GameData d;
    d.tycoons = TycoonRegistry::from_json(R"({"tycoons": [{"key": "new", "name": "N. Newcomer", "company": "New Co"}]})");
    return d;
}

struct Fixture {
    World w;
    PlayerId rival = 0;
    CompanyId rival_co = 0;

    explicit Fixture(bool can_be_fired = true) : w(make(can_be_fired)) {
        rival = w.add_player_company("Rival Lines", "A. Rival");
        rival_co = *w.investors()[rival].chairs;
    }
    static World make(bool can_be_fired) {
        WorldConfig cfg;
        testing::legacy_scale(cfg);
        cfg.width_tiles = cfg.height_tiles = 16;
        cfg.populate = false;
        cfg.business_cycle = false;
        cfg.chairman_can_be_fired = can_be_fired;
        return World(cfg, one_spare_tycoon());
    }
    // A loss in each of the next `years` years for company `c`.
    std::vector<std::string> losing_years(CompanyId c, int years) {
        std::vector<std::string> news;
        for (int i = 0; i < years; ++i) {
            w.company(c).post(Ledger::Fuel, Money::dollars(100'000));
            for (std::string& n : run_to_year(w, w.date().year() + 1)) news.push_back(std::move(n));
        }
        return news;
    }
};

} // namespace

TEST_CASE("each year records the shareholders' return; bad years and the weighted return follow [D/I]") {
    Company c = founded();
    CHECK(c.weighted_return_permille() == 0);
    CHECK(c.sentiment() == Sentiment::Content);

    // 1850: $10 to $12, plus a $0.50 dividend: 25%.
    c.set_dividend_per_share(Money::dollars(2));
    REQUIRE(c.pay_quarterly_dividend() > Money{});
    c.set_dividend_per_share(Money{});
    c.set_share_price(Money::dollars(12));
    c.post(Ledger::FreightRevenue, Money::dollars(500'000));
    c.close_year();
    CHECK(c.this_year().share_return_permille == 250);
    c.start_year(1851);
    CHECK(c.bad_year_streak() == 0);
    CHECK(c.sentiment() == Sentiment::Happy);

    // 1851: a loss, and the price falls to $9: a bad year.
    c.post(Ledger::Fuel, Money::dollars(1'000'000));
    c.set_share_price(Money::dollars(9));
    c.close_year();
    CHECK(c.this_year().share_return_permille == -250);
    c.start_year(1852);
    CHECK(c.bad_year_streak() == 1);

    // 1852: no profit, but better than last year's loss, so not bad though the price fell.
    c.set_share_price(Money::dollars(8));
    c.close_year();
    c.start_year(1853);
    CHECK(c.bad_year_streak() == 0);
    // Weighted 3, 4 and 5 (latest): (3 x 250 + 4 x -250 + 5 x -111) / 12.
    CHECK(c.weighted_return_permille() == (3 * 250 + 4 * -250 + 5 * -111) / 12);

    // Two losses in a row: grumbling; three: hostile.
    for (int y = 0; y < 3; ++y) {
        c.post(Ledger::Fuel, Money::dollars(1'000'000));
        c.close_year();
        c.start_year(1854 + y);
        if (y == 1) CHECK(c.sentiment() == Sentiment::Grumbling);
    }
    CHECK(c.bad_year_streak() == 3);
    CHECK(c.sentiment() == Sentiment::Hostile);
}

TEST_CASE("the chairman's salary follows the five-year weighted return [I]") {
    Company c = founded();
    CHECK(chairman_salary(c) == Money::dollars(50'000));
    c.set_share_price(Money::dollars(12)); // +20%
    c.close_year();
    c.start_year(1851);
    CHECK(chairman_salary(c) == Money::dollars(70'000)); // x (1 + 2 x 0.2)
    c.set_share_price(Money::dollars(1)); // -92%
    c.close_year();
    c.start_year(1852);
    CHECK(chairman_salary(c) == Money::dollars(25'000)); // held at half
}

TEST_CASE("a high share price splits the stock, and every holding with it [C/I]") {
    Market m;
    m.companies.push_back(founded());
    m.investors.push_back(Investor::founder("Founder", 0));
    Investor shorter;
    shorter.add_shares(0, -2'000);
    m.investors.push_back(shorter);
    Company& c = m.companies[0];
    c.set_share_price(Money::dollars(130));
    c.set_dividend_per_share(Money::dollars(4));
    const Money worth = net_worth(m.investors[0], m);
    CHECK(apply_splits(m).empty());
    CHECK(apply_splits(m).empty());
    const auto splits = apply_splits(m); // the third month end above $120
    REQUIRE(splits.size() == 1);
    CHECK(splits[0].second == 2);
    CHECK(c.shares_outstanding() == 1'200'000);
    CHECK(c.share_price() == Money::dollars(65));
    CHECK(c.dividend_per_share() == Money::dollars(2));
    CHECK(m.investors[0].shares_in(0) == 600'000);
    CHECK(m.investors[1].shares_in(0) == -4'000);
    CHECK(net_worth(m.investors[0], m) == worth);

    // Over $200: three for one. A dip below $120 restarts the count.
    c.set_share_price(Money::dollars(250));
    apply_splits(m);
    c.set_share_price(Money::dollars(100));
    apply_splits(m);
    c.set_share_price(Money::dollars(250));
    apply_splits(m);
    apply_splits(m);
    const auto big = apply_splits(m);
    REQUIRE(big.size() == 1);
    CHECK(big[0].second == 3);
    CHECK(c.shares_outstanding() == 3'600'000);
}

TEST_CASE("after two bad years investors grumble; after three they vote the chairman out [D]") {
    Fixture f;
    const auto news = f.losing_years(f.rival_co, 2);
    CHECK(any_news(news, "grumbling"));
    CHECK(f.w.investors()[f.rival].chairs == f.rival_co);

    const auto third = f.losing_years(f.rival_co, 1);
    CHECK(any_news(third, "voted A. Rival out"));
    CHECK_FALSE(f.w.investors()[f.rival].chairs.has_value());
    // The player runs a company already, so the board brings in a newcomer.
    REQUIRE(f.w.investors().size() == 3);
    CHECK(f.w.investors()[2].name == "N. Newcomer");
    CHECK(f.w.investors()[2].chairs == f.rival_co);
    CHECK(f.w.rivals().size() == 1);
    CHECK(any_news(third, "N. Newcomer is now chairman of Rival Lines"));
}

TEST_CASE("a chairman with over half the shares cannot be removed, and scenarios can lock the chair [C/D]") {
    Fixture f;
    REQUIRE(f.w.execute(BuyShares{.blocks = 1, .company = f.rival_co}, f.rival).ok); // 301,000 of 600,000
    const auto news = f.losing_years(f.rival_co, 3);
    CHECK(any_news(news, "keeps the chair"));
    CHECK(f.w.investors()[f.rival].chairs == f.rival_co);

    Fixture locked(/*can_be_fired=*/false);
    const auto grumbles = locked.losing_years(locked.rival_co, 4);
    CHECK(any_news(grumbles, "grumbling"));
    CHECK(locked.w.investors()[locked.rival].chairs == locked.rival_co);
}

TEST_CASE("the player can be fired too, and then runs nothing until they win a chair back") {
    Fixture f;
    const auto news = f.losing_years(0, 3);
    CHECK(any_news(news, "voted You out"));
    CHECK_FALSE(f.w.player_company().has_value());
    CHECK(f.w.company().id() == 0); // still shown: the company they last ran
    const CommandResult r = f.w.execute(IssueBond{});
    REQUIRE_FALSE(r.ok);
    CHECK(r.error.find("do not run a company") != std::string::npos);
    CHECK(f.w.execute(BuyShares{.blocks = 1, .company = f.rival_co}).ok); // trading still works

    // When the rival steps down, the biggest shareholder with no company, the player, is appointed.
    REQUIRE(f.w.execute(Resign{}, f.rival).ok);
    CHECK(f.w.player_company() == f.rival_co);
    CHECK(f.w.company().id() == f.rival_co);
}

TEST_CASE("resigning keeps your shares; the board appoints someone else") {
    Fixture f;
    const std::int64_t shares = f.w.investor().shares_in(0);
    REQUIRE(f.w.execute(Resign{}).ok);
    CHECK_FALSE(f.w.player_company().has_value());
    CHECK(f.w.investor().shares_in(0) == shares);
    CHECK(f.w.investors()[2].chairs == CompanyId{0}); // the newcomer: the rival already runs a company
    CHECK(any_news(f.w.take_news(), "resigned"));
    CHECK_FALSE(f.w.execute(Resign{}).ok); // nothing left to resign from

    WorldConfig cfg;

    testing::legacy_scale(cfg);
    cfg.width_tiles = cfg.height_tiles = 16;
    cfg.populate = false;
    cfg.chairman_can_resign = false;
    World locked(cfg);
    CHECK_FALSE(locked.execute(Resign{}).ok);
}
