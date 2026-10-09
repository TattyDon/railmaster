#pragma once

#include "railmaster/sim/fixed_math.hpp"

#include <cstdint>
#include <vector>

namespace railmaster::sim {

class Random;

// Ground cover. Placeholder set, to be reconciled with docs/spec once
// terrain types and their construction-cost multipliers are documented.
enum class GroundType : std::uint8_t {
    Grass,
    Farmland,
    Forest,
    Desert,
    Rock,
    Snow,
    Water,
};

// Heightfield terrain. Heights are sampled at tile *corners*, so a map of
// W x H tiles has (W+1) x (H+1) height samples. Heights are whole metres
// to keep slope computation in integer arithmetic.
class Terrain {
public:
    Terrain(std::int32_t width_tiles, std::int32_t height_tiles, std::int32_t tile_size_m);

    std::int32_t width() const { return width_; }
    std::int32_t height() const { return height_; }
    std::int32_t tile_size_m() const { return tile_size_m_; }

    std::int32_t corner_height(std::int32_t cx, std::int32_t cy) const;
    void set_corner_height(std::int32_t cx, std::int32_t cy, std::int32_t metres);

    GroundType ground(std::int32_t tx, std::int32_t ty) const;
    void set_ground(std::int32_t tx, std::int32_t ty, GroundType g);

    // Grade between two corners in hundredths of a percent (basis points of
    // rise over horizontal run). Positive means uphill from a to b.
    // Track grade drives train speed and construction cost, so it must be exact.
    std::int32_t grade_bp(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) const;

    // Ground height in millimetres at any map position, bilinearly
    // interpolated from the corners. Positions off the map are clamped.
    std::int64_t height_at_mm(MapPoint p) const;

    // Size of the whole map in millimetres.
    std::int64_t width_mm() const { return static_cast<std::int64_t>(width_) * tile_size_m_ * 1000; }
    std::int64_t height_mm() const { return static_cast<std::int64_t>(height_) * tile_size_m_ * 1000; }

    // Fill with gently rolling procedural hills. A stand-in until the real
    // map generator and scenario loader exist.
    void generate_rolling_hills(Random& rng, std::int32_t max_height_m);

private:
    std::size_t corner_index(std::int32_t cx, std::int32_t cy) const;
    std::size_t tile_index(std::int32_t tx, std::int32_t ty) const;

    std::int32_t width_;
    std::int32_t height_;
    std::int32_t tile_size_m_;
    std::vector<std::int32_t> heights_;
    std::vector<GroundType> ground_;
};

} // namespace railmaster::sim
