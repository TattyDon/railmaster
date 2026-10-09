#include "railmaster/sim/world.hpp"

#include "railmaster/sim/freight.hpp"

#include <utility>

namespace railmaster::sim {

World::World(const WorldConfig& config, GameData data)
    : rng_(config.seed),
      date_(config.start_date),
      terrain_(config.width_tiles, config.height_tiles, config.tile_size_m),
      data_(std::move(data)),
      economy_(config.width_tiles, config.height_tiles, std::int64_t{config.tile_size_m} * 1000, data_.cargo),
      railway_(config.seed) {
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
        earned_ += handle_arrival(railway_, economy_, data_.cargo, train, station, date_.days_since_epoch(),
                                  total_ticks_);
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

// Periodic hooks. Maintenance and finance processing get wired in here as
// those systems are implemented from docs/spec.
void World::on_new_day() {
    economy_.step_day(data_.cargo, data_.industries, date_.year());
    gather_at_stations(railway_, economy_, data_.cargo);
}
void World::on_new_month() {}
void World::on_new_year() {}

} // namespace railmaster::sim
