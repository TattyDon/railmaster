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

std::int64_t fuel_per_km(const LocomotiveType& loco, std::size_t cars) {
    std::int64_t base = provisional::kFuelPerKmSteam;
    if (loco.fuel == Fuel::Diesel) base = provisional::kFuelPerKmDiesel;
    if (loco.fuel == Fuel::Electric) base = provisional::kFuelPerKmElectric;
    return base + provisional::kFuelPerKmPerCar * static_cast<std::int64_t>(cars);
}

} // namespace

World::World(const WorldConfig& config, GameData data)
    : rng_(config.seed),
      date_(config.start_date),
      terrain_(config.width_tiles, config.height_tiles, config.tile_size_m),
      data_(std::move(data)),
      economy_(config.width_tiles, config.height_tiles, std::int64_t{config.tile_size_m} * 1000, data_.cargo),
      railway_(config.seed),
      sandbox_(config.sandbox),
      company_("Railmaster Railroad", Money::dollars(config.starting_cash), config.start_date.year()) {
    terrain_.generate_rolling_hills(rng_, 400);
    if (config.populate && !data_.industries.all().empty()) {
        populate_economy(economy_, terrain_, data_.cargo, data_.industries, rng_, date_.year());
        // A year of history, so the map starts with prices and cargo in place.
        economy_.settle(data_.cargo, data_.industries, date_.year(), 365);
    }
    railway_.set_rules({.breakdowns = !config.sandbox});
}

void World::tick() {
    ++total_ticks_;
    railway_.tick(data_.locomotives);
    for (const auto& [train, station] : railway_.take_arrivals()) {
        const Earnings e = handle_arrival(railway_, economy_, data_.cargo, data_.industries, train, station,
                                          date_.days_since_epoch(), total_ticks_);
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

void World::charge_running_costs() {
    const std::int32_t today = date_.days_since_epoch();
    Money maintenance, fuel;
    for (const Train& t : railway_.trains()) {
        const LocomotiveType& loco = data_.locomotives.get(t.loco);
        maintenance += annual_maintenance(loco, (today - t.built_day) / 365, t.oil).scaled(1, 12);
        const std::int64_t run_mm = t.distance_mm - t.fuel_billed_mm;
        fuel += Money::dollars(fuel_per_km(loco, t.cars.size())).scaled(run_mm, 1'000'000);
        railway_.train_mut(t.id).fuel_billed_mm = t.distance_mm;
    }
    company_.post(Ledger::TrainMaintenance, maintenance);
    company_.post(Ledger::Fuel, fuel);
    company_.post(Ledger::TrackUpkeep, company_.track_value().scaled(provisional::kTrackUpkeepPerMillePerMonth, 1000));
    company_.post(Ledger::BuildingUpkeep,
                  company_.building_value().scaled(provisional::kBuildingUpkeepPerMillePerMonth, 1000));
    company_.charge_interest();
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
}

void World::on_new_year() { company_.start_year(date_.year()); }

} // namespace railmaster::sim
