#include "railmaster/sim/terrain.hpp"

#include "railmaster/sim/fixed_math.hpp"
#include "railmaster/sim/random.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace railmaster::sim {

Terrain::Terrain(std::int32_t width_tiles, std::int32_t height_tiles, std::int32_t tile_size_m)
    : width_(width_tiles), height_(height_tiles), tile_size_m_(tile_size_m) {
    if (width_tiles <= 0 || height_tiles <= 0 || tile_size_m <= 0) {
        throw std::invalid_argument("terrain dimensions must be positive");
    }
    heights_.assign(static_cast<std::size_t>(width_ + 1) * static_cast<std::size_t>(height_ + 1), 0);
    ground_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), GroundType::Grass);
}

std::size_t Terrain::corner_index(std::int32_t cx, std::int32_t cy) const {
    if (cx < 0 || cy < 0 || cx > width_ || cy > height_) throw std::out_of_range("corner out of range");
    return static_cast<std::size_t>(cy) * static_cast<std::size_t>(width_ + 1) + static_cast<std::size_t>(cx);
}

std::size_t Terrain::tile_index(std::int32_t tx, std::int32_t ty) const {
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) throw std::out_of_range("tile out of range");
    return static_cast<std::size_t>(ty) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(tx);
}

std::int32_t Terrain::corner_height(std::int32_t cx, std::int32_t cy) const {
    return heights_[corner_index(cx, cy)];
}

void Terrain::set_corner_height(std::int32_t cx, std::int32_t cy, std::int32_t metres) {
    heights_[corner_index(cx, cy)] = metres;
}

GroundType Terrain::ground(std::int32_t tx, std::int32_t ty) const { return ground_[tile_index(tx, ty)]; }

void Terrain::set_ground(std::int32_t tx, std::int32_t ty, GroundType g) { ground_[tile_index(tx, ty)] = g; }

std::int32_t Terrain::grade_bp(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) const {
    const std::int64_t rise = corner_height(bx, by) - corner_height(ax, ay);
    const std::int64_t dx = static_cast<std::int64_t>(bx - ax) * tile_size_m_;
    const std::int64_t dy = static_cast<std::int64_t>(by - ay) * tile_size_m_;
    const std::int64_t run = isqrt(dx * dx + dy * dy);
    if (run == 0) throw std::invalid_argument("grade between identical corners");
    return static_cast<std::int32_t>(rise * 10000 / run);
}

std::int64_t Terrain::height_at_mm(MapPoint p) const {
    const std::int64_t tile_mm = static_cast<std::int64_t>(tile_size_m_) * 1000;
    const std::int64_t max_x = static_cast<std::int64_t>(width_) * tile_mm;
    const std::int64_t max_y = static_cast<std::int64_t>(height_) * tile_mm;
    const std::int64_t x = std::clamp<std::int64_t>(p.x_mm, 0, max_x);
    const std::int64_t y = std::clamp<std::int64_t>(p.y_mm, 0, max_y);
    const auto cx = static_cast<std::int32_t>(std::min<std::int64_t>(x / tile_mm, width_ - 1));
    const auto cy = static_cast<std::int32_t>(std::min<std::int64_t>(y / tile_mm, height_ - 1));
    const std::int64_t fx = x - static_cast<std::int64_t>(cx) * tile_mm;
    const std::int64_t fy = y - static_cast<std::int64_t>(cy) * tile_mm;
    // Bilinear interpolation of the four corner heights, all in millimetres.
    const std::int64_t h00 = corner_height(cx, cy) * 1000LL, h10 = corner_height(cx + 1, cy) * 1000LL;
    const std::int64_t h01 = corner_height(cx, cy + 1) * 1000LL, h11 = corner_height(cx + 1, cy + 1) * 1000LL;
    const std::int64_t top = h00 * (tile_mm - fx) + h10 * fx;
    const std::int64_t bot = h01 * (tile_mm - fx) + h11 * fx;
    return (top / tile_mm * (tile_mm - fy) + bot / tile_mm * fy) / tile_mm;
}

GroundType Terrain::ground_at_mm(MapPoint p) const {
    const std::int64_t tile_mm = static_cast<std::int64_t>(tile_size_m_) * 1000;
    const auto tx = static_cast<std::int32_t>(std::clamp<std::int64_t>(p.x_mm / tile_mm, 0, width_ - 1));
    const auto ty = static_cast<std::int32_t>(std::clamp<std::int64_t>(p.y_mm / tile_mm, 0, height_ - 1));
    return ground(tx, ty);
}

void Terrain::generate_rolling_hills(Random& rng, std::int32_t max_height_m) {
    // Midpoint-free value noise: random coarse lattice, bilinearly upsampled.
    constexpr std::int32_t kCell = 8;
    const std::int32_t lw = (width_ + kCell) / kCell + 1;
    const std::int32_t lh = (height_ + kCell) / kCell + 1;
    std::vector<std::int32_t> lattice(static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
    for (auto& v : lattice) v = rng.between(0, max_height_m);

    auto at = [&](std::int32_t x, std::int32_t y) {
        return lattice[static_cast<std::size_t>(y) * static_cast<std::size_t>(lw) + static_cast<std::size_t>(x)];
    };

    for (std::int32_t cy = 0; cy <= height_; ++cy) {
        for (std::int32_t cx = 0; cx <= width_; ++cx) {
            const std::int32_t gx = cx / kCell, gy = cy / kCell;
            const std::int32_t fx = cx % kCell, fy = cy % kCell;
            const std::int32_t top = at(gx, gy) * (kCell - fx) + at(gx + 1, gy) * fx;
            const std::int32_t bot = at(gx, gy + 1) * (kCell - fx) + at(gx + 1, gy + 1) * fx;
            set_corner_height(cx, cy, (top * (kCell - fy) + bot * fy) / (kCell * kCell));
        }
    }

    const std::int32_t water_line = max_height_m / 8;
    for (std::int32_t ty = 0; ty < height_; ++ty) {
        for (std::int32_t tx = 0; tx < width_; ++tx) {
            const std::int32_t lowest = std::min({corner_height(tx, ty), corner_height(tx + 1, ty),
                                                  corner_height(tx, ty + 1), corner_height(tx + 1, ty + 1)});
            set_ground(tx, ty, lowest < water_line ? GroundType::Water : GroundType::Grass);
        }
    }
}

} // namespace railmaster::sim
