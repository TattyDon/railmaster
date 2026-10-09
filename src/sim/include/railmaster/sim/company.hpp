#pragma once

#include "railmaster/sim/money.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace railmaster::sim {

// Stand-ins for finance rules the research has not pinned down. Documented
// in docs/spec/m3-finance-model.md.
namespace provisional {
constexpr std::int64_t kStartingCash = 6'000'000;
// Running costs.
constexpr std::int64_t kFuelPerKmSteam = 20;    // dollars per km per train
constexpr std::int64_t kFuelPerKmDiesel = 15;
constexpr std::int64_t kFuelPerKmElectric = 10;
constexpr std::int64_t kFuelPerKmPerCar = 2;
constexpr std::int32_t kTrackUpkeepPerMillePerMonth = 5;    // 0.5% of track cost a month (RT2's rate)
constexpr std::int32_t kBuildingUpkeepPerMillePerMonth = 5; // same for stations and support buildings
// Credit rating: debt as a share of total assets, in percent, at which each grade ends.
constexpr std::int32_t kRatingLeverageLimits[] = {5, 15, 25, 35, 45, 55, 70}; // AAA..C; above: D
// Interest by rating, in basis points a year (AAA..D), in a normal economy.
constexpr std::int32_t kBondRateBp[] = {400, 450, 500, 600, 700, 800, 1000, 1200};
// Shares: the company is founded with this many, at a price equal to its
// starting cash per share; the player holds half.
constexpr std::int64_t kFoundingShares = 600'000;
constexpr std::int64_t kFoundingPlayerShares = 300'000;
} // namespace provisional

// Researched (docs/spec/rt3-clone-spec.md §12.4): bonds are $500,000, need a
// rating of at least B, cost 2% of face value to underwrite and about 2% to
// repay early; at most 20 may be outstanding; interest is paid quarterly.
constexpr std::int64_t kBondFaceValue = 500'000;
constexpr std::int32_t kBondUnderwritingPercent = 2;
constexpr std::int32_t kBondEarlyRepaymentPercent = 2;
constexpr std::size_t kMaxBonds = 20;
namespace provisional {
constexpr std::int32_t kBondMaturityYears = 30; // [I] in the spec
} // namespace provisional

enum class CreditRating : std::uint8_t { AAA, AA, A, BBB, BB, B, C, D };
const char* rating_name(CreditRating r);

struct Bond {
    Money principal;
    std::int32_t rate_bp = 0; // yearly interest, fixed when issued
    std::int32_t issued_year = 0;
};

// The rows of the income statement.
enum class Ledger : std::uint8_t {
    FreightRevenue,
    PassengerRevenue,
    MailRevenue,
    TroopRevenue,
    TrainMaintenance,
    Fuel,
    TrackUpkeep,
    BuildingUpkeep,
    Interest,
    BondFees,
    Count,
};
constexpr std::size_t kLedgerLines = static_cast<std::size_t>(Ledger::Count);
const char* ledger_name(Ledger line);
bool is_revenue(Ledger line);

// One year's income statement, plus what was invested that year.
struct YearAccounts {
    std::int32_t year = 0;
    std::array<Money, kLedgerLines> lines{};
    Money track_built;
    Money buildings_built;
    Money trains_bought;
    Money dividends_paid; // a distribution to shareholders, not an expense

    Money revenue() const;
    Money expenses() const;
    Money profit() const { return revenue() - expenses(); }
};

// The player's railroad company. Assets are carried at what they cost.
class Company {
public:
    Company(std::string name, Money starting_cash, std::int32_t year);

    const std::string& name() const { return name_; }
    Money cash() const { return cash_; }

    // Record money coming in or going out on a ledger line this year.
    void post(Ledger line, Money amount);
    void invest_track(Money cost);
    void invest_buildings(Money cost);
    void invest_train(Money cost);
    // A train destroyed in a crash leaves the books.
    void write_off_train(Money cost) { rolling_stock_ -= cost; }

    Money track_value() const { return track_; }
    Money building_value() const { return buildings_; }
    Money rolling_stock_value() const { return rolling_stock_; }
    Money total_assets() const { return cash_ + track_ + buildings_ + rolling_stock_; }
    Money debt() const;
    Money book_value() const { return total_assets() - debt(); }

    // Grade from debt over total assets. A company yet to finish a
    // profitable year is held to BB at best and loses a notch per bond; a
    // proven one loses a notch per four bonds. Bonds need B or better.
    CreditRating credit_rating() const;
    std::int32_t bond_rate_bp() const;
    const std::vector<Bond>& bonds() const { return bonds_; }
    bool can_issue_bond() const { return credit_rating() <= CreditRating::B && bonds_.size() < kMaxBonds; }
    // Issue one bond: cash in, less the underwriting fee. Caller checks can_issue_bond.
    void issue_bond(std::int32_t year);
    // Repay the most expensive bond early: face value plus the 2% penalty.
    // Caller checks there is one and enough cash.
    void repay_bond();
    // Repay at face value any bond that has reached maturity in `year`.
    void retire_matured_bonds(std::int32_t year);

    // Quarterly: `months` of interest on bonds outstanding.
    void charge_interest(std::int32_t months = 3);

    // --- Shares ---
    std::int64_t shares_outstanding() const { return shares_; }
    Money share_price() const { return price_; }
    void set_share_price(Money p) { price_ = p; }
    Money market_cap() const { return price_ * shares_; }
    Money book_value_per_share() const { return shares_ > 0 ? book_value().scaled(1, shares_) : Money{}; }
    // Annual dividend per share, paid in quarters.
    Money dividend_per_share() const { return dividend_; }
    void set_dividend_per_share(Money d) { dividend_ = d; }
    std::int32_t stock_issues_this_year() const { return issues_this_year_; }
    // Sell `shares` new shares for `proceeds` (an issue) or retire them for
    // `cost` (a buyback). Callers enforce the rules and price impact.
    void issue_shares(std::int64_t shares, Money proceeds);
    void retire_shares(std::int64_t shares, Money cost);
    // Pay a quarter of the annual dividend on every share. Returns the
    // amount paid, or nothing (and cuts the dividend to zero) if the company
    // cannot afford it.
    Money pay_quarterly_dividend();

    // Profit over the last 12 months, annualised from what is available; nullopt if under 3 months of history.
    std::optional<Money> trailing_profit() const;
    // Monthly: remember the running profit total, for trailing_profit().
    void record_month();

    void start_year(std::int32_t year);
    const YearAccounts& this_year() const { return history_.back(); }
    const std::vector<YearAccounts>& history() const { return history_; }

private:
    std::string name_;
    Money cash_;
    Money track_, buildings_, rolling_stock_;
    std::vector<Bond> bonds_;
    std::vector<YearAccounts> history_;
    std::int64_t shares_ = provisional::kFoundingShares;
    Money price_;
    Money dividend_;
    std::int32_t issues_this_year_ = 0;
    Money lifetime_profit_;
    std::vector<Money> month_marks_; // lifetime_profit_ at each month end, newest last
};

} // namespace railmaster::sim
