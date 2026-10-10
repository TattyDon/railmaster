#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/economic_state.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/track.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace railmaster::sim {

// Bond rules (docs/spec/rt3-clone-spec.md §12.4): bonds need a rating of
// at least B and pay interest quarterly [D]. Their face value, fees, cap and
// maturity, the rating thresholds and rates, and the founding share count
// are in Balance::Finance and Balance::Stock.

// How shareholders feel about the chairman (rt3-clone-spec §12.2 [D]).
enum class Sentiment : std::uint8_t { Happy, Content, Grumbling, Hostile };
const char* sentiment_name(Sentiment s);

// RT2-style grades, best first (rt3-clone-spec §12.4 [I]); B or better may issue bonds.
enum class CreditRating : std::uint8_t { APlus, A, AMinus, BPlus, B, BMinus, CPlus, C, CMinus, D };
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
    IndustryIncome, // output of the industries it owns [C]
    StationBuildingIncome, // hotels, restaurants and taverns
    TrackageIncome, // rivals' share of income for running on our track [D]
    TrainMaintenance,
    Fuel,
    TrackagePaid,
    IndustryCosts, // their inputs, labour and overhead
    TrackUpkeep,
    BuildingUpkeep,
    Interest,
    BondFees,
    TerritoryFees, // access rights bought [D]
    Count,
};
constexpr std::size_t kLedgerLines = static_cast<std::size_t>(Ledger::Count);
const char* ledger_name(Ledger line);
bool is_revenue(Ledger line);

// One year's income statement, plus what was invested that year.
struct YearAccounts {
    std::int32_t year = 0;
    std::array<Money, kLedgerLines> lines{};
    Money track_built{};
    Money buildings_built{};
    Money trains_bought{};
    Money industries_bought{};
    Money dividends_paid{}; // a distribution to shareholders, not an expense
    Money acquisitions{}; // paid to buy out other companies' shareholders
    Money debt_forgiven{}; // bond debt written off in a bankruptcy
    // For shareholders' return: the share price when the year opened, the
    // dividends paid per share during it, and, once the year is closed, the
    // return they made, in thousandths (price change plus dividends).
    Money start_price{};
    Money dividends_per_share{};
    std::int32_t dividend_quarters = 0; // quarterly dividends paid in the year
    std::optional<std::int32_t> share_return_permille{};

    Money revenue() const;
    Money expenses() const;
    Money profit() const { return revenue() - expenses(); }
};

// The player's railroad company. Assets are carried at what they cost.
class Company {
public:
    // A new company with `starting_cash` of capital, issued as shares at the
    // founding price ($10 by default).
    Company(std::string name, Money starting_cash, std::int32_t year, const Balance& balance = default_balance(),
            CompanyId id = 0);

    CompanyId id() const { return id_; }
    // A company merged into another no longer trades or runs; its id stays
    // reserved so nothing else is renumbered.
    bool defunct() const { return merged_into_.has_value(); }
    std::optional<CompanyId> merged_into() const { return merged_into_; }
    // Take over everything `target` has: cash, track, buildings, trains and
    // bonds [C]. `target` is left an empty shell, merged into this one.
    void absorb(Company& target);
    // New shares for existing holders of something else, with no cash raised
    // (a merger's share exchange).
    void grant_shares(std::int64_t shares) { shares_ += shares; }
    // Pay another company's shareholders in a merger.
    void pay_for_acquisition(Money cost);
    const std::string& name() const { return name_; }
    const Balance::Finance& finance_balance() const { return finance_; }
    const Balance::Stock& stock_balance() const { return stock_; }

    // The business cycle, which sets the prime rate and moves share prices.
    EconomicState economic_state() const { return state_; }
    void set_economic_state(EconomicState s) { state_ = s; }
    std::int32_t prime_rate_bp() const { return states_.prime_rate_bp[index_of(state_)]; }
    // How far the prime rate is above its Normal level (negative in good times).
    std::int32_t prime_offset_bp() const {
        return prime_rate_bp() - states_.prime_rate_bp[index_of(EconomicState::Normal)];
    }
    std::int32_t stock_index_percent() const { return states_.stock_percent[index_of(state_)]; }
    Money cash() const { return cash_; }

    // Record money coming in or going out on a ledger line this year.
    void post(Ledger line, Money amount);
    void invest_track(Money cost);
    void invest_buildings(Money cost);
    void invest_train(Money cost);
    // Buying, building or upgrading an industry.
    void invest_industry(Money cost);
    // A train destroyed in a crash leaves the books.
    void write_off_train(Money cost) { rolling_stock_ -= cost; }

    Money track_value() const { return track_; }
    Money building_value() const { return buildings_; }
    Money rolling_stock_value() const { return rolling_stock_; }
    Money industry_value() const { return industries_; }
    Money total_assets() const { return cash_ + track_ + buildings_ + rolling_stock_ + industries_; }
    Money debt() const;
    Money book_value() const { return total_assets() - debt(); }

    // The rating score (Balance::Finance): asset cover, interest cover,
    // profit record, bonds outstanding and any recent bankruptcy.
    std::int32_t credit_score() const;
    // Graded from credit_score(), then shifted by the territories it holds
    // access to (rt3-clone-spec §3.3 [C]); D for some years after a
    // bankruptcy.
    CreditRating credit_rating() const;

