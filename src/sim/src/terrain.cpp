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
    ++revision_;
}

GroundType Terrain::ground(std::int32_t tx, std::int32_t ty) const { return ground_[tile_index(tx, ty)]; }

void Terrain::set_ground(std::int32_t tx, std::int32_t ty, GroundType g) {
    ground_[tile_index(tx, ty)] = g;
    ++revision_;
}

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

void Terrain::generate_rivers(Random& rng, std::int32_t count, std::int32_t source_percent) {
    if (width_ < 3 || height_ < 3) return;
    std::int32_t highest = 0;
    for (std::int32_t h : heights_) highest = std::max(highest, h);
    const std::int32_t source_min = highest * source_percent / 100;
    // A cell's level: its lowest corner.
    const auto level = [&](std::int32_t x, std::int32_t y) {
        return std::min({corner_height(x, y), corner_height(x + 1, y), corner_height(x, y + 1), corner_height(x + 1, y + 1)});
    };
    for (std::int32_t r = 0; r < count; ++r) {
        std::int32_t x = -1, y = -1;
        for (int attempt = 0; attempt < 200; ++attempt) {
            const std::int32_t tx = rng.between(1, width_ - 2), ty = rng.between(1, height_ - 2);
            if (ground(tx, ty) != GroundType::Water && level(tx, ty) >= source_min) {
                x = tx;
                y = ty;
                break;
            }
        }
        if (x < 0) continue;
        int heading = -1; // the way it last flowed: kept on level ground
        for (std::int32_t steps = 0; steps < 4 * (width_ + height_); ++steps) {
            const std::int32_t here = level(x, y);
            set_ground(x, y, GroundType::Water);
            if (x == 0 || y == 0 || x == width_ - 1 || y == height_ - 1) break; // out to the edge
            // The lowest neighbour that is not river already behind us. On
            // a tie it carries straight on; otherwise east, south, west, north.
            static constexpr std::int32_t kDx[] = {1, 0, -1, 0}, kDy[] = {0, 1, 0, -1};
            std::int32_t nx = -1, ny = -1, best = 0;
            int way = -1;
            bool into_water = false;
            for (int d = 0; d < 4; ++d) {
                const std::int32_t cx = x + kDx[d], cy = y + kDy[d];
                const std::int32_t l = level(cx, cy);
                if (ground(cx, cy) == GroundType::Water) {
                    if (l < here) into_water = true; // the sea, a lake or another river below
                    continue;
                }
                if (nx < 0 || l < best || (l == best && d == heading)) {
                    nx = cx;
                    ny = cy;
                    best = l;
                    way = d;
                }
            }
            heading = way;
            if (into_water || nx < 0) break;
            // Cut through a hollow's rim so the river keeps running down.
            for (std::int32_t cy = ny; cy <= ny + 1; ++cy)
                for (std::int32_t cx = nx; cx <= nx + 1; ++cx)
                    if (corner_height(cx, cy) > here) set_corner_height(cx, cy, here);
            x = nx;
            y = ny;
        }
    }
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

Landscape landscape_from_image(const std::uint8_t* rgb, std::int32_t image_w, std::int32_t image_h, std::int32_t map_w,
                               std::int32_t map_h, std::int32_t max_height_m, std::int32_t sea_level) {
    if (image_w < 2 || image_h < 2 || map_w < 1 || map_h < 1) throw std::invalid_argument("landscape: image or map too small");
    // Each pixel: its height in thousandths of the maximum, and whether it is water.
    const auto pixels = static_cast<std::size_t>(image_w) * static_cast<std::size_t>(image_h);
    std::vector<std::int32_t> level(pixels);
    std::vector<std::uint8_t> wet(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::int32_t r = rgb[3 * i], g = rgb[3 * i + 1], b = rgb[3 * i + 2];
        const std::int32_t grey = (r * 299 + g * 587 + b * 114) / 1000;
        const bool blue = b > r + 40 && b > g + 20;
        wet[i] = blue || grey <= sea_level ? 1 : 0;
        // Water lies at the sea's level, whatever its shade of blue.
        level[i] = (wet[i] ? std::min(grey, sea_level) : grey) * 1000 / 255;
    }
    // Sample the image at a map position (in pixels, fractional), bilinearly.
    const auto sample = [&](std::int64_t x_milli, std::int64_t y_milli) {
        const std::int64_t max_x = (image_w - 1) * std::int64_t{1000}, max_y = (image_h - 1) * std::int64_t{1000};
        x_milli = std::clamp<std::int64_t>(x_milli, 0, max_x);
        y_milli = std::clamp<std::int64_t>(y_milli, 0, max_y);
        const auto x0 = static_cast<std::int32_t>(std::min<std::int64_t>(x_milli / 1000, image_w - 2));
        const auto y0 = static_cast<std::int32_t>(std::min<std::int64_t>(y_milli / 1000, image_h - 2));
        const std::int64_t fx = x_milli - std::int64_t{x0} * 1000, fy = y_milli - std::int64_t{y0} * 1000;
        const auto at = [&](std::int32_t x, std::int32_t y) {
            return std::int64_t{level[static_cast<std::size_t>(y) * static_cast<std::size_t>(image_w) + static_cast<std::size_t>(x)]};
        };
        const std::int64_t top = at(x0, y0) * (1000 - fx) + at(x0 + 1, y0) * fx;
        const std::int64_t bot = at(x0, y0 + 1) * (1000 - fx) + at(x0 + 1, y0 + 1) * fx;
        return (top * (1000 - fy) + bot * fy) / 1'000'000; // thousandths of the maximum
    };
    Landscape l;
    l.width = map_w;
    l.height = map_h;
    const std::int32_t cw = map_w + 1, ch = map_h + 1;
    std::vector<std::int64_t> raw(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch));
    for (std::int32_t y = 0; y < ch; ++y) {
        for (std::int32_t x = 0; x < cw; ++x) {
            raw[static_cast<std::size_t>(y) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(x)] =
                sample(std::int64_t{x} * (image_w - 1) * 1000 / map_w, std::int64_t{y} * (image_h - 1) * 1000 / map_h);
        }
    }
    // A light 3 x 3 smoothing.
    l.corners.resize(raw.size());
    for (std::int32_t y = 0; y < ch; ++y) {
        for (std::int32_t x = 0; x < cw; ++x) {
            std::int64_t sum = 0, n = 0;
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                for (std::int32_t dx = -1; dx <= 1; ++dx) {
                    const std::int32_t nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) continue;
                    const std::int64_t weight = (dx == 0 && dy == 0) ? 4 : (dx == 0 || dy == 0) ? 2 : 1;
                    sum += raw[static_cast<std::size_t>(ny) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(nx)] * weight;
                    n += weight;
                }
            }
            l.corners[static_cast<std::size_t>(y) * static_cast<std::size_t>(cw) + static_cast<std::size_t>(x)] =
                static_cast<std::int32_t>(sum / n * max_height_m / 1000);
        }
    }
    // Water: the pixel under each tile's middle.
    l.water.resize(static_cast<std::size_t>(map_w) * static_cast<std::size_t>(map_h));
    for (std::int32_t y = 0; y < map_h; ++y) {
        for (std::int32_t x = 0; x < map_w; ++x) {
            const auto px = static_cast<std::int32_t>(std::min<std::int64_t>((2 * std::int64_t{x} + 1) * image_w / (2 * map_w), image_w - 1));
            const auto py = static_cast<std::int32_t>(std::min<std::int64_t>((2 * std::int64_t{y} + 1) * image_h / (2 * map_h), image_h - 1));
            l.water[static_cast<std::size_t>(y) * static_cast<std::size_t>(map_w) + static_cast<std::size_t>(x)] =
                wet[static_cast<std::size_t>(py) * static_cast<std::size_t>(image_w) + static_cast<std::size_t>(px)];
        }
    }
    return l;
}

