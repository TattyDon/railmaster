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

// Each returns an error message, or nullopt on success.
std::optional<std::string> buy_shares(Market& m, PlayerId who, CompanyId c, std::int64_t blocks);
std::optional<std::string> sell_shares(Market& m, PlayerId who, CompanyId c, std::int64_t blocks);
std::optional<std::string> issue_stock(Company& c);
std::optional<std::string> buy_back_stock(Market& m, CompanyId c);

// Monthly: move every price towards its value; pay chairmen their salary;
// charge margin interest; sell shares of anyone whose purchasing power has
// gone negative. Returns the shares force-sold, by player.
std::vector<std::int64_t> monthly_market(Market& m);
Money target_share_price(const Company& c);

// Quarterly: the company pays its dividend to every holder.
void pay_dividends(Market& m, CompanyId c);

} // namespace railmaster::sim
