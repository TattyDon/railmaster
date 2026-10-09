#pragma once

#include "railmaster/sim/money.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::sim {

using LocoTypeId = std::uint16_t;

enum class Fuel : std::uint8_t { Steam, Diesel, Electric };

// Static definition of a locomotive model, loaded from data/locomotives.json.
struct LocomotiveType {
    LocoTypeId id = 0;
    std::string key;
    std::string name;
    Fuel fuel = Fuel::Steam;
    std::int32_t available_from = 0;
    std::optional<std::int32_t> available_until; // nullopt = until the end of the game
    std::int32_t top_speed_mph = 0;
    Money cost;
    Money maintenance_per_year;
    // Hill-climbing ability. 100 is the provisional default; higher climbs
    // better. RT3 shows this as a word scale whose numbers are not yet known.
    std::int32_t grade_rating = 100;
    // Relative chance of breaking down: 100 is average, 200 fails half as
    // often. RT3 shows a word rating; per-engine values are not yet known.
    std::int32_t reliability = 100;
    bool scenario_only = false;

    bool available_in(std::int32_t year) const {
        return !scenario_only && year >= available_from && (!available_until || year <= *available_until);
    }
};

class LocomotiveRegistry {
public:
    // Parse {"locomotives": [...]}. Throws std::runtime_error on bad data.
    static LocomotiveRegistry from_json(std::string_view json_text);

    const std::vector<LocomotiveType>& all() const { return types_; }
    const LocomotiveType& get(LocoTypeId id) const { return types_.at(id); }
    std::optional<LocoTypeId> find(std::string_view key) const;

private:
    std::vector<LocomotiveType> types_;
};

} // namespace railmaster::sim
