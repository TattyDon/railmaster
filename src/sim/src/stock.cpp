#include "railmaster/sim/stock.hpp"

#include <algorithm>

namespace railmaster::sim {

namespace {

Money floor_price(const Company& c, Money p) { return std::max(p, Money::cents(c.stock_balance().min_share_price_cents)); }

std::int64_t block_size(const Company& c) { return std::max<std::int64_t>(1, c.stock_balance().share_block); }

std::string dollars(Money m) {
    const std::string digits = std::to_string(std::max<std::int64_t>(0, m.whole_dollars()));
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return "$" + out;
}

std::int64_t issue_size(const Company& c) {
    const std::int64_t block = block_size(c);
    return std::max<std::int64_t>(block, c.shares_outstanding() * c.stock_balance().issue_percent / 100 / block * block);
}

} // namespace

Money holdings_value(const Investor& inv, const Company& c) { return c.share_price() * inv.shares; }

Money net_worth(const Investor& inv, const Company& c) { return inv.cash + holdings_value(inv, c); }

Money purchasing_power(const Investor& inv, const Company& c) {
    return inv.cash + holdings_value(inv, c).scaled(c.stock_balance().margin_percent, 100);
}

std::int64_t public_float(const Investor& inv, const Company& c) { return c.shares_outstanding() - inv.shares; }

Money trade_with_impact(Company& c, std::int64_t shares, bool buying) {
    Money total;
    const std::int64_t lot = block_size(c);
    for (std::int64_t done = 0; done < shares; done += lot) {
        const std::int64_t block = std::min(lot, shares - done);
        const Money move = c.share_price().scaled(block * c.stock_balance().impact_per_share_of_company,
                                                  std::max<std::int64_t>(1, c.shares_outstanding()));
        c.set_share_price(floor_price(c, buying ? c.share_price() + move : c.share_price() - move));
        total += c.share_price() * block;
    }
    return total;
}

std::optional<std::string> buy_shares(Investor& inv, Company& c, std::int64_t blocks) {
    const std::int64_t shares = blocks * block_size(c);
    if (blocks <= 0) return "nothing to buy";
    if (shares > public_float(inv, c)) return "not enough shares on the market";
    // Price the trade without committing to it, and check it against what
    // could be raised *before* it. Valuing holdings after the trade would let
    // buying, which lifts the price, pay for itself.
    Company trial = c;
    const Money cost = trade_with_impact(trial, shares, true);
    if (cost > purchasing_power(inv, c)) {
        return "not enough purchasing power: costs " + dollars(cost) + ", you can raise " +
               dollars(purchasing_power(inv, c));
    }
    c = trial;
    inv.cash -= cost;
    inv.shares += shares;
    return std::nullopt;
}

std::optional<std::string> sell_shares(Investor& inv, Company& c, std::int64_t blocks) {
    const std::int64_t shares = blocks * block_size(c);
    if (blocks <= 0) return "nothing to sell";
    if (shares > inv.shares) return "you do not hold that many shares";
    inv.cash += trade_with_impact(c, shares, false);
    inv.shares -= shares;
    return std::nullopt;
}

std::optional<std::string> issue_stock(const Investor&, Company& c) {
    if (c.stock_issues_this_year() >= kMaxStockIssuesPerYear) return "stock can be issued at most twice a year";
    const std::int64_t shares = issue_size(c);
    const Money proceeds = trade_with_impact(c, shares, false); // new supply pushes the price down
    c.issue_shares(shares, proceeds);
    return std::nullopt;
}

std::optional<std::string> buy_back_stock(const Investor& inv, Company& c) {
    const std::int64_t shares = std::min(issue_size(c), public_float(inv, c) / block_size(c) * block_size(c));
    if (shares <= 0) return "no shares in public hands to buy back";
    Company trial = c;
    const Money cost = trade_with_impact(trial, shares, true);
    if (cost > c.cash()) return "the company cannot afford a buyback (" + dollars(cost) + ")";
    c = trial;
    c.retire_shares(shares, cost);
    return std::nullopt;
}

Money target_share_price(const Company& c) {
    const std::int64_t n = std::max<std::int64_t>(1, c.shares_outstanding());
    const Balance::Stock& b = c.stock_balance();
    Money target = c.book_value_per_share().scaled(b.book_weight_percent, 100);
    if (const auto profit = c.trailing_profit()) target += profit->scaled(b.earnings_multiple, n);
    target += c.dividend_per_share() * b.dividend_multiple;
    return floor_price(c, target);
}

std::int64_t monthly_market(Investor& inv, Company& c) {
    const Money target = target_share_price(c);
    const Balance::Stock& b = c.stock_balance();
    c.set_share_price(floor_price(c, c.share_price() + (target - c.share_price()).scaled(b.price_adjust_percent, 100)));

    inv.cash += Money::dollars(b.salary_per_year).scaled(1, 12);
    if (inv.cash < Money{}) inv.cash -= (-inv.cash).scaled(b.margin_interest_bp, 10000 * 12);

    std::int64_t sold = 0;
    const std::int64_t lot = block_size(c);
    while (purchasing_power(inv, c) < Money{} && inv.shares >= lot) {
        sell_shares(inv, c, 1);
        sold += lot;
    }
    return sold;
}

void pay_dividends(Investor& inv, Company& c) {
    const Money paid = c.pay_quarterly_dividend();
    if (paid > Money{} && c.shares_outstanding() > 0) inv.cash += paid.scaled(inv.shares, c.shares_outstanding());
}

} // namespace railmaster::sim
