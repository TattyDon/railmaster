#pragma once

#include "railmaster/sim/date.hpp"
#include "railmaster/sim/random.hpp"
#include "railmaster/sim/terrain.hpp"

#include <cstdint>

namespace railmaster::sim {

struct WorldConfig {
    std::uint64_t seed = 1;
    Date start_date = Date::from_ymd(1830, 1, 1);
    std::int32_t width_tiles = 128;
    std::int32_t height_tiles = 128;
    std::int32_t tile_size_m = 100;
};

// Root of all simulation state. Advancing it is a pure function of its
// current state; the client never mutates it except by submitting commands
// (to be added), which keeps replays and lockstep multiplayer possible.
class World {
public:
    // Fixed simulation steps per game day. Provisional; the real value
    // should come from measuring train movement in the original game.
    static constexpr std::int32_t kTicksPerDay = 16;

    explicit World(const WorldConfig& config);

    void tick();

    Date date() const { return date_; }
    std::int32_t tick_of_day() const { return tick_of_day_; }
    std::uint64_t total_ticks() const { return total_ticks_; }
    const Terrain& terrain() const { return terrain_; }
    Terrain& terrain() { return terrain_; }

private:
    void on_new_day();
    void on_new_month();
    void on_new_year();

    Random rng_;
    Date date_;
    std::int32_t tick_of_day_ = 0;
    std::uint64_t total_ticks_ = 0;
    Terrain terrain_;
};

} // namespace railmaster::sim
