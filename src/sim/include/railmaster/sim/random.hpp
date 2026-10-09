#pragma once

#include <cstdint>

namespace railmaster::sim {

// PCG32 (https://www.pcg-random.org). The simulation must not use
// std::mt19937 with std:: distributions, because distribution output is
// implementation-defined and would differ between standard libraries.
class Random {
public:
    explicit constexpr Random(std::uint64_t seed, std::uint64_t stream = 0x5851f42d4c957f2dULL)
        : inc_((stream << 1u) | 1u) {
        next_u32();
        state_ += seed;
        next_u32();
    }

    constexpr std::uint32_t next_u32() {
        std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // Uniform integer in [0, bound), without modulo bias.
    constexpr std::uint32_t below(std::uint32_t bound) {
        if (bound == 0) return 0;
        std::uint32_t threshold = (~bound + 1u) % bound;
        for (;;) {
            std::uint32_t r = next_u32();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform integer in [lo, hi].
    constexpr std::int32_t between(std::int32_t lo, std::int32_t hi) {
        auto span = static_cast<std::uint32_t>(static_cast<std::int64_t>(hi) - lo + 1);
        return static_cast<std::int32_t>(lo + static_cast<std::int64_t>(below(span)));
    }

    // True with probability num/den.
    constexpr bool chance(std::uint32_t num, std::uint32_t den) { return below(den) < num; }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_;
};

} // namespace railmaster::sim
