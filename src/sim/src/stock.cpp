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

bool valid(const Market& m, PlayerId who, CompanyId c) { return who < m.investors.size() && c < m.companies.size(); }

} // namespace

void Investor::add_shares(CompanyId c, std::int64_t n) {
    if (holdings.size() <= c) holdings.resize(std::size_t{c} + 1, 0);
    holdings[c] += n;
}

Investor Investor::founder(std::string name, CompanyId company, const Balance& b) {
    Investor inv;
    inv.name = std::move(name);
    inv.cash = Money::dollars(b.stock.starting_personal_cash);
    inv.add_shares(company, b.stock.founding_player_shares);
    inv.chairs = company;
    return inv;
}

Money holdings_value(const Investor& inv, const Market& m) {
    Money total;
    for (const Company& c : m.companies) total += c.share_price() * inv.shares_in(c.id());
    return total;
}

Money net_worth(const Investor& inv, const Market& m) { return inv.cash + holdings_value(inv, m); }

Money purchasing_power(const Investor& inv, const Market& m) {
    Money borrowable;
    for (const Company& c : m.companies) {
        borrowable += (c.share_price() * inv.shares_in(c.id())).scaled(c.stock_balance().margin_percent, 100);
    }
    return inv.cash + borrowable;
}

std::int64_t public_float(const Market& m, CompanyId c) {
    std::int64_t held = 0;
    for (const Investor& inv : m.investors) held += inv.shares_in(c);
    return m.companies.at(c).shares_outstanding() - held;
}

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

std::optional<std::string> buy_shares(Market& m, PlayerId who, CompanyId cid, std::int64_t blocks) {
    if (!valid(m, who, cid)) return "no such company";
    Company& c = m.companies[cid];
    Investor& inv = m.investors[who];
    const std::int64_t shares = blocks * block_size(c);
    if (blocks <= 0) return "nothing to buy";
    if (shares > public_float(m, cid)) return "not enough shares on the market";
    // Price the trade without committing to it, and check it against what
    // could be raised *before* it. Valuing holdings after the trade would let
    // buying, which lifts the price, pay for itself.
    Company trial = c;
    const Money cost = trade_with_impact(trial, shares, true);
    const Money power = purchasing_power(inv, m);
    if (cost > power) {
        return "not enough purchasing power: costs " + dollars(cost) + ", you can raise " + dollars(power);
    }
    c = trial;
    inv.cash -= cost;
    inv.add_shares(cid, shares);
    return std::nullopt;
}

std::optional<std::string> sell_shares(Market& m, PlayerId who, CompanyId cid, std::int64_t blocks) {
    if (!valid(m, who, cid)) return "no such company";
    Company& c = m.companies[cid];
    Investor& inv = m.investors[who];
    const std::int64_t shares = blocks * block_size(c);
    if (blocks <= 0) return "nothing to sell";
    if (shares > inv.shares_in(cid)) return "you do not hold that many shares";
    inv.cash += trade_with_impact(c, shares, false);
    inv.add_shares(cid, -shares);
    return std::nullopt;
}

std::optional<std::string> issue_stock(Company& c) {
    if (c.stock_issues_this_year() >= kMaxStockIssuesPerYear) return "stock can be issued at most twice a year";
    const std::int64_t shares = issue_size(c);
    const Money proceeds = trade_with_impact(c, shares, false); // new supply pushes the price down
    c.issue_shares(shares, proceeds);
    return std::nullopt;
}

std::optional<std::string> buy_back_stock(Market& m, CompanyId cid) {
    Company& c = m.companies.at(cid);
    const std::int64_t shares = std::min(issue_size(c), public_float(m, cid) / block_size(c) * block_size(c));
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
    // Good times lift every share price and bad times depress it [C].
    return floor_price(c, target.scaled(c.stock_index_percent(), 100));
}

std::vector<std::int64_t> monthly_market(Market& m) {
    for (Company& c : m.companies) {
        const Money target = target_share_price(c);
        const Balance::Stock& b = c.stock_balance();
        c.set_share_price(floor_price(c, c.share_price() + (target - c.share_price()).scaled(b.price_adjust_percent, 100)));
    }

    std::vector<std::int64_t> sold(m.investors.size(), 0);
    if (m.companies.empty()) return sold;
    // Personal rates follow the first company's balance and the economy.
    const Company& ref = m.companies.front();
    const Balance::Stock& b = ref.stock_balance();
    for (std::size_t i = 0; i < m.investors.size(); ++i) {
        Investor& inv = m.investors[i];
        if (inv.chairs) inv.cash += Money::dollars(b.salary_per_year).scaled(1, 12);
        if (inv.cash < Money{}) {
            inv.cash -= (-inv.cash).scaled(std::max(0, b.margin_interest_bp + ref.prime_offset_bp()), 10000 * 12);
        }
        // Margin call: sell a block of the most valuable holding until purchasing power is back.
        while (purchasing_power(inv, m) < Money{}) {
            std::optional<CompanyId> pick;
            Money best;
            for (const Company& c : m.companies) {
                if (inv.shares_in(c.id()) < block_size(c)) continue;
                const Money value = c.share_price() * inv.shares_in(c.id());
                if (!pick || value > best) {
                    pick = c.id();
                    best = value;
                }
            }
            if (!pick) break;
            sell_shares(m, static_cast<PlayerId>(i), *pick, 1);
            sold[i] += block_size(m.companies[*pick]);
        }
    }
    return sold;
}

void pay_dividends(Market& m, CompanyId cid) {
    Company& c = m.companies.at(cid);
    const std::int64_t outstanding = c.shares_outstanding();
    const Money paid = c.pay_quarterly_dividend();
    if (paid <= Money{} || outstanding <= 0) return;
    for (Investor& inv : m.investors) {
        if (const std::int64_t held = inv.shares_in(cid); held > 0) inv.cash += paid.scaled(held, outstanding);
    }
}

} // namespace railmaster::sim
