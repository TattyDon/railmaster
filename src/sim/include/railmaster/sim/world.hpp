#pragma once

#include "railmaster/sim/ai.hpp"
#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/commands.hpp"
#include "railmaster/sim/company.hpp"
#include "railmaster/sim/stock.hpp"
#include "railmaster/sim/date.hpp"
#include "railmaster/sim/economy.hpp"
#include "railmaster/sim/freight.hpp"
#include "railmaster/sim/locomotive.hpp"
#include "railmaster/sim/railway.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace railmaster::sim {

struct WorldConfig {
    std::uint64_t seed = 1;
    Date start_date = Date::from_ymd(1830, 1, 1);
    std::int32_t width_tiles = 128;
    std::int32_t height_tiles = 128;
    std::int32_t tile_size_m = 1000; // provisional map scale, see docs/spec/m1-provisional-models.md
    bool sandbox = false;            // sandbox: no breakdowns by default, and money is no object
    // The player founds their company when the world is made, on these
    // terms (default: Balance::Stock's founder investment and the full
    // outside offer). With found_player_company off, the player starts with
    // no company and founds one with a FoundCompany command (the client's
    // founding dialog).
    bool found_player_company = true;
    std::optional<FoundCompany> founding;
    // For tests: the player's company capital in dollars, half of it the
    // player's own (bypasses the founding limits).
    std::optional<std::int64_t> starting_cash;
    bool populate = true;            // place towns and industries (when industry data is present)
    Difficulty difficulty = Difficulty::Medium;
    bool business_cycle = true; // the economic state moves; off holds it at Normal
    std::int32_t rivals = 0;    // AI companies, up to the number of tycoons in the data
    // Scenarios may lock the chair: no firing and no resigning (Go West!).
    bool chairman_can_be_fired = true;
    bool chairman_can_resign = true;
    bool rival_ai = true;       // off: rivals exist but make no decisions (for tests)
};

// Static definitions shared by the whole game, loaded from data/.
struct GameData {
    CargoRegistry cargo{};
    LocomotiveRegistry locomotives{};
    IndustryRegistry industries{};
    Balance balance{}; // data/balance.json
    TycoonRegistry tycoons{};
};

// Root of all simulation state. Advancing it is a pure function of its
// current state; the client changes it only by submitting commands (see
// commands.hpp), which keeps replays and lockstep multiplayer possible.
class World {
public:
    // Fixed simulation steps per game day. Provisional; the real value
    // should come from measuring train movement in the original game.
    static constexpr std::int32_t kTicksPerDay = 16;

    explicit World(const WorldConfig& config, GameData data = {});

    void tick();

    Date date() const { return date_; }
    std::int32_t tick_of_day() const { return tick_of_day_; }
    std::uint64_t total_ticks() const { return total_ticks_; }
    const Terrain& terrain() const { return terrain_; }
    Terrain& terrain() { return terrain_; }
    const GameData& data() const { return data_; }
    Economy& economy() { return economy_; }
    const Economy& economy() const { return economy_; }
    // The company the human player chairs (company 0 unless control changed
    // hands), or, after being fired or resigning, the one they last ran.
    Company& company() { return market_.companies.at(player_company().value_or(last_player_company_)); }
    const Company& company() const { return market_.companies.at(player_company().value_or(last_player_company_)); }
    // The company the human player runs now, if any.
    std::optional<CompanyId> player_company() const { return market_.investors.front().chairs; }
    // Things that happened since the last call, for the news ticker:
    // splits, grumbling investors, chairmen fired, appointed or resigning.
    std::vector<std::string> take_news() { return std::exchange(news_, {}); }
    Company& company(CompanyId id) { return market_.companies.at(id); }
    const Company& company(CompanyId id) const { return market_.companies.at(id); }
    const std::vector<Company>& companies() const { return market_.companies; }
    const std::vector<Investor>& investors() const { return market_.investors; }
    const Market& market() const { return market_; }
    // The AI players, in the order they act each month.
    const std::vector<Rival>& rivals() const { return rivals_; }
    // Found a company for a new player (an AI rival, or for tests). Returns
    // the new player; their company is the next CompanyId.
    PlayerId add_player_company(std::string company_name, std::string chairman);
    bool sandbox() const { return sandbox_; }
    Difficulty difficulty() const { return difficulty_; }
    // Difficulty x station age, in thousandths, for income at a station.
    std::int32_t revenue_permille(StationId s) const;
    Investor& investor() { return market_.investors.front(); }
    const Investor& investor() const { return market_.investors.front(); }
    // Shares sold automatically at the last month end because purchasing
    // power went negative.
    std::int64_t last_forced_sale() const { return last_forced_sale_; }
    Railway& railway() { return railway_; }
    const Railway& railway() const { return railway_; }

