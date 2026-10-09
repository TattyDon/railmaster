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
        const std::string cls = entry.value("class", std::string("freight"));
        if (cls == "freight") {
            t.cargo_class = CargoClass::Freight;
        } else if (cls == "express") {
            t.cargo_class = CargoClass::Express;
        } else {
            throw std::runtime_error("cargo data: unknown class '" + cls + "' for '" + t.key + "'");
        }
        t.available_year = entry.value("available_year", 1800);
        t.base_price = Money::dollars(entry.value("base_price", std::int64_t{0}) * provisional::kCargoPriceUnitDollars);
        t.decay_sensitivity = entry.value("decay_sensitivity", 1);
        if (t.decay_sensitivity < 1 || t.decay_sensitivity > 10) {
            throw std::runtime_error("cargo data: decay_sensitivity out of 1..10 for '" + t.key + "'");
        }
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
