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

// The broker's commission on a trade worth `value` [I].
Money brokerage(const Company& c, Money value) { return value.scaled(c.stock_balance().brokerage_permille, 1000); }

// Buy back borrowed shares, with no checks: covering a short.
void cover(Market& m, PlayerId who, CompanyId cid, std::int64_t shares) {
    Investor& inv = m.investors[who];
    const Money cost = trade_with_impact(m.companies[cid], shares, true);
    inv.cash -= cost + brokerage(m.companies[cid], cost);
    inv.add_shares(cid, shares);
}

Money short_value(const Investor& inv, const Market& m) {
    Money total;
    for (const Company& c : m.companies) {
        if (const std::int64_t h = inv.shares_in(c.id()); h < 0) total += c.share_price() * -h;
    }
    return total;
}

} // namespace

void Investor::add_shares(CompanyId c, std::int64_t n) {
    if (holdings.size() <= c) holdings.resize(std::size_t{c} + 1, 0);
    holdings[c] += n;
}

Investor Investor::founder(std::string name, CompanyId company, const Balance& b, std::optional<Money> investment) {
    const Money put_in = investment.value_or(Money::dollars(b.stock.founder_investment));
    Investor inv;
    inv.name = std::move(name);
    inv.cash = Money::dollars(b.stock.founder_fortune) - put_in;
    inv.add_shares(company, put_in.in_cents() / std::max<std::int64_t>(1, b.stock.founding_share_price_cents));
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
    // Longs can be borrowed against at the margin rate; shorts are a debt
    // in shares, held against at more than their value.
    Money power = inv.cash;
    for (const Company& c : m.companies) {
        const std::int64_t h = inv.shares_in(c.id());
        if (h > 0) power += (c.share_price() * h).scaled(c.stock_balance().margin_percent, 100);
        if (h < 0) power -= (c.share_price() * -h).scaled(c.stock_balance().short_margin_percent, 100);
    }
    return power;
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
    if (c.defunct()) return "that company no longer exists";
    const std::int64_t shares = blocks * block_size(c);
    if (blocks <= 0) return "nothing to buy";
    if (shares > public_float(m, cid)) return "not enough shares on the market";
    // Buying back shares sold short is always allowed: it can only reduce risk.
    if (inv.shares_in(cid) < 0 && shares <= -inv.shares_in(cid)) {
        cover(m, who, cid, shares);
        return std::nullopt;
    }
    // Price the trade without committing to it, and check it against what
    // could be raised *before* it. Valuing holdings after the trade would let
    // buying, which lifts the price, pay for itself.
    Company trial = c;
    const Money cost = trade_with_impact(trial, shares, true);
    const Money total = cost + brokerage(c, cost);
    const Money power = purchasing_power(inv, m);
    if (total > power) {
        return "not enough purchasing power: costs " + dollars(total) + " with brokerage, you can raise " +
               dollars(power);
    }
    c = trial;
    inv.cash -= total;
    inv.add_shares(cid, shares);
    return std::nullopt;
}

