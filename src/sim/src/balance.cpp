#include "railmaster/sim/balance.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace railmaster::sim {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Trains, seconds_per_tick, accel_ticks_to_top_speed,
                                                station_dwell_ticks, min_speed_permille, grade_penalty,
                                                wood_bridge_speed_permille, other_bridge_speed_permille)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Servicing, water_range_mm, sand_range_climb_mm, oil_range_mm,
                                                service_threshold_permille, service_ticks, no_water_speed_permille,
                                                no_sand_grade_permille, service_tower_cost,
                                                maintenance_facility_cost)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Breakdowns, mean_distance_mm, empty_oil_multiplier,
                                                breakdown_ticks, crash_ppb_per_tick, maintenance_age_cap_years,
                                                maintenance_low_oil_multiplier)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Track, default_max_grade_bp, tunnel_cover_mm,
                                                viaduct_clearance_mm, piece_mm, ground_per_km, wood_bridge_multiple,
                                                steel_bridge_multiple, stone_bridge_multiple,
                                                suspension_bridge_multiple, tunnel_multiple, double_track_percent,
                                                suspension_min_span_mm)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Stations, small_cost, medium_cost, large_cost,
                                                catchment_small, catchment_medium, catchment_large,
                                                gather_percent_per_day, cap_milli, town_reach_cells)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Economy, cargo_price_unit, demand_price_percent,
                                                supply_price_percent, neutral_price_percent, screening_per_10000,
                                                drift_percent_per_day, transport_cost_percent, saturation_days,
                                                industry_saturation_days, spoilage_per_mille_per_sensitivity,
                                                max_stock_milli, input_buffer_days, boost_percent, history_days,
                                                water_conductance_permille, coast_conductance_permille,
                                                hill_conductance_permille, mountain_conductance_permille,
                                                hill_grade_bp, mountain_grade_bp)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::EconomicStates, activity_percent, cost_percent,
                                                prime_rate_bp, stock_percent, checks_per_year, stay_percent,
                                                toward_normal_percent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::MapGeneration, cells_per_town, town_min_houses,
                                                town_max_houses, town_spacing_cells, raw_per_type,
                                                processors_per_type, sinks_per_type, max_height_m)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Freight, decay_ppm_per_sensitivity_day, expired_permille)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Express, attraction_half, cap_milli,
                                                wait_loss_per_mille_per_sensitivity, mail_cap_months)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Ai, min_route_km, max_route_km, build_interval_months_min,
                                                build_interval_months_max, cash_reserve, candidates_previewed,
                                                cars_per_train, max_trains_per_route, waiting_carloads_for_train,
                                                cover_cells, buy_below_value_percent, sell_above_value_percent,
                                                short_above_value_percent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Finance, starting_cash, fuel_per_km_steam, fuel_per_km_diesel,
                                                fuel_per_km_electric, fuel_per_km_per_car,
                                                track_upkeep_per_mille_month, building_upkeep_per_mille_month,
                                                easy_cost_percent, rating_leverage_limits, bond_rate_bp,
                                                bonds_per_notch_when_proven, bond_face_value,
                                                bond_underwriting_percent, bond_early_repayment_percent, max_bonds,
                                                bond_maturity_years)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Stock, founding_shares, founding_player_shares,
                                                starting_personal_cash, salary_per_year, margin_percent,
                                                margin_interest_bp, impact_per_share_of_company, issue_percent,
                                                book_weight_percent, earnings_multiple, dividend_multiple,
                                                price_adjust_percent, min_share_price_cents, share_block,
                                                short_margin_percent, short_cap_percent_of_net_worth)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Balance::Corporate, takeover_base_support_permille,
                                                takeover_below_book_permille, takeover_loss_year_permille,
                                                merger_neutral_premium_percent, merger_support_slope,
                                                merger_chairman_premium_percent, retry_days)

namespace {

nlohmann::json to_tree(const Balance& b) {
    nlohmann::json j;
    j["version"] = Balance::kVersion;
    j["trains"] = b.trains;
    j["servicing"] = b.servicing;
    j["breakdowns"] = b.breakdowns;
    j["track"] = b.track;
    j["stations"] = b.stations;
    j["economy"] = b.economy;
    j["map"] = b.map;
    j["freight"] = b.freight;
    j["express"] = b.express;
    j["economic_states"] = b.economic_states;
    j["ai"] = b.ai;
    j["finance"] = b.finance;
    j["stock"] = b.stock;
    j["corporate"] = b.corporate;
    return j;
}

// Every key in `given` must exist in `known` (the full tree with defaults).
// Keys starting with "_" are comments and are allowed anywhere.
void reject_unknown_keys(const nlohmann::json& given, const nlohmann::json& known, const std::string& path) {
    if (!given.is_object()) return;
    for (const auto& [key, value] : given.items()) {
        if (!key.empty() && key[0] == '_') continue;
        if (!known.contains(key)) throw std::runtime_error("balance: unknown key '" + path + key + "'");
        reject_unknown_keys(value, known[key], path + key + ".");
    }
}

template <typename T>
void read_section(const nlohmann::json& j, const char* name, T& out) {
    if (j.contains(name)) out = j.at(name).get<T>();
}

} // namespace

Balance Balance::from_json(std::string_view json_text) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("balance: ") + e.what());
    }
    if (!j.is_object()) throw std::runtime_error("balance: expected a JSON object");
    if (j.contains("version") && j["version"].get<std::int32_t>() != kVersion) {
        throw std::runtime_error("balance: version " + j["version"].dump() + " is not supported (expected " +
                                 std::to_string(kVersion) + ")");
    }
    reject_unknown_keys(j, to_tree(Balance{}), "");

    Balance b;
    try {
        read_section(j, "trains", b.trains);
        read_section(j, "servicing", b.servicing);
        read_section(j, "breakdowns", b.breakdowns);
        read_section(j, "track", b.track);
        read_section(j, "stations", b.stations);
        read_section(j, "economy", b.economy);
        read_section(j, "map", b.map);
        read_section(j, "freight", b.freight);
        read_section(j, "express", b.express);
        read_section(j, "economic_states", b.economic_states);
        read_section(j, "ai", b.ai);
        read_section(j, "finance", b.finance);
        read_section(j, "stock", b.stock);
        read_section(j, "corporate", b.corporate);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("balance: ") + e.what());
    }
    return b;
}

std::string Balance::to_json() const { return to_tree(*this).dump(2) + "\n"; }

std::int64_t Balance::decay_step_q30() const {
    // exp(-k) = 1 - k + k^2/2! - k^3/3! + ..., with k = ppm / 1e6, in Q30.
    constexpr std::int64_t kOne = std::int64_t{1} << 30;
    const std::int64_t ppm = freight.decay_ppm_per_sensitivity_day;
    std::int64_t term = kOne;
    std::int64_t total = kOne;
    for (std::int64_t n = 1; n < 20; ++n) {
        term = term * ppm / 1'000'000 / n;
        if (term == 0) break;
        total += (n % 2 == 0) ? term : -term;
    }
    return total;
}

bool operator==(const Balance& a, const Balance& b) { return to_tree(a) == to_tree(b); }

const Balance& default_balance() {
    static const Balance b;
    return b;
}

} // namespace railmaster::sim
