#pragma once

#include "railmaster/sim/money.hpp"

#include <array>
#include <cstdint>
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
} // namespace provisional

// Researched: bonds are $500,000, need a rating of at least B, and cost 2%
// of face value to underwrite.
constexpr std::int64_t kBondFaceValue = 500'000;
constexpr std::int32_t kBondUnderwritingPercent = 2;

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

    Money track_value() const { return track_; }
    Money building_value() const { return buildings_; }
    Money rolling_stock_value() const { return rolling_stock_; }
    Money total_assets() const { return cash_ + track_ + buildings_ + rolling_stock_; }
    Money debt() const;
    Money book_value() const { return total_assets() - debt(); }

    // Grade from debt over total assets; a company yet to finish a
    // profitable year is held to BB at best; each bond outstanding costs a
    // further notch. Bonds need B or better.
    CreditRating credit_rating() const;
    std::int32_t bond_rate_bp() const;
    const std::vector<Bond>& bonds() const { return bonds_; }
    bool can_issue_bond() const { return credit_rating() <= CreditRating::B; }
    // Issue one bond: cash in, less the underwriting fee. Caller checks can_issue_bond.
    void issue_bond(std::int32_t year);
    // Repay the most expensive bond at face value. Caller checks there is
    // one and enough cash.
    void repay_bond();

    // Monthly: interest on bonds outstanding.
    void charge_interest();

    void start_year(std::int32_t year);
    const YearAccounts& this_year() const { return history_.back(); }
    const std::vector<YearAccounts>& history() const { return history_; }

private:
    std::string name_;
    Money cash_;
    Money track_, buildings_, rolling_stock_;
    std::vector<Bond> bonds_;
    std::vector<YearAccounts> history_;
};

} // namespace railmaster::sim
