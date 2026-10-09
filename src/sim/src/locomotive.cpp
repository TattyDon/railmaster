#include "railmaster/sim/locomotive.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace railmaster::sim {

namespace {

Fuel parse_fuel(const std::string& s, const std::string& key) {
    if (s == "steam") return Fuel::Steam;
    if (s == "diesel") return Fuel::Diesel;
    if (s == "electric") return Fuel::Electric;
    throw std::runtime_error("locomotive data: unknown fuel '" + s + "' for '" + key + "'");
}

} // namespace

LocomotiveRegistry LocomotiveRegistry::from_json(std::string_view json_text) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("locomotive data: ") + e.what());
    }

    LocomotiveRegistry reg;
    for (const auto& entry : doc.at("locomotives")) {
        LocomotiveType t;
        t.key = entry.at("key").get<std::string>();
        if (reg.find(t.key)) throw std::runtime_error("locomotive data: duplicate key '" + t.key + "'");
        t.id = static_cast<LocoTypeId>(reg.types_.size());
        t.name = entry.at("name").get<std::string>();
        t.fuel = parse_fuel(entry.at("fuel").get<std::string>(), t.key);
        t.available_from = entry.at("available_from").get<std::int32_t>();
        if (entry.contains("available_until")) t.available_until = entry["available_until"].get<std::int32_t>();
        t.top_speed_mph = entry.at("top_speed_mph").get<std::int32_t>();
        t.cost = Money::dollars(entry.at("cost").get<std::int64_t>());
        t.maintenance_per_year = Money::dollars(entry.at("maintenance_per_year").get<std::int64_t>());
        t.grade_rating = entry.value("grade_rating", 100);
        t.reliability = entry.value("reliability", 100);
        t.scenario_only = entry.value("scenario_only", false);
        if (t.top_speed_mph <= 0) throw std::runtime_error("locomotive data: bad top speed for '" + t.key + "'");
        if (t.grade_rating <= 0) throw std::runtime_error("locomotive data: bad grade_rating for '" + t.key + "'");
        if (t.reliability <= 0) throw std::runtime_error("locomotive data: bad reliability for '" + t.key + "'");
        if (t.available_until && *t.available_until < t.available_from) {
            throw std::runtime_error("locomotive data: availability ends before it starts for '" + t.key + "'");
        }
        reg.types_.push_back(std::move(t));
    }
    return reg;
}

std::optional<LocoTypeId> LocomotiveRegistry::find(std::string_view key) const {
    for (const auto& t : types_) {
        if (t.key == key) return t.id;
    }
    return std::nullopt;
}

} // namespace railmaster::sim
