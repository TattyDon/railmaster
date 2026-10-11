#pragma once

#include "railmaster/sim/date.hpp"
#include "railmaster/sim/money.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::sim {

class World;

// A town a scenario places by name, rather than leaving it to the map
// generator: its centre in map cells and how many houses it starts with.
struct ScenarioTown {
    std::string name;
    std::int32_t x = 0, y = 0;
    std::int32_t houses = 20;
};

// One goal (rt3-clone-spec §14.3 [I]). Which fields matter depends on kind.
enum class GoalKind : std::uint8_t {
    ConnectTowns,      // towns: each served by a station of yours, all joined by track
    Territories,       // count: your stations stand in this many territories
    BookValue,         // amount: company book value
    CompanyCash,       // amount: company cash
    Revenue,           // amount: company revenue over the last 12 months
    PersonalNetWorth,  // amount: your net worth
    IndustryProfit,    // amount: lifetime profit of the industries your company owns
    Deliver,           // cargo, count: loads your company has delivered
    OnlyRailroad,      // every rival company merged away
    HighestValue,      // your company's market value is the highest of all
};
struct Goal {
    GoalKind kind = GoalKind::BookValue;
    std::vector<std::string> towns{};
    std::string cargo{};
    std::int64_t count = 0;
    Money amount{};
};

// How far the player is with a goal, for the status screen.
struct GoalProgress {
    bool met = false;
    std::string text; // e.g. "Book value $4,200,000 of $10,000,000"
};

enum class Medal : std::uint8_t { Bronze, Silver, Gold };
const char* medal_name(Medal m);

struct MedalTier {
    Date deadline = Date::from_ymd(1900, 1, 1);
    std::vector<Goal> goals{}; // on top of the lower tiers' (Silver needs Bronze's too)
};

// Scenario events (rt3-clone-spec §14.4 [C]): conditions and effects,
// checked at each month end in file order.
struct EventCondition {
    std::optional<Date> after{};      // on or after this date
    std::optional<Date> before{};     // on or before; a one-shot event lapses after it
    std::vector<Goal> goals{};        // all must be met, as for medals
    std::optional<std::string> variable{}; // a counter (see SetVariable) ...
    std::int64_t at_least = 0;        // ... at least this
    std::int32_t chance_percent = 100; // then fires with this chance each month
};

enum class EffectKind : std::uint8_t {
    News,          // text: a newspaper headline
    CompanyCash,   // amount: to (or, negative, from) the player's company, as a grant
    PlayerCash,    // amount: to the player's own account
    CargoPrice,    // target cargo, percent, months (0: for good): its base price
    ExpressDemand, // target cargo (passengers, mail, troops), percent, months: how many travel
    Production,    // target industry type, percent, months: its output
    BuildCost,     // percent, months: all construction costs
    UnlockEngine,  // target engine: available from now, even before its year
    TerritoryPrice, // target territory, amount: a new price for access rights
    SetVariable,   // target counter, value
    AddVariable,   // target counter, value
    Briefing,      // text: replaces the scenario's briefing
};
struct EventEffect {
    EffectKind kind = EffectKind::News;
    std::string target{};
    std::string text{};
    std::int32_t percent = 100;
    std::int32_t months = 0;
    Money amount{};
    std::int64_t value = 0;
};
struct ScenarioEvent {
    std::string id;
    EventCondition when{};
    std::vector<EventEffect> effects{};
    std::int32_t repeat_months = 0; // 0: fires once; otherwise may fire again this many months later
};

// What a scenario forbids or limits (rt3-clone-spec §14.3 [C/I]), for every
// company in the game.
struct Restrictions {
    // A budget of track, in map cells (double track counts twice): so many
    // at the start, more each year; bulldozing gives cells back only with
    // tile_refund. No budget when unset.
    std::optional<std::int64_t> track_tiles{};
    std::int64_t tiles_per_year = 0;
    bool tile_refund = false;
    bool no_unconnected_track = false; // new track must join the company's own (after its first)
    bool one_company_only = false;     // the player may not found a second company
    bool no_track = false, no_stations = false, no_trains = false, no_industries = false; // purchases
};

struct Scenario {
    std::string key, name, briefing;
    Date start = Date::from_ymd(1830, 1, 1);
    std::uint64_t seed = 1;
    std::string map_size = "small";
    std::int32_t rivals = 0;
    std::int32_t territories = 0;
    std::optional<std::int64_t> founder_fortune, founder_investment, outside_investment;
    bool chairman_can_resign = true;
    bool chairman_can_be_fired = true;
    std::vector<ScenarioTown> towns{};
    std::array<MedalTier, 3> medals{}; // bronze, silver, gold
    std::vector<ScenarioEvent> events{};
    Restrictions restrictions{};

    // Parse and check a scenario file. Throws std::runtime_error on bad data.
    static Scenario from_json(std::string_view json_text);
    // The goals a tier needs, its own and the lower tiers'.
    std::vector<Goal> goals_for(Medal m) const;
    Date last_deadline() const;
};

// Check one goal against the world now.
GoalProgress check_goal(const World& w, const Goal& g);

// Where the player stands: the best medal won so far and when, and whether
// the scenario is over (gold won, or the last deadline passed).
struct ScenarioResult {
    std::optional<Medal> medal{};
    std::optional<Date> won_on{};
    bool finished = false;
};

// Score (rt3-clone-spec §14.3 [I]): medal 1/2/3 x difficulty (Easy 0.75,
// Medium 1, Hard 1.25, Expert 1.5) x (1 + 5% a whole year early), in
// thousandths.
std::int64_t scenario_score_milli(Medal m, std::int32_t difficulty_percent, std::int32_t years_early);

} // namespace railmaster::sim
