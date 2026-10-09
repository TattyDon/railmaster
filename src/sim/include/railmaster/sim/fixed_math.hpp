#pragma once

#include <cstdint>

namespace railmaster::sim {

// Integer square root (floor). The simulation uses this instead of
// std::sqrt so distances are bit-identical on every platform.
constexpr std::int64_t isqrt(std::int64_t n) {
    if (n <= 0) return 0;
    std::int64_t x = n;
    std::int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Horizontal positions on the map, in millimetres.
struct MapPoint {
    std::int64_t x_mm = 0;
    std::int64_t y_mm = 0;

    constexpr bool operator==(const MapPoint&) const = default;
};

constexpr std::int64_t distance_mm(MapPoint a, MapPoint b) {
    const std::int64_t dx = b.x_mm - a.x_mm;
    const std::int64_t dy = b.y_mm - a.y_mm;
    return isqrt(dx * dx + dy * dy);
}

} // namespace railmaster::sim
