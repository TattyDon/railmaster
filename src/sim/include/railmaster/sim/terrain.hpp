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

// A landscape made by hand (from a heightmap image, say), for a new map in
// place of the generated hills and rivers: heights at the tile corners and
// which tiles are water.
struct Landscape {
    std::int32_t width = 0, height = 0;  // in tiles
    std::vector<std::int32_t> corners{}; // metres, (width + 1) x (height + 1), row by row
    std::vector<std::uint8_t> water{};   // width x height, 1 for water
};

// A landscape from an image (RGB, row by row from the top): brightness is
// height, black at 0 and white at `max_height_m`; water is where the image
// is clearly blue or its brightness is at or below `sea_level` (0-255).
// The image is stretched over a map of `map_w` x `map_h` tiles and lightly
// smoothed, so 8-bit heights do not show as terraces.
Landscape landscape_from_image(const std::uint8_t* rgb, std::int32_t image_w, std::int32_t image_h, std::int32_t map_w,
                               std::int32_t map_h, std::int32_t max_height_m, std::int32_t sea_level);

class Terrain;
// A terrain's heights and water, as a landscape (to play on, or save as a map).
Landscape landscape_of(const Terrain& t);
// A flat landscape of dry land, `metres` high (the map editor's blank page).
Landscape flat_landscape(std::int32_t width, std::int32_t height, std::int32_t metres);

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

    // Ground cover of the tile containing a map position (clamped to the map).
    GroundType ground_at_mm(MapPoint p) const;

    // Size of the whole map in millimetres.
    std::int64_t width_mm() const { return static_cast<std::int64_t>(width_) * tile_size_m_ * 1000; }
    std::int64_t height_mm() const { return static_cast<std::int64_t>(height_) * tile_size_m_ * 1000; }

    // Bumped by every change, so things derived from the terrain know when
    // to recompute.
    std::uint64_t revision() const { return revision_; }

    // Fill with gently rolling procedural hills. A stand-in until the real
    // map generator and scenario loader exist.
    void generate_rolling_hills(Random& rng, std::int32_t max_height_m);
    // Rivers: each starts on high ground (at least source_percent of the
    // highest corner) and runs to the lowest neighbouring cell until it
    // reaches water or the map edge, cutting through any hollow on the way
    // so it always runs downhill. River cells are water.
    void generate_rivers(Random& rng, std::int32_t count, std::int32_t source_percent);
    // Take a hand-made landscape's heights and water (it must be this map's
    // size); the rest of the ground is grass.
    void apply(const Landscape& l);

    // Saving and loading (save.cpp) reads and writes every member.
    template <class Ar>
    friend void io(Ar& ar, Terrain& v);

private:
    std::size_t corner_index(std::int32_t cx, std::int32_t cy) const;
    std::size_t tile_index(std::int32_t tx, std::int32_t ty) const;

    std::int32_t width_;
    std::int32_t height_;
    std::int32_t tile_size_m_;
    std::vector<std::int32_t> heights_;
    std::vector<GroundType> ground_;
    std::uint64_t revision_ = 0;
};

} // namespace railmaster::sim
