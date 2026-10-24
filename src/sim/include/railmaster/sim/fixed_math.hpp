#pragma once

#include <algorithm>
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

// log2(num / den) in thousandths, for positive num and den, held within
// +/- `cap` doublings. Integer-only, so it is the same on every machine.
inline std::int64_t log2_permille(std::int64_t num, std::int64_t den, std::int64_t cap) {
    std::int64_t whole = 0;
    // Bring the ratio into [1, 2), counting doublings.
    while (num >= 2 * den && whole < cap) {
        den *= 2;
        ++whole;
    }
    while (num < den && whole > -cap) {
        num *= 2;
        --whole;
    }
    if (whole >= cap || whole <= -cap) return whole * 1000;
    // Fraction by repeated squaring of x = num/den in 2^30 fixed point.
    constexpr std::int64_t kOne = std::int64_t{1} << 30;
    while (den >= (std::int64_t{1} << 31)) { // keep num x 2^30 in range; num < 2 den
        num /= 2;
        den /= 2;
    }
    std::int64_t x = num * kOne / std::max<std::int64_t>(1, den);
    std::int64_t frac = 0; // in 2^-20
    for (int i = 19; i >= 0; --i) {
        x = (x * x) >> 30; // x < 2^31, so this fits
        if (x >= 2 * kOne) {
            x /= 2;
            frac += std::int64_t{1} << i;
        }
    }
    return whole * 1000 + (frac * 1000 >> 20);
}

// 2^(x / 1000), in thousandths, for x within about +/- 40,000. Integer-only:
// the fraction is built from 2^(1/2), 2^(1/4) ... 2^(1/1024).
inline std::int64_t exp2_permille(std::int64_t x) {
    static constexpr std::int64_t kRoots[] = {1414213562, 1189207115, 1090507733, 1044273782, 1021897149,
                                              1010889286, 1005429901, 1002711275, 1001354720, 1000677131};
    std::int64_t whole = x >= 0 ? x / 1000 : -((-x + 999) / 1000);
    const std::int64_t bits = (x - whole * 1000) * 1024 / 1000; // the fraction in 1/1024ths
    std::int64_t v = 1'000'000'000;                             // 1.0 in 1e9
    for (int i = 0; i < 10; ++i)
        if (bits & (std::int64_t{512} >> i)) v = v * kRoots[i] / 1'000'000'000;
    for (; whole > 0; --whole) v *= 2;
    for (; whole < 0; ++whole) v /= 2;
    return v / 1'000'000;
}

// (num / den) ^ (alpha / 1000), in thousandths.
inline std::int64_t pow_ratio_permille(std::int64_t num, std::int64_t den, std::int64_t alpha_permille) {
    return exp2_permille(log2_permille(std::max<std::int64_t>(1, num), std::max<std::int64_t>(1, den), 30) *
                         alpha_permille / 1000);
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
