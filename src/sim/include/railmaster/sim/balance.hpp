#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace railmaster::sim {

// Every community [C] and inferred [I] number in the simulation, per the
// rule in docs/spec/rt3-clone-spec.md §0: they live in data/balance.json so
// they can be calibrated against the real game without touching code.
// Documented [D] rules stay in code.
//
// The defaults here are the shipped values; data/balance.json mirrors them
// (a test keeps the two in step). Each value is described, with its source,
// in docs/spec/m1-provisional-models.md, m2-economy-model.md and
// m3-finance-model.md.
struct Balance {
    static constexpr std::int32_t kVersion = 1;

    struct Trains {
        std::int64_t seconds_per_tick = 40; // how much travel a simulation tick represents
        std::int32_t accel_ticks_to_top_speed = 8;
        std::int32_t station_dwell_ticks = 8;
        std::int32_t min_speed_permille = 100;   // the slowest a grade can make a train
        std::int32_t grade_penalty = 25;         // speed lost per (grade bp x (cars + 2)) / grade rating
        std::int32_t wood_bridge_speed_permille = 500;
        std::int32_t other_bridge_speed_permille = 900;
    } trains;

    struct Servicing {
        std::int64_t water_range_mm = 150'000'000;
        std::int64_t sand_range_climb_mm = 600'000;
        std::int64_t oil_range_mm = 1'500'000'000;
        std::int32_t service_threshold_permille = 500;
        std::int32_t service_ticks = 4;
        std::int32_t no_water_speed_permille = 250;
        std::int32_t no_sand_grade_permille = 400;
        std::int64_t service_tower_cost = 30'000;
        std::int64_t maintenance_facility_cost = 100'000;
    } servicing;

    struct Breakdowns {
        std::int64_t mean_distance_mm = 2'000'000'000; // at reliability 100, full oil
        std::int32_t empty_oil_multiplier = 4;
        std::int32_t breakdown_ticks = 32;
        std::int64_t crash_ppb_per_tick = 342; // 0.2% a year at reliability 100
        std::int32_t maintenance_age_cap_years = 20; // maintenance rises linearly to 3x by this age
        std::int32_t maintenance_low_oil_multiplier = 2;
    } breakdowns;

    struct Track {
        std::int32_t default_max_grade_bp = 300;
        std::int64_t tunnel_cover_mm = 12'000;
        std::int64_t viaduct_clearance_mm = 10'000;
        std::int64_t piece_mm = 500'000; // curve smoothness
        std::int64_t ground_per_km = 25'000;
        // Structure costs as multiples of plain track.
        std::int32_t wood_bridge_multiple = 3;
        std::int32_t steel_bridge_multiple = 5;
        std::int32_t stone_bridge_multiple = 6;
        std::int32_t suspension_bridge_multiple = 10;
        std::int32_t tunnel_multiple = 15;
        std::int32_t double_track_percent = 170;
        std::int64_t suspension_min_span_mm = 2'000'000;
    } track;

    struct Stations {
        std::int64_t small_cost = 50'000;
        std::int64_t medium_cost = 100'000;
        std::int64_t large_cost = 200'000;
        std::int32_t catchment_small = 1; // radius in economy cells
        std::int32_t catchment_medium = 2;
        std::int32_t catchment_large = 3;
        std::int32_t gather_percent_per_day = 20;
        std::int32_t cap_milli = 20'000;
        std::int32_t town_reach_cells = 4; // a station within this of a town centre serves that town
    } stations;

    struct Economy {
        std::int64_t cargo_price_unit = 1000; // cargo.json base prices are in $K
        std::int32_t demand_price_percent = 150;
        std::int32_t supply_price_percent = 50;
        std::int32_t neutral_price_percent = 50;
        std::int32_t screening_per_10000 = 25;
        std::int32_t drift_percent_per_day = 5;
        std::int32_t transport_cost_percent = 1;
        std::int32_t saturation_days = 30;
        std::int32_t industry_saturation_days = 120;
        std::int32_t spoilage_per_mille_per_sensitivity = 1;
        std::int32_t max_stock_milli = 50'000;
        std::int32_t input_buffer_days = 30;
        std::int32_t boost_percent = 50;
        std::int32_t history_days = 365; // simulated before a new map opens
        // Middlemen and price coupling by terrain (rt3-clone-spec §5.3): how
        // easily freight and price signals cross a cell, in thousandths of
        // flat land. A cell is hilly or mountainous by its steepest corner-
        // to-corner grade; land next to water counts as coast.
        std::int32_t water_conductance_permille = 2000;
        std::int32_t coast_conductance_permille = 1500;
        std::int32_t hill_conductance_permille = 750;
        std::int32_t mountain_conductance_permille = 500;
        std::int32_t hill_grade_bp = 400;      // 40 m of relief across a 1 km cell
        std::int32_t mountain_grade_bp = 700;
    } economy;

    struct MapGeneration {
        std::int32_t cells_per_town = 2048;
        std::int32_t town_min_houses = 10;
        std::int32_t town_max_houses = 40;
        std::int32_t town_spacing_cells = 12;
        std::int32_t raw_per_type = 4; // per 128 x 128 map
        std::int32_t processors_per_type = 2;
        std::int32_t sinks_per_type = 2;
        std::int32_t max_height_m = 400;
    } map;

    struct Freight {
        std::int32_t decay_ppm_per_sensitivity_day = 2300; // value left = exp(-k S t), k = 0.0023
        std::int32_t expired_permille = 100;
    } freight;

