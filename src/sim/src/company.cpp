#include "railmaster/sim/company.hpp"

#include <algorithm>
#include <stdexcept>

namespace railmaster::sim {

const char* rating_name(CreditRating r) {
    static constexpr const char* kNames[] = {"AAA", "AA", "A", "BBB", "BB", "B", "C", "D"};
    return kNames[static_cast<std::size_t>(r)];
}

const char* ledger_name(Ledger line) {
    switch (line) {
    case Ledger::FreightRevenue: return "Freight revenue";
    case Ledger::PassengerRevenue: return "Passenger revenue";
    case Ledger::MailRevenue: return "Mail revenue";
    case Ledger::TroopRevenue: return "Troop revenue";
    case Ledger::TrainMaintenance: return "Train maintenance";
    case Ledger::Fuel: return "Fuel";
    case Ledger::TrackUpkeep: return "Track upkeep";
    case Ledger::BuildingUpkeep: return "Building upkeep";
    case Ledger::Interest: return "Interest";
    case Ledger::BondFees: return "Bond fees";
    case Ledger::Count: break;
    }
    return "?";
}

bool is_revenue(Ledger line) { return line <= Ledger::TroopRevenue; }

Money YearAccounts::revenue() const {
    Money m;
    for (std::size_t i = 0; i < kLedgerLines; ++i)
        if (is_revenue(static_cast<Ledger>(i))) m += lines[i];
    return m;
}

Money YearAccounts::expenses() const {
    Money m;
    for (std::size_t i = 0; i < kLedgerLines; ++i)
        if (!is_revenue(static_cast<Ledger>(i))) m += lines[i];
    return m;
}

Company::Company(std::string name, Money starting_cash, std::int32_t year)
    : name_(std::move(name)), cash_(starting_cash) {
    history_.push_back(YearAccounts{year, {}, {}, {}, {}, {}});
    price_ = shares_ > 0 ? std::max(Money::cents(100), starting_cash.scaled(1, shares_)) : Money::dollars(1);
}

void Company::post(Ledger line, Money amount) {
    history_.back().lines[static_cast<std::size_t>(line)] += amount;
    if (is_revenue(line)) {
        cash_ += amount;
        lifetime_profit_ += amount;
    } else {
        cash_ -= amount;
        lifetime_profit_ -= amount;
    }
}

void Company::issue_shares(std::int64_t shares, Money proceeds) {
    shares_ += shares;
    cash_ += proceeds;
    ++issues_this_year_;
}

void Company::retire_shares(std::int64_t shares, Money cost) {
    shares_ -= shares;
    cash_ -= cost;
}

Money Company::pay_quarterly_dividend() {
    const Money payout = dividend_.scaled(shares_, 4);
    if (payout <= Money{}) return Money{};
    if (payout > cash_) {
        dividend_ = Money{};
        return Money{};
    }
    cash_ -= payout;
    history_.back().dividends_paid += payout;
    return payout;
}

std::optional<Money> Company::trailing_profit() const {
    const std::size_t months = std::min<std::size_t>(month_marks_.size(), 12);
    if (months < 3) return std::nullopt;
    const Money then = month_marks_.size() > 12 ? month_marks_[month_marks_.size() - 13] : Money{};
    return (lifetime_profit_ - then).scaled(12, static_cast<std::int64_t>(months));
}

void Company::record_month() { month_marks_.push_back(lifetime_profit_); }

void Company::invest_track(Money cost) {
    cash_ -= cost;
    track_ += cost;
    history_.back().track_built += cost;
}

void Company::invest_buildings(Money cost) {
    cash_ -= cost;
    buildings_ += cost;
    history_.back().buildings_built += cost;
}

void Company::invest_train(Money cost) {
    cash_ -= cost;
    rolling_stock_ += cost;
    history_.back().trains_bought += cost;
}

Money Company::debt() const {
    Money d;
    for (const Bond& b : bonds_) d += b.principal;
    return d;
}

CreditRating Company::credit_rating() const {
    const Money assets = total_assets();
    const std::int64_t leverage_pct =
        assets > Money{} ? debt().in_cents() * 100 / assets.in_cents() : (debt() > Money{} ? 100 : 0);
    int grade = 0;
    while (grade < 7 && leverage_pct >= provisional::kRatingLeverageLimits[grade]) ++grade;

    // A young company, with no completed profitable year, is marginal.
    const bool proven = std::any_of(history_.begin(), history_.end() - 1,
                                    [](const YearAccounts& y) { return y.profit() > Money{}; });
    // Each bond outstanding lowers the rating: a notch per bond while the
    // company is unproven (room for one or two), a notch per four once it
    // has a profitable year behind it (so a strong company can reach 20).
    const int bonds = static_cast<int>(bonds_.size());
    if (!proven) grade = std::max(grade, static_cast<int>(CreditRating::BB)) + bonds;
    else grade += bonds / 4;
    return static_cast<CreditRating>(std::min(grade, static_cast<int>(CreditRating::D)));
}

std::int32_t Company::bond_rate_bp() const {
    return provisional::kBondRateBp[static_cast<std::size_t>(credit_rating())];
}

void Company::issue_bond(std::int32_t year) {
    const Money face = Money::dollars(kBondFaceValue);
    bonds_.push_back({face, bond_rate_bp(), year});
    cash_ += face;
    post(Ledger::BondFees, face.scaled(kBondUnderwritingPercent, 100));
}

void Company::repay_bond() {
    if (bonds_.empty()) throw std::logic_error("no bond to repay");
    const auto worst = std::max_element(bonds_.begin(), bonds_.end(),
                                        [](const Bond& a, const Bond& b) { return a.rate_bp < b.rate_bp; });
    cash_ -= worst->principal;
    post(Ledger::BondFees, worst->principal.scaled(kBondEarlyRepaymentPercent, 100));
    bonds_.erase(worst);
}

void Company::retire_matured_bonds(std::int32_t year) {
    for (auto it = bonds_.begin(); it != bonds_.end();) {
        if (year - it->issued_year >= provisional::kBondMaturityYears) {
            cash_ -= it->principal;
            it = bonds_.erase(it);
        } else {
            ++it;
        }
    }
}

void Company::charge_interest(std::int32_t months) {
    Money interest;
    for (const Bond& b : bonds_) interest += b.principal.scaled(std::int64_t{b.rate_bp} * months, 10000 * 12);
    if (interest > Money{}) post(Ledger::Interest, interest);
}

void Company::start_year(std::int32_t year) {
    history_.push_back(YearAccounts{year, {}, {}, {}, {}, {}});
    issues_this_year_ = 0;
}

} // namespace railmaster::sim