    // Territory access rights (rt3-clone-spec §3.3 [D]), each with the
    // credit-grade shift that territory brings.
    bool has_access(std::uint16_t territory) const;
    void grant_access(std::uint16_t territory, std::int32_t credit_grades);
    const std::vector<std::pair<std::uint16_t, std::int32_t>>& access() const { return access_; }
    // Loads delivered over the company's life, in thousandths, by cargo.
    std::int64_t delivered_milli(std::uint16_t cargo) const {
        return cargo < delivered_.size() ? delivered_[cargo] : 0;
    }
    void record_delivery(std::uint16_t cargo, std::int64_t milli) {
        if (delivered_.size() <= cargo) delivered_.resize(std::size_t{cargo} + 1, 0);
        delivered_[cargo] += milli;
    }
    // For a new bond: the prime rate plus the rating's spread.
    std::int32_t bond_rate_bp() const;
    // Interest a year on the bonds outstanding.
    Money annual_interest() const;
    const std::vector<Bond>& bonds() const { return bonds_; }
    bool can_issue_bond() const { return credit_rating() <= CreditRating::B && bonds_.size() < static_cast<std::size_t>(finance_.max_bonds); }
    // Issue one bond: cash in, less the underwriting fee. Caller checks can_issue_bond.
    void issue_bond(std::int32_t year);
    // Repay the most expensive bond early: face value plus the penalty.
    // Caller checks there is one and enough cash.
    void repay_bond();
    // Repay at face value any bond that has reached maturity in `year`.
    void retire_matured_bonds(std::int32_t year);

    // Bankruptcy [D]: why it is not allowed now, if it is not. It needs bonds,
    // and either loss years in a row or no cash to pay with, and none in the
    // last few years.
    std::optional<std::string> bankruptcy_problem() const;
    // Halve every bond; the bondholders take new shares for what they lose,
    // diluting the price. The rating stays at D for some years.
    void declare_bankruptcy();
    std::optional<std::int32_t> bankrupt_year() const { return bankrupt_year_; }

    // Quarterly: `months` of interest on bonds outstanding.
    void charge_interest(std::int32_t months = 3);

    // --- Shares ---
    std::int64_t shares_outstanding() const { return shares_; }
    Money share_price() const { return price_; }
    void set_share_price(Money p) { price_ = p; }
    // The part of the price that trading put there, which fades [C].
    Money trade_pressure() const { return pressure_; }
    // A trade moved the price by `move`: set it and remember the pressure.
    void move_price_by_trade(Money price, Money move) {
        price_ = price;
        pressure_ += move;
    }
    // Monthly: move the price 1/price_smoothing of the way from where
    // fundamentals had it to `value`, and let trade pressure fade.
    void settle_price(Money value, Money floor);
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
    // Revenue likewise.
    std::optional<Money> trailing_revenue() const;
    // Earnings per share, weighting the trailing 12 months, last year and
    // the year before by Balance::Stock::eps_trend_weights; nullopt with no
    // earnings history yet.
    std::optional<Money> eps_trend() const;
    // Closed years in a row, most recent last, in which all four quarterly
    // dividends were paid; zero if no dividend is set now.
    std::int32_t unbroken_dividend_years() const;
    // Monthly: remember the running profit total, for trailing_profit().
    void record_month();

    // At year end, before start_year: record the shareholders' return.
    void close_year();
    void start_year(std::int32_t year);

    // A bad year: a loss, or profit and share price both down [I].
    bool bad_year(std::size_t index) const;
    // Closed years in a row, most recent first, that were bad.
    std::int32_t bad_year_streak() const;
    // Return over up to five closed years, the latest weighted 5, then 4, 3,
    // 2, 1, in thousandths; zero before the first year closes.
    std::int32_t weighted_return_permille() const;
    Sentiment sentiment() const;

    // Monthly: count month ends at a high price; returns the split ratio
    // (2 or 3) when one is due.
    std::optional<std::int32_t> check_split();
    // Split every share into `ratio`: the price, dividend and per-share
    // history fall in proportion. Holders' shares are the market's to adjust.
    void split(std::int32_t ratio);
    const YearAccounts& this_year() const { return history_.back(); }
    const std::vector<YearAccounts>& history() const { return history_; }

private:
    std::string name_;
    CompanyId id_ = 0;
    std::optional<CompanyId> merged_into_;
    Balance::Finance finance_;
    Balance::Stock stock_;
    Balance::EconomicStates states_;
    Balance::Corporate corporate_;
    std::int32_t months_above_split_ = 0;
    std::optional<std::int32_t> bankrupt_year_;
    EconomicState state_ = EconomicState::Normal;
    Money cash_;
    Money track_, buildings_, rolling_stock_;
    Money industries_;
    std::vector<Bond> bonds_;
    std::vector<std::pair<std::uint16_t, std::int32_t>> access_; // territory, credit grades
    std::vector<std::int64_t> delivered_;                        // by cargo, milli
    std::vector<YearAccounts> history_;
    std::int64_t shares_ = 0;
    Money price_;
    Money dividend_;
    std::int32_t issues_this_year_ = 0;
    Money pressure_;
    Money lifetime_profit_, lifetime_revenue_;
    // lifetime_profit_ and lifetime_revenue_ at each month end, newest last
    std::vector<Money> month_marks_, revenue_marks_;
};

} // namespace railmaster::sim