    struct Express {
        std::int32_t attraction_half = 20;
        std::int32_t cap_milli = 20'000;
        std::int32_t wait_loss_per_mille_per_sensitivity = 5;
        std::int32_t mail_cap_months = 2;
    } express;

    // The business cycle (rt3-clone-spec §5.5). Arrays run Depression,
    // Recession, Normal, Prosperity, Boom.
    struct EconomicStates {
        std::array<std::int32_t, 5> activity_percent{75, 85, 100, 120, 125}; // production and demand
        std::array<std::int32_t, 5> cost_percent{85, 92, 100, 108, 115};     // construction, fuel, labour
        std::array<std::int32_t, 5> prime_rate_bp{800, 700, 600, 500, 400};
        std::array<std::int32_t, 5> stock_percent{80, 90, 100, 112, 125}; // share price fundamentals
        std::int32_t checks_per_year = 2;
        std::int32_t stay_percent = 50;
        std::int32_t toward_normal_percent = 30; // the rest moves one step away
    } economic_states;

    // Rival companies' decisions (rt3-clone-spec §13.2, all [I]).
    struct Ai {
        std::int32_t min_route_km = 8;
        std::int32_t max_route_km = 40;           // plus up to half again for the keenest builders
        std::int32_t build_interval_months_min = 3; // the most expansive tycoon
        std::int32_t build_interval_months_max = 24; // the least
        std::int64_t cash_reserve = 250'000;
        std::int32_t candidates_previewed = 8;    // best rough guesses costed in full each time
        std::int32_t cars_per_train = 4;
        std::int32_t max_trains_per_route = 3;
        std::int32_t waiting_carloads_for_train = 6;
        std::int32_t cover_cells = 2;             // a station this near already serves a place
        // Share trading, against the price the market is heading for
        // (target_share_price): buy below, sell and short above.
        std::int32_t buy_below_value_percent = 80;
        std::int32_t sell_above_value_percent = 120;
        std::int32_t short_above_value_percent = 125;
    } ai;

    struct Finance {
        std::int64_t starting_cash = 6'000'000;
        std::int64_t fuel_per_km_steam = 20;
        std::int64_t fuel_per_km_diesel = 15;
        std::int64_t fuel_per_km_electric = 10;
        std::int64_t fuel_per_km_per_car = 2;
        std::int32_t track_upkeep_per_mille_month = 5;
        std::int32_t building_upkeep_per_mille_month = 5;
        std::int32_t easy_cost_percent = 85;
        std::array<std::int32_t, 7> rating_leverage_limits{5, 15, 25, 35, 45, 55, 70}; // AAA..C, % debt/assets
        // AAA..D at the Normal prime rate; bonds pay this plus (prime - Normal prime).
        std::array<std::int32_t, 8> bond_rate_bp{400, 450, 500, 600, 700, 800, 1000, 1200};
        std::int32_t bonds_per_notch_when_proven = 4;
        std::int64_t bond_face_value = 500'000;
        std::int32_t bond_underwriting_percent = 2;
        std::int32_t bond_early_repayment_percent = 2;
        std::int32_t max_bonds = 20;
        std::int32_t bond_maturity_years = 30;
    } finance;

    struct Stock {
        std::int64_t founding_shares = 600'000;
        std::int64_t founding_player_shares = 300'000;
        std::int64_t starting_personal_cash = 500'000;
        std::int64_t salary_per_year = 50'000;
        std::int32_t margin_percent = 50;
        std::int32_t margin_interest_bp = 1000; // at the Normal prime rate; moves with it
        std::int32_t impact_per_share_of_company = 2;
        std::int32_t issue_percent = 10;
        std::int32_t book_weight_percent = 60;
        std::int32_t earnings_multiple = 8;
        std::int32_t dividend_multiple = 10;
        std::int32_t price_adjust_percent = 25;
        std::int64_t min_share_price_cents = 50;
        std::int64_t share_block = 1000;
        // Short selling [D concept]: a short position counts against
        // purchasing power at this share of its value [I], and all shorts
        // together may not exceed this share of net worth [C].
        std::int32_t short_margin_percent = 150;
        std::int32_t short_cap_percent_of_net_worth = 50;
    } stock;

    // Takeovers and mergers (rt3-clone-spec §12.6, the vote models [I]).
    struct Corporate {
        // Takeover: share of the public float voting for the bidder, in
        // thousandths, built up from these parts.
        std::int32_t takeover_base_support_permille = 250;
        std::int32_t takeover_below_book_permille = 250; // the price is below book value per share
        std::int32_t takeover_loss_year_permille = 250;  // the last full year was a loss
        // Merger: holders accept from offer / price of this percent, the
        // public in proportion around it ([I] linearised sigmoid).
        std::int32_t merger_neutral_premium_percent = 110;
        std::int32_t merger_support_slope = 3; // permille of the float per permille of premium
        std::int32_t merger_chairman_premium_percent = 125;
        std::int32_t retry_days = 365; // after a failed attempt [C]
    } corporate;

    // Parse a balance file. Missing keys keep their defaults; unknown keys
    // and a wrong version are errors (std::runtime_error), so a typo in a
    // calibration file cannot be silently ignored.
    static Balance from_json(std::string_view json_text);
    // The whole balance as pretty-printed JSON, every key present.
    std::string to_json() const;

    // exp(-k) for one unit of sensitivity x day, as Q30 fixed point, derived
    // from decay_ppm_per_sensitivity_day with integer arithmetic only.
    std::int64_t decay_step_q30() const;
};

bool operator==(const Balance& a, const Balance& b);

// The shipped defaults, for code that runs without a loaded balance (tests,
// standalone tools).
const Balance& default_balance();

} // namespace railmaster::sim
