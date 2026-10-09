#include "railmaster/sim/cargo.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace railmaster::sim {

CargoRegistry CargoRegistry::from_json(std::string_view json_text) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("cargo data: ") + e.what());
    }

    CargoRegistry reg;
    for (const auto& entry : doc.at("cargo")) {
        CargoType t;
        t.key = entry.at("key").get<std::string>();
        if (reg.find(t.key)) throw std::runtime_error("cargo data: duplicate key '" + t.key + "'");
        t.id = static_cast<CargoId>(reg.types_.size());
        t.name = entry.at("name").get<std::string>();
        t.base_price = Money::dollars(entry.at("base_price").get<std::int64_t>());
        t.decay_days = entry.value("decay_days", 0);
        reg.types_.push_back(std::move(t));
    }
    return reg;
}

std::optional<CargoId> CargoRegistry::find(std::string_view key) const {
    for (const auto& t : types_) {
        if (t.key == key) return t.id;
    }
    return std::nullopt;
}

} // namespace railmaster::sim
