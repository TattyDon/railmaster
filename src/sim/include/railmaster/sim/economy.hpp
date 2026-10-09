#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/fixed_math.hpp"
#include "railmaster/sim/money.hpp"
#include "railmaster/sim/track.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::sim {

class Random;
class Terrain;


constexpr std::int32_t kMilli = 1000; // stock is counted in thousandths of a carload

using IndustryTypeId = std::uint16_t;
using SiteId = std::uint32_t;

enum class IndustryKind : std::uint8_t {
    Raw,       // produces from nothing; inputs are optional boosters
    Processor, // turns inputs into outputs, up to its capacity
    Sink,      // consumes only
    House,     // a town's houses: consume goods, produce waste
    Port,      // trade beyond the map: takes its inputs (exports), supplies its outputs (imports) [C]
};

// What a port does with its cargo lists: each port can be set to receive,
// supply or exchange [C, research §8].
enum class PortMode : std::uint8_t { Exchange, Receive, Supply };
const char* port_mode_name(PortMode m);

enum class InputRule : std::uint8_t {
    Any, // any one input is enough
    All, // needs every input (RT3's steel mill)
};

struct IndustryInput {
    CargoId cargo = 0;
    std::optional<std::int32_t> until_year; // demand ends after this year
};

struct IndustryType {
    IndustryTypeId id = 0;
    std::string key;
    std::string name;
    IndustryKind kind = IndustryKind::Raw;
    InputRule rule = InputRule::Any;
    std::vector<IndustryInput> inputs; // for Raw, these are boosters
    std::vector<CargoId> outputs;
    std::int32_t rate_per_year = 0; // carloads per year at level 1 (per house for houses)
};

class IndustryRegistry {
public:
    // Parse {"industries": [...]}, resolving cargo keys against `cargo`.
    // Throws std::runtime_error on bad data or unknown cargo.
    static IndustryRegistry from_json(std::string_view json_text, const CargoRegistry& cargo);

    const std::vector<IndustryType>& all() const { return types_; }
    const IndustryType& get(IndustryTypeId id) const { return types_.at(id); }
    std::optional<IndustryTypeId> find(std::string_view key) const;

private:
    std::vector<IndustryType> types_;
};

// One industry, or the houses of a town in one cell.
struct Site {
    SiteId id = 0;
    IndustryTypeId type = 0;
    std::int32_t cx = 0, cy = 0;
    std::int32_t level = 1;          // capacity multiplier; number of houses for houses
    std::vector<std::int32_t> buffer; // per input, milli-carloads held by a processor
    std::int64_t produced_milli = 0;  // lifetime output

    PortMode port_mode = PortMode::Exchange; // ports only
    std::int64_t received_year_milli = 0;    // consumers and ports: deliveries this year

    // Ownership and accounts (rt3-clone-spec §6.2); houses are never owned.
    std::optional<CompanyId> owner{};
    bool closed = false;                    // shut down: produces and consumes nothing
    std::vector<std::int64_t> made_milli{}; // per output, since the accounts were last closed
    std::vector<std::int64_t> used_milli{}; // per input, likewise
    std::vector<Money> monthly_profit{};    // the last 12 months, newest last
    std::int32_t loss_years = 0;            // closed years in a row with a loss
    std::int32_t utilisation_permille = 0;  // output against capacity, last period
    Money owner_paid{};                     // what the owner has spent on it: the book value
};

// One period of an industry's accounts.
struct IndustryAccounts {
    Money revenue; // output at base prices
    Money costs;   // inputs at base prices, labour and overhead
    Money profit() const { return revenue - costs; }
};

// Can a company own this kind of industry (producers and processors), and
// can one build it (processors only [C/D])?
bool ownable(IndustryKind k);
bool buildable(IndustryKind k);

struct Town {
    std::string name;
    std::int32_t cx = 0, cy = 0;
    std::optional<std::int32_t> first_station_day{}; // for the station-age revenue modifier
    std::vector<SiteId> houses{};                    // its house cells
    // Income earned at its stations, for growth: this month, and the last 12.
    Money express_this_month{};
    Money freight_this_month{};
    std::vector<Money> express_months{};
    std::vector<Money> freight_months{};
    std::int32_t growth_milli = 0; // thousandths of the next house
};

// 1 to 5 stars by number of houses (rt3-clone-spec §3.2 [D], §6.4 [I]).
std::int32_t town_stars(std::int64_t houses, const Balance::Towns& b);

// The map's cargo economy: a grid of economy nodes (one per terrain tile),
// each holding a price and a stock for every cargo, plus the industries and
// houses that produce and consume. Each day:
//   1. sites produce into, and consume from, the stock in their own cell;
//   2. stock spoils a little;
//   3. the price field relaxes one step: consumers pin it high, producers
//      pin it low, and everywhere else it is a screened average of its
//      neighbours, so a consumer's pull fades with distance;
//   4. stock drifts a little toward the highest-priced neighbouring cell.
// Terrain shapes steps 3 and 4 (set_terrain): each cell has a conductance,
// high on water and coasts and low in mountains. Price signals reach further,
// and middlemen move freight faster and more cheaply, where it is high.
// Prices are whole dollars per carload. Express cargo (passengers, mail,
// troops) does not use the field; it travels to destinations (later slice).
class Economy {
public:
    Economy(std::int32_t width_cells, std::int32_t height_cells, std::int64_t cell_size_mm,
            const CargoRegistry& cargo, const Balance& balance = default_balance());

