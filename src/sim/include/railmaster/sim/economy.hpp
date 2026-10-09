#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/cargo.hpp"
#include "railmaster/sim/fixed_math.hpp"

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
};

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
};

struct Town {
    std::string name;
    std::int32_t cx = 0, cy = 0;
    std::optional<std::int32_t> first_station_day{}; // for the station-age revenue modifier
};

// The map's cargo economy: a grid of economy nodes (one per terrain tile),
// each holding a price and a stock for every cargo, plus the industries and
// houses that produce and consume. Each day:
//   1. sites produce into, and consume from, the stock in their own cell;
//   2. stock spoils a little;
//   3. the price field relaxes one step: consumers pin it high, producers
//      pin it low, and everywhere else it is a screened average of its
//      neighbours, so a consumer's pull fades with distance;
//   4. stock drifts a little toward the highest-priced neighbouring cell.
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
    std::vector<Site> sites_;
    std::vector<Town> towns_;
    Balance::Economy balance_;
};

// Place towns and industries on a new map. A stand-in for authored scenario
// maps: towns, and a spread of industries of every type whose products
// exist in `year`, all on dry land (sizes and counts: Balance::map).
void populate_economy(Economy& economy, const Terrain& terrain, const CargoRegistry& cargo,
                      const IndustryRegistry& industries, Random& rng, std::int32_t year,
                      const Balance& b = default_balance());

} // namespace railmaster::sim
