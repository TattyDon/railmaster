#include "railmaster/sim/world.hpp"

#include <utility>

namespace railmaster::sim {

World::World(const WorldConfig& config, GameData data)
    : rng_(config.seed),
      date_(config.start_date),
      terrain_(config.width_tiles, config.height_tiles, config.tile_size_m),
      data_(std::move(data)),
      railway_(config.seed) {
    terrain_.generate_rolling_hills(rng_, 400);
    railway_.set_rules({.breakdowns = !config.sandbox});
}

void World::tick() {
    ++total_ticks_;
    railway_.tick(data_.locomotives);
    if (++tick_of_day_ < kTicksPerDay) return;
    tick_of_day_ = 0;

    const CalendarDate before = date_.calendar();
    date_ += 1;
    const CalendarDate after = date_.calendar();

    on_new_day();
    if (after.month != before.month) on_new_month();
    if (after.year != before.year) on_new_year();
}

// Periodic hooks. Economy, maintenance and finance processing get wired in
// here as those systems are implemented from docs/spec.
void World::on_new_day() {}
void World::on_new_month() {}
void World::on_new_year() {}

} // namespace railmaster::sim