std::optional<std::string> sell_shares(Market& m, PlayerId who, CompanyId cid, std::int64_t blocks) {
    if (!valid(m, who, cid)) return "no such company";
    Company& c = m.companies[cid];
    Investor& inv = m.investors[who];
    if (c.defunct()) return "that company no longer exists";
    const std::int64_t shares = blocks * block_size(c);
    if (blocks <= 0) return "nothing to sell";
    // Selling more than you hold is selling short: borrowing shares to sell.
    const std::int64_t shorted = shares - std::max<std::int64_t>(0, inv.shares_in(cid));
    if (shorted > 0) {
        if (inv.chairs == cid) return "you cannot sell your own company short";
        const Money worth = net_worth(inv, m);
        const Money cap = worth.scaled(c.stock_balance().short_cap_percent_of_net_worth, 100);
        const Money after = short_value(inv, m) + c.share_price() * shorted;
        if (worth <= Money{} || after > cap) {
            return "short positions may not exceed " +
                   std::to_string(c.stock_balance().short_cap_percent_of_net_worth) + "% of your net worth (" +
                   dollars(cap) + ")";
        }
    }
    const Money proceeds = trade_with_impact(c, shares, false);
    inv.cash += proceeds - brokerage(c, proceeds);
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

Money chairman_salary(const Company& c) {
    const Balance::Stock& b = c.stock_balance();
    const std::int64_t pct = std::clamp<std::int64_t>(
        100 + std::int64_t{b.salary_return_factor_percent} * c.weighted_return_permille() / 1000, b.salary_min_percent,
        b.salary_max_percent);
    return Money::dollars(b.salary_per_year).scaled(pct, 100);
}

std::vector<std::pair<CompanyId, std::int32_t>> apply_splits(Market& m) {
    std::vector<std::pair<CompanyId, std::int32_t>> done;
    for (Company& c : m.companies) {
        const auto ratio = c.check_split();
        if (!ratio) continue;
        c.split(*ratio);
        for (Investor& inv : m.investors) {
            const std::int64_t h = inv.shares_in(c.id());
            if (h != 0) inv.add_shares(c.id(), h * (*ratio - 1));
        }
        done.emplace_back(c.id(), *ratio);
    }
    return done;
}

std::vector<std::int64_t> monthly_market(Market& m) {
    for (Company& c : m.companies) {
        if (c.defunct()) continue;
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
        if (inv.chairs) inv.cash += chairman_salary(m.companies.at(*inv.chairs)).scaled(1, 12);
        if (inv.cash < Money{}) {
            inv.cash -= (-inv.cash).scaled(std::max(0, b.margin_interest_bp + ref.prime_offset_bp()), 10000 * 12);
        }
        // Margin call: unwind the biggest position, long or short, a block at
        // a time until purchasing power is back.
        while (purchasing_power(inv, m) < Money{}) {
            std::optional<CompanyId> pick;
            Money best;
            for (const Company& c : m.companies) {
                const std::int64_t h = inv.shares_in(c.id());
                if (c.defunct() || (h > 0 && h < block_size(c)) || h == 0) continue;
                const Money value = c.share_price() * (h < 0 ? -h : h);
                if (!pick || value > best) {
                    pick = c.id();
                    best = value;
                }
            }
            if (!pick) break;
            const auto who = static_cast<PlayerId>(i);
            const std::int64_t lot = block_size(m.companies[*pick]);
            const std::int64_t h = inv.shares_in(*pick);
            if (h > 0) sell_shares(m, who, *pick, 1);
            else cover(m, who, *pick, std::min(lot, -h));
            sold[i] += h > 0 ? lot : std::min(lot, -h);
        }
    }
    return sold;
}

namespace {

// Shares in public hands: all those no player holds (shorted shares are among them).
std::int64_t public_shares(const Market& m, CompanyId cid) {
    std::int64_t held = 0;
    for (const Investor& inv : m.investors) held += std::max<std::int64_t>(0, inv.shares_in(cid));
    return std::max<std::int64_t>(0, m.companies.at(cid).shares_outstanding() - held);
}

bool is_chairman(const Investor& inv, CompanyId cid) { return inv.chairs && *inv.chairs == cid; }

} // namespace

Vote takeover_vote(const Market& m, PlayerId bidder, CompanyId cid, const Balance::Corporate& b) {
    const Company& c = m.companies.at(cid);
    const auto& years = c.history();
    std::int32_t support = b.takeover_base_support_permille;
    if (c.share_price() < c.book_value_per_share()) support += b.takeover_below_book_permille;
    if (years.size() > 1 && years[years.size() - 2].profit() < Money{}) support += b.takeover_loss_year_permille;
    support = std::clamp(support, 0, 1000);

    Vote v{0, c.shares_outstanding()};
    for (std::size_t i = 0; i < m.investors.size(); ++i) {
        const Investor& inv = m.investors[i];
        const std::int64_t h = std::max<std::int64_t>(0, inv.shares_in(cid));
        if (i == bidder) v.yes += h;
        else if (!is_chairman(inv, cid) && support >= 500) v.yes += h;
    }
    v.yes += public_shares(m, cid) * support / 1000;
    return v;
}

Vote merger_vote(const Market& m, PlayerId bidder, CompanyId cid, Money offer, const Balance::Corporate& b) {
    const Company& c = m.companies.at(cid);
    const std::int64_t ratio = c.share_price() > Money{} ? offer.in_cents() * 1000 / c.share_price().in_cents() : 0;
    const std::int64_t neutral = std::int64_t{b.merger_neutral_premium_percent} * 10;
    const std::int64_t support = std::clamp<std::int64_t>(500 + (ratio - neutral) * b.merger_support_slope, 0, 1000);

    Vote v{0, c.shares_outstanding()};
    for (std::size_t i = 0; i < m.investors.size(); ++i) {
        const Investor& inv = m.investors[i];
        const std::int64_t h = std::max<std::int64_t>(0, inv.shares_in(cid));
        const std::int64_t needs = is_chairman(inv, cid) ? std::int64_t{b.merger_chairman_premium_percent} * 10 : neutral;
        if (i == bidder || ratio >= needs) v.yes += h;
    }
    v.yes += public_shares(m, cid) * support / 1000;
    return v;
}

void pay_dividends(Market& m, CompanyId cid) {
    Company& c = m.companies.at(cid);
    const std::int64_t outstanding = c.shares_outstanding();
    const Money paid = c.pay_quarterly_dividend();
    if (paid <= Money{} || outstanding <= 0) return;
    for (Investor& inv : m.investors) {
        const std::int64_t held = inv.shares_in(cid);
        if (held > 0) inv.cash += paid.scaled(held, outstanding);
        // A short seller owes the dividend on the shares borrowed.
        if (held < 0) inv.cash -= paid.scaled(-held, outstanding);
    }
}

} // namespace railmaster::sim
