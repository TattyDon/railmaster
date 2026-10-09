#pragma once

#include "railmaster/sim/balance.hpp"
#include "railmaster/sim/money.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::sim {

using CargoId = std::uint16_t;

// Freight is priced by the regional price field; express cargo (passengers,
// mail, troops) travels to a specific destination. See docs/spec/economy-cargo.md.
enum class CargoClass : std::uint8_t { Freight, Express };

// Static definition of a cargo type, loaded from data/cargo.json.
struct CargoType {
    CargoId id = 0;
    std::string key;  // stable identifier used in save files and data, e.g. "coal"
    std::string name; // display name
    CargoClass cargo_class = CargoClass::Freight;
    std::int32_t available_year = 1800;
    Money base_price; // per carload (data value x Balance::Economy::cargo_price_unit); zero for express cargo
    // 1 (insensitive) to 10 (most perishable). Believed to set how fast value
    // decays in transit; the exact mapping is not yet known.
    std::int32_t decay_sensitivity = 1;
    // Express cargo only (all provisional): fare in dollars per carload per
    // km of straight-line distance, how much of it a source generates, and
    // whether destinations stop paying past their monthly demand (mail).
    std::int32_t fare_per_km = 0;
    std::int32_t generation = 0;
    bool demand_cap = false;
};

class CargoRegistry {
public:
    // Parse a JSON document of the form {"cargo": [{"key": ..., ...}, ...]}.
    // base_price values are multiplied by `price_unit` dollars. Throws
    // std::runtime_error on malformed or duplicate entries.
    static CargoRegistry from_json(std::string_view json_text,
                                   std::int64_t price_unit = default_balance().economy.cargo_price_unit);

    const std::vector<CargoType>& all() const { return types_; }
    const CargoType& get(CargoId id) const { return types_.at(id); }
    std::optional<CargoId> find(std::string_view key) const;

private:
    std::vector<CargoType> types_;
};

} // namespace railmaster::sim
