#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/economic_state.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/track.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace railmaster::sim {

// Bond rules (docs/spec/rt3-clone-spec.md §12.4): bonds need a rating of
// at least B and pay interest quarterly [D]. Their face value, fees, cap and
// maturity, the rating thresholds and rates, and the founding share count
// are in Balance::Finance and Balance::Stock.

// How shareholders feel about the chairman (rt3-clone-spec §12.2 [D]).
enum class Sentiment : std::uint8_t { Happy, Content, Grumbling, Hostile };
const char* sentiment_name(Sentiment s);

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
    TrackageIncome, // rivals' share of income for running on our track [D]
    TrainMaintenance,
    Fuel,
    TrackagePaid,
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
    Money acquisitions;   // paid to buy out other companies' shareholders
    Money debt_forgiven{}; // bond debt written off in a bankruptcy
    // For shareholders' return: the share price when the year opened, the
    // dividends paid per share during it, and, once the year is closed, the
    // return they made, in thousandths (price change plus dividends).
    Money start_price{};
    Money dividends_per_share{};
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
    // For a new bond: the rating's rate, moved with the prime rate.
    std::int32_t bond_rate_bp() const;
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
    std::vector<Bond> bonds_;
    std::vector<YearAccounts> history_;
    std::int64_t shares_ = 0;
    Money price_;
    Money dividend_;
    std::int32_t issues_this_year_ = 0;
    Money lifetime_profit_;
    std::vector<Money> month_marks_; // lifetime_profit_ at each month end, newest last
};

} // namespace railmaster::sim
