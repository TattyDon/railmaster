#pragma once

#include "railmaster/sim/company.hpp"
#include "railmaster/sim/money.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace railmaster::sim {

// The stock market and the players' own money. Researched rules are in
// docs/spec/overview-finance-scenarios.md §4.1 and §4.3; the rest is
// documented in docs/spec/m3-finance-model.md.

// Documented [D]: stock may be issued at most twice a year. The share block,
// margin, salary and price model are in Balance::Stock, read from the
// company concerned.
constexpr std::int32_t kMaxStockIssuesPerYear = 2;

// A player: the human (always PlayerId 0) or an AI tycoon. Each has a
// private account and a portfolio, and may chair one company.
using PlayerId = std::uint16_t;
constexpr PlayerId kHumanPlayer = 0;

struct Investor {
    std::string name{};
    Money cash{};
    std::vector<std::int64_t> holdings{}; // shares, indexed by CompanyId
    std::optional<CompanyId> chairs{};

    std::int64_t shares_in(CompanyId c) const { return c < holdings.size() ? holdings[c] : 0; }
    void add_shares(CompanyId c, std::int64_t n);

    // The founder of `company`: starting personal cash and the founder's shares.
    static Investor founder(std::string name, CompanyId company, const Balance& b = default_balance());
};

// Every company and every player.
struct Market {
    std::vector<Company> companies{};
    std::vector<Investor> investors{};
};

Money holdings_value(const Investor& inv, const Market& m);
Money net_worth(const Investor& inv, const Market& m);
// Cash plus what can be borrowed against shares held. Buying needs this to
// cover the cost; if it goes negative, shares are sold automatically.
Money purchasing_power(const Investor& inv, const Market& m);
// Shares of a company held by no player.
std::int64_t public_float(const Market& m, CompanyId c);

// Move the price for trading `shares` (one block at a time); returns the
// total paid or received, each block at the price after its own impact.
Money trade_with_impact(Company& c, std::int64_t shares, bool buying);

// Each returns an error message, or nullopt on success. Selling more than
// you hold sells short (not your own company; within a cap on net worth);
// buying while short covers first.
std::optional<std::string> buy_shares(Market& m, PlayerId who, CompanyId c, std::int64_t blocks);
std::optional<std::string> sell_shares(Market& m, PlayerId who, CompanyId c, std::int64_t blocks);
std::optional<std::string> issue_stock(Company& c);
std::optional<std::string> buy_back_stock(Market& m, CompanyId c);

// Monthly: move every price towards its value; pay chairmen their salary;
// charge margin interest; for anyone whose purchasing power has gone
// negative, sell longs or buy back shorts, biggest first. Returns the shares
// traded that way, by player.
std::vector<std::int64_t> monthly_market(Market& m);
Money target_share_price(const Company& c);

// A shareholder vote on a takeover or merger (rt3-clone-spec §12.6).
struct Vote {
    std::int64_t yes = 0;
    std::int64_t outstanding = 0;
    bool passed() const { return yes * 2 > outstanding; }
    std::int32_t percent_yes() const {
        return outstanding > 0 ? static_cast<std::int32_t>(yes * 100 / outstanding) : 0;
    }
};

// Replacing the chairman of `target` with `bidder`. The bidder's shares vote
// yes and the chairman's no. The public backs the bidder more when the
// company trades below book value or lost money last year; other players
// vote with the public majority [I]. Over 50% of the shares always wins [C].
Vote takeover_vote(const Market& m, PlayerId bidder, CompanyId target, const Balance::Corporate& b);

// Selling `target` to the bidder's company for `offer_per_share`. The
// bidder's shares vote yes; other players accept a premium of 10% or more,
// the chairman 25%; the public in proportion around 10% [I].
Vote merger_vote(const Market& m, PlayerId bidder, CompanyId target, Money offer_per_share,
                 const Balance::Corporate& b);

// Quarterly: the company pays its dividend to every holder; short sellers
// pay it on the shares they borrowed.
void pay_dividends(Market& m, CompanyId c);

} // namespace railmaster::sim
