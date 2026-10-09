#pragma once

#include "railmaster/sim/company.hpp"
#include "railmaster/sim/money.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace railmaster::sim {

// The stock market and the player's own money. Researched rules are in
// docs/spec/overview-finance-scenarios.md §4.1 and §4.3; the rest is
// documented in docs/spec/m3-finance-model.md.

// Documented [D]: stock may be issued at most twice a year. The share block,
// margin, salary and price model are in Balance::Stock, read from the
// company each function is given.
constexpr std::int32_t kMaxStockIssuesPerYear = 2;

// The player as a private investor.
struct Investor {
    Money cash = Money::dollars(default_balance().stock.starting_personal_cash);
    std::int64_t shares = default_balance().stock.founding_player_shares; // in the player's company

    static Investor founder(const Balance& b) {
        return {Money::dollars(b.stock.starting_personal_cash), b.stock.founding_player_shares};
    }
};

Money holdings_value(const Investor& inv, const Company& c);
Money net_worth(const Investor& inv, const Company& c);
// Cash plus what can be borrowed against shares held. Buying needs this to
// cover the cost; if it goes negative, shares are sold automatically.
Money purchasing_power(const Investor& inv, const Company& c);
std::int64_t public_float(const Investor& inv, const Company& c);

// Move the price for trading `shares` (one block at a time); returns the
// total paid or received, each block at the price after its own impact.
Money trade_with_impact(Company& c, std::int64_t shares, bool buying);

// Each returns an error message, or nullopt on success.
std::optional<std::string> buy_shares(Investor& inv, Company& c, std::int64_t blocks);
std::optional<std::string> sell_shares(Investor& inv, Company& c, std::int64_t blocks);
std::optional<std::string> issue_stock(const Investor& inv, Company& c);
std::optional<std::string> buy_back_stock(const Investor& inv, Company& c);

// Monthly: move the price towards its value; pay salary; charge margin
// interest; sell shares if purchasing power has gone negative. Returns the
// number of shares force-sold.
std::int64_t monthly_market(Investor& inv, Company& c);
Money target_share_price(const Company& c);

// Quarterly: the company pays its dividend, and the player gets their share.
void pay_dividends(Investor& inv, Company& c);

} // namespace railmaster::sim
