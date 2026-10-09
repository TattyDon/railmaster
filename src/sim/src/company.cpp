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
    case Ledger::IndustryIncome: return "Industry income";
    case Ledger::TrackageIncome: return "Trackage income";
    case Ledger::TrainMaintenance: return "Train maintenance";
    case Ledger::Fuel: return "Fuel";
    case Ledger::TrackagePaid: return "Trackage paid";
    case Ledger::IndustryCosts: return "Industry costs";
    case Ledger::TrackUpkeep: return "Track upkeep";
    case Ledger::BuildingUpkeep: return "Building upkeep";
    case Ledger::Interest: return "Interest";
    case Ledger::BondFees: return "Bond fees";
    case Ledger::Count: break;
    }
    return "?";
}

bool is_revenue(Ledger line) { return line <= Ledger::TrackageIncome; }

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

Company::Company(std::string name, Money starting_cash, std::int32_t year, const Balance& balance, CompanyId id)
    : name_(std::move(name)), id_(id), finance_(balance.finance), stock_(balance.stock),
      states_(balance.economic_states), corporate_(balance.corporate), cash_(starting_cash),
      shares_(starting_cash.in_cents() / std::max<std::int64_t>(1, balance.stock.founding_share_price_cents)) {
    history_.push_back(YearAccounts{.year = year});
    price_ = shares_ > 0 ? std::max(Money::cents(100), starting_cash.scaled(1, shares_)) : Money::dollars(1);
    history_.back().start_price = price_;
}

const char* sentiment_name(Sentiment s) {
    switch (s) {
    case Sentiment::Happy: return "Happy";
    case Sentiment::Content: return "Content";
    case Sentiment::Grumbling: return "Grumbling";
    case Sentiment::Hostile: return "Hostile";
    }
    return "?";
}

void Company::close_year() {
    YearAccounts& y = history_.back();
    if (y.start_price <= Money{}) return;
    const Money gain = price_ + y.dividends_per_share - y.start_price;
    y.share_return_permille = static_cast<std::int32_t>(gain.in_cents() * 1000 / y.start_price.in_cents());
}

bool Company::bad_year(std::size_t i) const {
    const YearAccounts& y = history_.at(i);
    if (y.profit() < Money{}) return true;
    const bool price_down = y.share_return_permille && *y.share_return_permille < 0;
    return price_down && i > 0 && y.profit() < history_[i - 1].profit();
}

std::int32_t Company::bad_year_streak() const {
    std::int32_t streak = 0;
    for (std::size_t i = history_.size(); i-- > 0;) {
        if (!history_[i].share_return_permille) continue; // the year still open
        if (!bad_year(i)) break;
        ++streak;
    }
    return streak;
}

std::int32_t Company::weighted_return_permille() const {
    std::int64_t sum = 0, weights = 0;
    std::int64_t w = 5;
    for (std::size_t i = history_.size(); i-- > 0 && w > 0;) {
        if (!history_[i].share_return_permille) continue;
        sum += w * *history_[i].share_return_permille;
        weights += w;
        --w;
    }
    return weights > 0 ? static_cast<std::int32_t>(sum / weights) : 0;
}

Sentiment Company::sentiment() const {
    const std::int32_t streak = bad_year_streak();
    if (streak >= corporate_.oust_after_bad_years) return Sentiment::Hostile;
    if (streak >= corporate_.grumble_after_bad_years) return Sentiment::Grumbling;
    if (weighted_return_permille() >= corporate_.happy_return_permille) return Sentiment::Happy;
    return Sentiment::Content;
}

std::optional<std::int32_t> Company::check_split() {
    if (defunct() || price_ < Money::cents(stock_.split_price_cents)) {
        months_above_split_ = 0;
        return std::nullopt;
    }
    if (++months_above_split_ < stock_.split_months) return std::nullopt;
    return price_ >= Money::cents(stock_.big_split_price_cents) ? 3 : 2;
}