    std::int32_t width() const { return width_; }
    std::int32_t height() const { return height_; }

    SiteId add_site(const IndustryRegistry& industries, IndustryTypeId type, std::int32_t cx, std::int32_t cy,
                    std::int32_t level = 1);
    const std::vector<Site>& sites() const { return sites_; }
    void add_town(Town t) { towns_.push_back(std::move(t)); }
    Town& town_mut(std::size_t i) { return towns_.at(i); }
    const std::vector<Town>& towns() const { return towns_; }
    // Houses in a town: the levels of its house cells.
    std::int64_t town_houses(std::size_t t) const;

    // Derive each cell's conductance from the terrain (same grid size).
    // Until called, every cell is flat land.
    void set_terrain(const Terrain& terrain);
    // In thousandths of flat land.
    std::int32_t conductance(std::int32_t cx, std::int32_t cy) const { return conductance_[cell(cx, cy)]; }
    // Production and demand everywhere, in percent (the economic state).
    void set_activity_percent(std::int32_t pct) { activity_percent_ = pct; }
    std::int32_t activity_percent() const { return activity_percent_; }

    Site& site_mut(SiteId id) { return sites_.at(id); }
    // Close the accounts of every industry for a period of `months`: work
    // out what was made and used, record the profit, and start afresh.
    // Returns each site's accounts for the period (indexed by SiteId).
    std::vector<IndustryAccounts> close_accounts(const CargoRegistry& cargo, const IndustryRegistry& industries,
                                                 const Balance::Industries& b, std::int32_t months);
    // Year end: count loss years, close unowned producers and processors that
    // have lost money too long [I], and enlarge unowned ports and consumers
    // that ran near capacity [C].
    struct YearEnd {
        std::vector<SiteId> closed;
        std::vector<SiteId> upgraded;
    };
    YearEnd close_year(const IndustryRegistry& industries, const Balance::Industries& b, Random& rng);

    void step_day(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year);
    // Run the price field to (near) steady state, e.g. when a map is created.
    void settle(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year, int days);

    std::int32_t price(CargoId c, std::int32_t cx, std::int32_t cy) const { return price_[c][cell(cx, cy)]; }
    std::int32_t stock_milli(CargoId c, std::int32_t cx, std::int32_t cy) const { return stock_[c][cell(cx, cy)]; }
    void add_stock(CargoId c, std::int32_t cx, std::int32_t cy, std::int32_t milli);
    // Remove up to `milli`; returns how much was taken.
    std::int32_t take_stock(CargoId c, std::int32_t cx, std::int32_t cy, std::int32_t milli);

    std::int32_t cell_x(MapPoint p) const;
    std::int32_t cell_y(MapPoint p) const;
    // Is this cargo produced or consumed anywhere this year?
    bool active(CargoId c) const { return active_[c]; }

private:
    struct Anchor {
        std::size_t cell;
        std::int32_t price;
    };

    std::size_t cell(std::int32_t cx, std::int32_t cy) const;
    void run_sites(const CargoRegistry& cargo, const IndustryRegistry& industries, std::int32_t year);
    void spoil(const CargoRegistry& cargo);
    void relax(const CargoRegistry& cargo);
    void drift(const CargoRegistry& cargo);

    std::int32_t width_, height_;
    std::int64_t cell_size_mm_;
    std::vector<std::vector<std::int32_t>> price_; // [cargo][cell], dollars
    std::vector<std::vector<std::int32_t>> stock_; // [cargo][cell], milli-carloads
    std::vector<std::vector<Anchor>> anchors_;     // [cargo], rebuilt daily from sites
    std::vector<bool> active_;
    std::vector<std::int32_t> scratch_;
    std::vector<std::int32_t> conductance_; // per cell, permille
    std::vector<std::int32_t> east_w_;      // coupling to the cell to the east, permille; 0 at the edge
    std::vector<std::int32_t> south_w_;     // coupling to the cell to the south
    std::int32_t activity_percent_ = 100;
    std::vector<Site> sites_;
    std::vector<Town> towns_;
    Balance::Economy balance_;
};

// A year's profit from the last 12 months (annualised if fewer are known).
Money annual_profit(const Site& s);
// What a company pays for an industry: a multiple of its yearly profit, or
// the floor price if that is more [C].
Money industry_price(const Site& s, const Balance::Industries& b);
// Building one costs a share more than an existing one's floor price [C];
// doubling a plant's capacity costs a share of building another as big [I].
Money industry_build_cost(const Balance::Industries& b);
Money industry_upgrade_cost(const Site& s, const Balance::Industries& b);

// Place towns and industries on a new map. A stand-in for authored scenario
// maps: towns, and a spread of industries of every type whose products
// exist in `year`, all on dry land (sizes and counts: Balance::map).
void populate_economy(Economy& economy, const Terrain& terrain, const CargoRegistry& cargo,
                      const IndustryRegistry& industries, Random& rng, std::int32_t year,
                      const Balance& b = default_balance());

} // namespace railmaster::sim