    // Apply a command made by player `who`: all of it, or none of it with a reason.
    CommandResult execute(const Command& cmd, PlayerId who = kHumanPlayer);
    // What a BuildTrack would build and cost, without building it.
    PlanResult preview(const BuildTrack& cmd) const;
    // Everything spent through commands so far. A stand-in until companies
    // and ledgers exist (M3).
    Money total_spent() const { return spent_; }
    // Everything trains have earned delivering freight.
    Money total_revenue() const { return earned_; }

    // The business cycle (rt3-clone-spec §5.5): checked a few times a year.
    EconomicState economic_state() const { return economic_state_; }
    void set_economic_state(EconomicState s); // for scenarios and tests
    // Construction, fuel and labour costs, in percent of Normal.
    std::int32_t cost_percent() const;
    Money construction_cost(Money base) const { return base.scaled(cost_percent(), 100); }
    // The state the economy changed to since the last call, if it did: news.
    std::optional<EconomicState> take_economy_news();

private:
    void on_new_day();
    void on_new_month();
    void on_new_year();

    Random rng_;
    Date date_;
    std::int32_t tick_of_day_ = 0;
    std::uint64_t total_ticks_ = 0;
    Terrain terrain_;
    GameData data_;
    Economy economy_;
    Railway railway_;
    Money spent_;
    Money earned_;
    bool sandbox_ = false;
    Difficulty difficulty_ = Difficulty::Medium;
    Market market_;
    std::vector<Rival> rivals_;
    std::vector<std::string> news_;
    CompanyId last_player_company_ = 0;
    bool chairman_can_be_fired_ = true;
    bool chairman_can_resign_ = true;
    std::map<std::pair<PlayerId, CompanyId>, std::int32_t> failed_attempts_; // day of the last failure
    bool rival_ai_ = true;
    PlayerId actor_ = kHumanPlayer; // who the command being run is for
    std::int64_t last_forced_sale_ = 0;
    bool business_cycle_ = true;
    std::optional<EconomicState> economy_news_;
    EconomicState economic_state_ = EconomicState::Normal;
    std::uint64_t economy_terrain_revision_ = 0;

    void refresh_economy_terrain();

    void pay_trackage(Train& t, Money income);
    void run_rivals();
    // The company of the player whose command is being run.
    Company& acting();
    const Company& acting() const;
    bool money_no_object() const; // sandbox, for the human player
    bool owns_track_at(const TrackEnd& at);
    // A track plan with its costs moved by the economic state.
    PlanResult priced(PlanResult r) const;

    CommandResult run(const BuildTrack& cmd);
    CommandResult run(const BuildStation& cmd);
    CommandResult run(const BuildServiceBuilding& cmd);
    CommandResult run(const BuyTrain& cmd);
    CommandResult run(const IssueBond& cmd);
    CommandResult run(const RepayBond& cmd);
    CommandResult run(const BuyShares& cmd);
    CommandResult run(const SellShares& cmd);
    CommandResult run(const IssueStock& cmd);
    CommandResult run(const BuyBackStock& cmd);
    CommandResult run(const SetDividend& cmd);
    CommandResult run(const AttemptTakeover& cmd);
    CommandResult run(const AttemptMerger& cmd);
    CommandResult run(const Resign& cmd);
    CommandResult run(const FoundCompany& cmd);
    CommandResult run(const DeclareBankruptcy& cmd);
    // Why these founding terms are not allowed for player `who`, if they are not.
    std::optional<std::string> founding_problem(PlayerId who, const FoundCompany& terms) const;
    CompanyId found_for(PlayerId who, const FoundCompany& terms);
    // Year end: grumbling, and the vote to fire a chairman after too many bad years.
    void review_chairmen();
    // Give a company with no chairman one: its biggest shareholder who runs
    // nothing else, or a newly arrived tycoon. Never `excluded`.
    void appoint_chairman(CompanyId c, std::optional<PlayerId> excluded);
    void note_player_company();
    // Refuse a second attempt on the same company within a year of a failed one [C].
    std::optional<std::string> too_soon(CompanyId target) const;
    void record_failure(CompanyId target);
    void merge(Company& buyer, CompanyId target, Money offer_per_share);
    // Sandbox games ignore cash; otherwise the company must be able to pay.
    std::optional<std::string> cannot_afford(Money cost) const;
    void charge_running_costs();
    NodeId resolve_on_track(const TrackEnd& at);
};

} // namespace railmaster::sim
