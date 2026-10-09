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
        // Ageing (rt3-clone-spec §9.4 [I]): breakdowns x (1 + age / 15 years),
        // crashes x (1 + age / 20 years).
        std::int32_t breakdown_age_years = 15;
        std::int32_t crash_age_years = 20;
        // Maintenance = base x (1 + this% x age in years), with no cap, and
        // x maintenance_no_oil_percent when out of oil (rt3-clone-spec §9.4 [I]).
        std::int32_t maintenance_age_percent_per_year = 4;
        std::int32_t maintenance_no_oil_percent = 150;
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
        // Station-area buildings (rt3-clone-spec §7.2): costs are not
        // researched [I]; each serves stations within the range; income per
        // passenger load is [I], tuned to be small [C: "~$1K a year to a
        // small loss per building"].
        std::int64_t building_range_mm = 2'000'000;
        std::int64_t post_office_cost = 30'000;
        std::int64_t hotel_cost = 100'000;
        std::int64_t restaurant_cost = 50'000;
        std::int64_t tavern_cost = 25'000;
        std::int64_t hotel_per_load_day = 20;   // passengers waiting at the station [D/C]
        std::int64_t restaurant_per_load = 50;  // every passenger arriving or leaving [C]
        std::int64_t tavern_per_load = 80;      // passengers boarding [C]
        std::int32_t wait_loss_percent = 50;    // post office (mail) and hotel (passengers) slow waiting loss [D]
    } stations;

    struct Economy {
        std::int64_t cargo_price_unit = 1000; // cargo.json base prices are in $K
        std::int32_t demand_price_percent = 150;
        std::int32_t supply_price_percent = 50;
        std::int32_t neutral_price_percent = 50;
        std::int32_t screening_per_10000 = 25;
        std::int32_t drift_percent_per_day = 5;
        std::int32_t transport_cost_percent = 1;
        // Unconsumed stock equal to this many days of demand halves a
        // consumer's price. Calibrated for the spec's low rates (a town of 30
        // houses wants ~3 loads of a good a year), so one delivery dents the
        // price rather than crashing it [I; docs/spec/calibration.md].
        std::int32_t saturation_days = 365;
        std::int32_t industry_saturation_days = 730;
        std::int32_t spoilage_per_mille_per_sensitivity = 1;
        std::int32_t max_stock_milli = 50'000;
        std::int32_t input_buffer_days = 30;
        std::int32_t boost_percent = 50;
        std::int32_t history_days = 365; // simulated before a new map opens
        // Price-responsive output (rt3-clone-spec §6.1 [C]): unsold stock
        // equal to this many days of output halves a producer's price, and
        // producers slow as their local price falls below the full-pace
        // share of base, stopping at the stop share [I].
        std::int32_t supply_saturation_days = 180;
        std::int32_t output_full_percent = 35;
        std::int32_t output_stop_percent = 10;
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
        std::int32_t ports = 3; // per 128 x 128 map, on the coast or else the map edge
        // New industries over time [C/I]: the yearly chance of an extra one
        // of each type (times the economy's activity), and the cap on how
        // many a type may reach, as a multiple of its usual number.
        std::int32_t appear_chance_percent = 10;
        std::int32_t max_count_multiple = 2;
    } map;

    struct Freight {
        std::int32_t decay_ppm_per_sensitivity_day = 2300; // value left = exp(-k S t), k = 0.0023
        std::int32_t expired_permille = 100;
    } freight;

    struct Express {
        std::int32_t attraction_half = 2; // destination pull, in loads a year of house demand
        std::int32_t min_load_milli = 500; // an express car leaves with at least half a load [C]
        std::int32_t cap_milli = 20'000;
        std::int32_t wait_loss_per_mille_per_sensitivity = 1; // ~half the on-train rate [I, spec §8.2]
        std::int32_t mail_cap_months = 2;
    } express;

    // Town growth (rt3-clone-spec §6.4 [I]), per year: a base rate, plus a
    // share for each $1,000 per house of passenger and mail income, and of
    // freight income, earned at the town's stations in the last 12 months;
    // towns no train serves grow at a fraction of that. Stars by houses.
    struct Towns {
        std::int32_t base_growth_permille = 8;            // 0.5-1% [I]
        std::int32_t express_permille_per_k_house = 3;
        std::int32_t freight_permille_per_k_house = 3;
        std::int32_t max_growth_permille = 150;
        std::int32_t unconnected_permille = 300;
        std::int32_t max_houses_per_cell = 6;
        std::int32_t max_radius_cells = 8;
        std::array<std::int32_t, 4> star_houses{20, 50, 120, 300}; // 2 to 5 stars from these
    } towns;

    // Owning industries (rt3-clone-spec §6.1-6.2). An industry's accounts:
    // output at the cargo's base price, less inputs at theirs, labour in
    // proportion to output and a fixed overhead per level of capacity.
    struct Industries {
        std::int32_t labour_percent = 20;          // of output value [I]
        std::int64_t overhead_per_level = 30'000;  // dollars a year [C: a brewery's $30K]
        std::int32_t profit_multiple = 10;         // price = this x a year's profit [C]
        std::int64_t floor_price = 300'000;        // per level, for the unprofitable [C: farms $240K-$350K]
        std::int32_t build_cost_percent = 150;     // of an existing one's floor price [C, low confidence]
        std::int32_t upgrade_cost_percent = 50;    // of building another of the same size [I: "much less"]
        std::int32_t close_after_loss_years = 5;   // unowned industries [I]
        std::int32_t close_chance_percent = 20;    // each year after that [I]
        // Unowned ports and consumers double their capacity when a year's
        // deliveries reach this share of it [C: "upgrade on demand"; I numbers].
        std::int32_t receiver_upgrade_permille = 900;
        std::int32_t receiver_max_level = 8;
        // Warehouses keep cargo nearby from spoiling [D/C]: within this many
        // cells, stock spoils at this share of the usual rate [I]; the owner
        // earns the value saved [C: "its profit is the loss it saves"].
        std::int32_t warehouse_radius_cells = 2;
        std::int32_t warehouse_spoilage_percent = 25;
    } industries;

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
        std::int64_t fuel_per_km_steam = 20;
        std::int64_t fuel_per_km_diesel = 15;
        std::int64_t fuel_per_km_electric = 10;
        std::int64_t fuel_per_km_per_car = 2;
        // A year's upkeep as a share of build cost, charged monthly. Track:
        // rt3-clone-spec §11.2 [I] 2%. Buildings: no figure in the spec;
        // ours, RT2's 6%.
        std::int32_t track_upkeep_bp_per_year = 200;
        std::int32_t building_upkeep_bp_per_year = 600;
        std::int32_t easy_cost_percent = 85;
        // Credit rating (rt3-clone-spec §12.4 [I]): a score in points,
        //   + rating_asset_points per doubling of assets over debt (to +/- rating_asset_cap_doublings)
        //   + rating_cover_points per times operating profit covers interest (to rating_cover_cap)
        //   + / - rating_profit_year_points per profitable / loss year of the last three
        //   - rating_bond_points per bond outstanding
        //   - rating_bankruptcy_points within bankruptcy_repeat_years of a bankruptcy
        // graded A+..C- by rating_thresholds (the least score for each),
        // else D. Bonds pay the prime rate plus the grade's spread.
        std::int32_t rating_asset_points = 1000;
        std::int32_t rating_asset_cap_doublings = 5;
        std::int32_t rating_cover_points = 500;
        std::int32_t rating_cover_cap = 5;
        std::int32_t rating_profit_year_points = 500;
        std::int32_t rating_bond_points = 100;
        std::int32_t rating_bankruptcy_points = 2000;
        std::array<std::int32_t, 9> rating_thresholds{7000, 6000, 5000, 4000, 3000, 2000, 1000, 0, -1000};
        std::array<std::int32_t, 10> bond_spread_bp{0, 50, 100, 200, 300, 400, 500, 600, 700, 800}; // A+..D
        std::int64_t bond_face_value = 500'000;
        std::int32_t bond_underwriting_percent = 2;
        std::int32_t bond_early_repayment_percent = 2;
        std::int32_t max_bonds = 20;
        std::int32_t bond_maturity_years = 30;
        // Bankruptcy (rt3-clone-spec §12.4): allowed after this many loss
        // years in a row, or when the company cannot pay [C/I]; keeps this
        // share of each bond [D: halved]; rating held at D for some years
        // and no second bankruptcy within more [I].
        std::int32_t bankruptcy_loss_years = 2;
        std::int32_t bankruptcy_debt_kept_percent = 50;
        std::int32_t bankruptcy_rating_years = 5;
        std::int32_t bankruptcy_repeat_years = 10;
    } finance;

    struct Stock {
        // Founding a company (rt3-clone-spec §12.2 [C/I]): the founder's
        // personal fortune, what they put in unless they choose otherwise,
        // the most outside investors will put in, and the least a founder
        // may. Shares are issued at the founding price [I, RT2: $10].
        std::int64_t founder_fortune = 3'500'000;
        std::int64_t founder_investment = 3'000'000;
        std::int64_t outside_investment = 3'000'000;
        std::int64_t min_founder_investment = 100'000;
        std::int64_t founding_share_price_cents = 1'000;
        std::int64_t starting_personal_cash = 500'000; // a tycoon who arrives later to run a company
        std::int32_t brokerage_permille = 10;          // on every trade a player makes [I: ~1%]
        std::int64_t salary_per_year = 50'000;
        std::int32_t margin_percent = 50;
        std::int32_t margin_interest_bp = 1000; // at the Normal prime rate; moves with it
        std::int32_t issue_percent = 10;
        // Share price (rt3-clone-spec §12.3 [I]), monthly:
        //   value = book/share x book_weight
        //         + max(0, EPS trend) x P/E for the economic state
        //         + dividend/share x dividend_multiple x min(1, unbroken years / dividend_full_years)
        //         + revenue/share x revenue_weight
        // then x the state's stock_percent. The price closes 1/price_smoothing
        // of the gap each month. The EPS trend weights the trailing 12 months,
        // last year and the year before by eps_trend_weights.
        std::int32_t book_weight_percent = 80;
        std::array<std::int32_t, 5> pe_by_state{8, 9, 10, 11, 12}; // Depression..Boom
        std::array<std::int32_t, 3> eps_trend_weights{3, 2, 1};
        std::int32_t dividend_multiple = 6;
        std::int32_t dividend_full_years = 5;
        std::int32_t revenue_weight_percent = 10;
        std::int32_t price_smoothing = 8; // RT2-style 1/8 [C]
        // Trade pressure [C/I]: a trade moves the price by impact x
        // sqrt(shares traded / shares outstanding) x price; the pressure
        // decays, keeping this share each month (891: halves in 6 months).
        // 200 makes one 1,000-share lot move a $50 stock with 100,000
        // shares by $1 [C].
        std::int32_t impact_permille = 200;
        std::int32_t pressure_keep_permille = 891;
        std::int64_t min_share_price_cents = 50;
        std::int64_t share_block = 1000;
        // Short selling [D concept]: a short position counts against
        // purchasing power at this share of its value [I], and all shorts
        // together may not exceed this share of net worth [C].
        std::int32_t short_margin_percent = 150;
        std::int32_t short_cap_percent_of_net_worth = 50;
        // Salary [I]: salary_per_year x (1 + this% x five-year weighted
        // return), held between the two limits (rt3-clone-spec §12.2).
        std::int32_t salary_return_factor_percent = 200;
        std::int32_t salary_min_percent = 50;
        std::int32_t salary_max_percent = 200;
        // Stock splits [C/I]: a price over this for split_months month ends
        // in a row splits 2 for 1, or 3 for 1 over the higher price.
        std::int64_t split_price_cents = 12'000;
        std::int64_t big_split_price_cents = 20'000;
        std::int32_t split_months = 3;
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
        // Investor sentiment (§12.2): bad years in a row before investors
        // grumble [D/I], and before they vote the chairman out [D/I]; a
        // five-year weighted return that makes them happy [I].
        std::int32_t grumble_after_bad_years = 2;
        std::int32_t oust_after_bad_years = 3;
        std::int32_t happy_return_permille = 100;
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
