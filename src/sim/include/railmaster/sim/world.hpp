#pragma once

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
#include <optional>

namespace railmaster::sim {

struct WorldConfig {
    std::uint64_t seed = 1;
    Date start_date = Date::from_ymd(1830, 1, 1);
    std::int32_t width_tiles = 128;
    std::int32_t height_tiles = 128;
    std::int32_t tile_size_m = 1000; // provisional map scale, see docs/spec/m1-provisional-models.md
    bool sandbox = false;            // sandbox: no breakdowns by default, and money is no object
    // Company's opening cash in dollars; by default Balance::Finance::starting_cash.
    std::optional<std::int64_t> starting_cash;
    bool populate = true;            // place towns and industries (when industry data is present)
    Difficulty difficulty = Difficulty::Medium;
    bool business_cycle = true; // the economic state moves; off holds it at Normal
};

// Static definitions shared by the whole game, loaded from data/.
struct GameData {
    CargoRegistry cargo{};
    LocomotiveRegistry locomotives{};
    IndustryRegistry industries{};
    Balance balance{}; // data/balance.json
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
    Company& company() { return company_; }
    const Company& company() const { return company_; }
    bool sandbox() const { return sandbox_; }
    Difficulty difficulty() const { return difficulty_; }
    // Difficulty x station age, in thousandths, for income at a station.
    std::int32_t revenue_permille(StationId s) const;
    Investor& investor() { return investor_; }
    const Investor& investor() const { return investor_; }
    // Shares sold automatically at the last month end because purchasing
    // power went negative.
    std::int64_t last_forced_sale() const { return last_forced_sale_; }
    Railway& railway() { return railway_; }
    const Railway& railway() const { return railway_; }

    // Apply a player command: all of it, or none of it with a reason.
    CommandResult execute(const Command& cmd);
    // What a BuildTrack would build and cost, without building it.
    PlanResult preview(const BuildTrack& cmd) const;
    // Everything spent through commands so far. A stand-in until companies
    // and ledgers exist (M3).
    Money total_spent() const { return spent_; }
    // Everything trains have earned delivering freight.
    Money total_revenue() const { return earned_; }

    // The business cycle (rt3-clone-spec §5.5): checked a few times a year.
    EconomicState economic_state() const { return company_.economic_state(); }
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
    Company company_;
    Investor investor_;
    std::int64_t last_forced_sale_ = 0;
    bool business_cycle_ = true;
    std::optional<EconomicState> economy_news_;
    std::uint64_t economy_terrain_revision_ = 0;

    void refresh_economy_terrain();
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
    // Sandbox games ignore cash; otherwise the company must be able to pay.
    std::optional<std::string> cannot_afford(Money cost) const;
    void charge_running_costs();
    NodeId resolve_on_track(const TrackEnd& at);
};

} // namespace railmaster::sim
