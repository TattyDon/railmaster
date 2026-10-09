#include "railmaster/sim/world.hpp"

#include "railmaster/sim/freight.hpp"

#include <utility>

namespace railmaster::sim {

namespace {

Ledger revenue_line(const CargoType& c) {
    if (c.key == "passengers") return Ledger::PassengerRevenue;
    if (c.key == "mail") return Ledger::MailRevenue;
    if (c.key == "troops") return Ledger::TroopRevenue;
    return Ledger::FreightRevenue;
}

std::int64_t fuel_per_km(const Balance::Finance& f, const LocomotiveType& loco, std::size_t cars) {
    std::int64_t base = f.fuel_per_km_steam;
    if (loco.fuel == Fuel::Diesel) base = f.fuel_per_km_diesel;
    if (loco.fuel == Fuel::Electric) base = f.fuel_per_km_electric;
    return base + f.fuel_per_km_per_car * static_cast<std::int64_t>(cars);
}

} // namespace

World::World(const WorldConfig& config, GameData data)
    : rng_(config.seed),
      date_(config.start_date),
      terrain_(config.width_tiles, config.height_tiles, config.tile_size_m),
      data_(std::move(data)),
      economy_(config.width_tiles, config.height_tiles, std::int64_t{config.tile_size_m} * 1000, data_.cargo,
               data_.balance),
      railway_(config.seed),
      sandbox_(config.sandbox),
      difficulty_(config.difficulty),
      company_("Railmaster Railroad",
               Money::dollars(config.starting_cash.value_or(data_.balance.finance.starting_cash)),
               config.start_date.year(), data_.balance),
      investor_(Investor::founder(data_.balance)) {
    terrain_.generate_rolling_hills(rng_, data_.balance.map.max_height_m);
    if (config.populate && !data_.industries.all().empty()) {
        populate_economy(economy_, terrain_, data_.cargo, data_.industries, rng_, date_.year(), data_.balance);
        // History, so the map starts with prices and cargo in place.
        economy_.settle(data_.cargo, data_.industries, date_.year(), data_.balance.economy.history_days);
    }
    railway_.set_balance(data_.balance);
    railway_.set_rules({.breakdowns = !config.sandbox});
}

void World::tick() {
    ++total_ticks_;
    railway_.set_today(date_.days_since_epoch());
    railway_.tick(data_.locomotives);
    for (TrainId id : railway_.take_crashes()) {
        company_.write_off_train(data_.locomotives.get(railway_.train(id).loco).cost);
    }
    for (const auto& [train, station] : railway_.take_arrivals()) {
        const Earnings e = handle_arrival(railway_, economy_, data_.cargo, data_.industries, train, station,
                                          date_.days_since_epoch(), total_ticks_, revenue_permille(station));
        earned_ += e.total;
        for (const auto& [cargo, amount] : e.by_cargo) company_.post(revenue_line(data_.cargo.get(cargo)), amount);
    }
    if (++tick_of_day_ < kTicksPerDay) return;
    tick_of_day_ = 0;

    const CalendarDate before = date_.calendar();
    date_ += 1;
    const CalendarDate after = date_.calendar();

    on_new_day();
    if (after.month != before.month) on_new_month();
    if (after.year != before.year) on_new_year();
}

std::int32_t World::revenue_permille(StationId s) const {
    const Station& st = railway_.station(s);
    const std::int32_t today = date_.days_since_epoch();
    std::int32_t age_permille;
    if (st.town) {
        const auto& first = economy_.towns()[*st.town].first_station_day;
        age_permille = station_age_permille(today - first.value_or(st.built_day), false);
    } else {
        age_permille = station_age_permille(today - st.built_day, true);
    }
    return difficulty_revenue_permille(difficulty_) * age_permille / 1000;
}

void World::charge_running_costs() {
    const std::int32_t today = date_.days_since_epoch();
    const Balance::Finance& f = data_.balance.finance;
    Money maintenance, fuel;
    for (const Train& t : railway_.trains()) {
        if (t.state == TrainState::Crashed) continue;
        const LocomotiveType& loco = data_.locomotives.get(t.loco);
        maintenance += annual_maintenance(loco, (today - t.built_day) / 365, t.oil, data_.balance).scaled(1, 12);
        const std::int64_t run_mm = t.distance_mm - t.fuel_billed_mm;
        fuel += Money::dollars(fuel_per_km(f, loco, t.cars.size())).scaled(run_mm, 1'000'000);
        railway_.train_mut(t.id).fuel_billed_mm = t.distance_mm;
    }
    // Easy games cut maintenance, fuel and track costs [D]; by how much is [I].
    const std::int64_t cost_pct = difficulty_ == Difficulty::Easy ? f.easy_cost_percent : 100;
    company_.post(Ledger::TrainMaintenance, maintenance.scaled(cost_pct, 100));
    company_.post(Ledger::Fuel, fuel.scaled(cost_pct, 100));
    company_.post(Ledger::TrackUpkeep,
                  company_.track_value().scaled(f.track_upkeep_per_mille_month * cost_pct, 1000 * 100));
    company_.post(Ledger::BuildingUpkeep,
                  company_.building_value().scaled(f.building_upkeep_per_mille_month, 1000));
}

void World::on_new_day() {
    economy_.step_day(data_.cargo, data_.industries, date_.year());
    gather_at_stations(railway_, economy_, data_.cargo, data_.industries, date_.year());
}

// Running costs for the month just ended are charged on the 1st, before a
// new year's accounts are opened, so December's land in the old year.
void World::on_new_month() {
    start_new_month(railway_);
    charge_running_costs();
    company_.record_month();
    last_forced_sale_ = monthly_market(investor_, company_);
    // Bond interest [D] and dividends are paid at the end of each quarter.
    if ((date_.month() - 1) % 3 == 0) {
        company_.charge_interest(3);
        pay_dividends(investor_, company_);
    }
}

void World::on_new_year() {
    company_.retire_matured_bonds(date_.year());
    company_.start_year(date_.year());
}

} // namespace railmaster::sim
