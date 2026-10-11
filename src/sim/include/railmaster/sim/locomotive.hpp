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
    // Performance (rt3-clone-spec §9.1, §10.2 [I]; RT3 shows only the
    // speed-vs-grade chart, so the numbers are ours until measured):
    // - time constant of the approach to target speed, in thousandths of a day;
    std::int32_t accel_milli_days = 500;
    // - speed lost per 1% grade at the reference load, in thousandths;
    std::int32_t grade_drop_permille = 180;
    // - the load it hauls without slowing, in tons;
    std::int32_t free_weight_tons = 50;
    // - fuel used per ton-km, in percent of average;
    std::int32_t fuel_economy_percent = 100;
    // - its own weight, which it also burns fuel to move;
    std::int32_t weight_tons = 60;
    // - passenger appeal: passenger and mail fares +30% (handsome) to -15%
    //   (ugly) [D range].
    std::int32_t appeal_percent = 0;
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