void Company::split(std::int32_t ratio) {
    shares_ *= ratio;
    price_ = price_.scaled(1, ratio);
    dividend_ = dividend_.scaled(1, ratio);
    for (YearAccounts& y : history_) {
        y.start_price = y.start_price.scaled(1, ratio);
        y.dividends_per_share = y.dividends_per_share.scaled(1, ratio);
    }
    months_above_split_ = 0;
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

void Company::absorb(Company& target) {
    cash_ += target.cash_;
    track_ += target.track_;
    buildings_ += target.buildings_;
    rolling_stock_ += target.rolling_stock_;
    industries_ += target.industries_;
    bonds_.insert(bonds_.end(), target.bonds_.begin(), target.bonds_.end());
    target.cash_ = target.track_ = target.buildings_ = target.rolling_stock_ = target.industries_ = Money{};
    target.bonds_.clear();
    target.shares_ = 0;
    target.dividend_ = Money{};
    target.price_ = Money{};
    target.merged_into_ = id_;
}

void Company::pay_for_acquisition(Money cost) {
    cash_ -= cost;
    history_.back().acquisitions += cost;
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
    history_.back().dividends_per_share += dividend_.scaled(1, 4);
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

void Company::invest_industry(Money cost) {
    cash_ -= cost;
    industries_ += cost;
    history_.back().industries_bought += cost;
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

std::optional<std::string> Company::bankruptcy_problem() const {
    const std::int32_t year = history_.back().year;
    if (bankrupt_year_ && year - *bankrupt_year_ < finance_.bankruptcy_repeat_years) {
        return "a company cannot go bankrupt twice within " + std::to_string(finance_.bankruptcy_repeat_years) + " years";
    }
    if (bonds_.empty()) return std::string("there is no bond debt to clear");
    std::int32_t losses = 0;
    for (std::size_t i = history_.size() - 1; i-- > 0;) {
        if (history_[i].profit() >= Money{}) break;
        ++losses;
    }
    if (losses < finance_.bankruptcy_loss_years && cash_ >= Money{}) {
        return "bankruptcy needs " + std::to_string(finance_.bankruptcy_loss_years) +
               " loss years in a row, or bills the company cannot pay";
    }
    return std::nullopt;
}

void Company::declare_bankruptcy() {
    Money forgiven;
    for (Bond& b : bonds_) {
        const Money kept = b.principal.scaled(finance_.bankruptcy_debt_kept_percent, 100);
        forgiven += b.principal - kept;
        b.principal = kept;
    }
    // The bondholders are paid in new shares at today's price, which falls
    // in proportion to the dilution.
    const Money price = std::max(price_, Money::cents(stock_.min_share_price_cents));
    const std::int64_t old_shares = shares_;
    const std::int64_t new_shares = forgiven.in_cents() / std::max<std::int64_t>(1, price.in_cents());
    shares_ += new_shares;
    if (shares_ > 0) {
        price_ = std::max(price.scaled(old_shares, shares_), Money::cents(stock_.min_share_price_cents));
    }
    history_.back().debt_forgiven += forgiven;
    bankrupt_year_ = history_.back().year;
}

CreditRating Company::credit_rating() const {
    // Ruined for some years after a bankruptcy [D/I].
    if (bankrupt_year_ && history_.back().year - *bankrupt_year_ < finance_.bankruptcy_rating_years) {
        return CreditRating::D;
    }
    const Money assets = total_assets();
    const std::int64_t leverage_pct =
        assets > Money{} ? debt().in_cents() * 100 / assets.in_cents() : (debt() > Money{} ? 100 : 0);
    int grade = 0;
    while (grade < 7 && leverage_pct >= finance_.rating_leverage_limits[static_cast<std::size_t>(grade)]) ++grade;

    // A young company, with no completed profitable year, is marginal.
    const bool proven = std::any_of(history_.begin(), history_.end() - 1,
                                    [](const YearAccounts& y) { return y.profit() > Money{}; });
    // Each bond outstanding lowers the rating: a notch per bond while the
    // company is unproven (room for one or two), a notch per four once it
    // has a profitable year behind it (so a strong company can reach 20).
    const int bonds = static_cast<int>(bonds_.size());
    if (!proven) grade = std::max(grade, static_cast<int>(CreditRating::BB)) + bonds;
    else grade += bonds / std::max(1, finance_.bonds_per_notch_when_proven);
    return static_cast<CreditRating>(std::min(grade, static_cast<int>(CreditRating::D)));
}

std::int32_t Company::bond_rate_bp() const {
    return std::max(0, finance_.bond_rate_bp[static_cast<std::size_t>(credit_rating())] + prime_offset_bp());
}

void Company::issue_bond(std::int32_t year) {
    const Money face = Money::dollars(finance_.bond_face_value);
    bonds_.push_back({face, bond_rate_bp(), year});
    cash_ += face;
    post(Ledger::BondFees, face.scaled(finance_.bond_underwriting_percent, 100));
}

void Company::repay_bond() {
    if (bonds_.empty()) throw std::logic_error("no bond to repay");
    const auto worst = std::max_element(bonds_.begin(), bonds_.end(),
                                        [](const Bond& a, const Bond& b) { return a.rate_bp < b.rate_bp; });
    cash_ -= worst->principal;
    post(Ledger::BondFees, worst->principal.scaled(finance_.bond_early_repayment_percent, 100));
    bonds_.erase(worst);
}

void Company::retire_matured_bonds(std::int32_t year) {
    for (auto it = bonds_.begin(); it != bonds_.end();) {
        if (year - it->issued_year >= finance_.bond_maturity_years) {
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
    history_.push_back(YearAccounts{.year = year});
    history_.back().start_price = price_;
    issues_this_year_ = 0;
}

} // namespace railmaster::sim