Landscape landscape_of(const Terrain& t) {
    Landscape l;
    l.width = t.width();
    l.height = t.height();
    l.corners.reserve(static_cast<std::size_t>(t.width() + 1) * static_cast<std::size_t>(t.height() + 1));
    for (std::int32_t cy = 0; cy <= t.height(); ++cy)
        for (std::int32_t cx = 0; cx <= t.width(); ++cx) l.corners.push_back(t.corner_height(cx, cy));
    l.water.reserve(static_cast<std::size_t>(t.width()) * static_cast<std::size_t>(t.height()));
    for (std::int32_t ty = 0; ty < t.height(); ++ty)
        for (std::int32_t tx = 0; tx < t.width(); ++tx) l.water.push_back(t.ground(tx, ty) == GroundType::Water ? 1 : 0);
    return l;
}

Landscape flat_landscape(std::int32_t width, std::int32_t height, std::int32_t metres) {
    Landscape l;
    l.width = width;
    l.height = height;
    l.corners.assign(static_cast<std::size_t>(width + 1) * static_cast<std::size_t>(height + 1), metres);
    l.water.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    return l;
}

void Terrain::apply(const Landscape& l) {
    if (l.width != width_ || l.height != height_) throw std::invalid_argument("landscape: not this map's size");
    for (std::int32_t cy = 0; cy <= height_; ++cy) {
        for (std::int32_t cx = 0; cx <= width_; ++cx) {
            set_corner_height(cx, cy, l.corners[static_cast<std::size_t>(cy) * static_cast<std::size_t>(width_ + 1) + static_cast<std::size_t>(cx)]);
        }
    }
    for (std::int32_t ty = 0; ty < height_; ++ty) {
        for (std::int32_t tx = 0; tx < width_; ++tx) {
            const bool water = l.water[static_cast<std::size_t>(ty) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(tx)] != 0;
            set_ground(tx, ty, water ? GroundType::Water : GroundType::Grass);
        }
    }
}

} // namespace railmaster::sim
