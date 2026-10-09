#pragma once

#include "railmaster/sim/money.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::sim {

using CargoId = std::uint16_t;

// Static definition of a cargo type, loaded from data/cargo.json.
// Fields beyond these are added as docs/spec/economy.md pins them down.
struct CargoType {
    CargoId id = 0;
    std::string key;          // stable identifier used in save files and data, e.g. "coal"
    std::string name;         // display name
    Money base_price;         // price per carload before supply/demand and distance
    std::int32_t decay_days = 0; // 0 = does not spoil
};

class CargoRegistry {
public:
    // Parse a JSON document of the form {"cargo": [{"key": ..., ...}, ...]}.
    // Throws std::runtime_error on malformed or duplicate entries.
    static CargoRegistry from_json(std::string_view json_text);

    const std::vector<CargoType>& all() const { return types_; }
    const CargoType& get(CargoId id) const { return types_.at(id); }
    std::optional<CargoId> find(std::string_view key) const;

private:
    std::vector<CargoType> types_;
};

} // namespace railmaster::sim
