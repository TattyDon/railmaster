#pragma once

#include <compare>
#include <cstdint>

namespace railmaster::sim {

// Money is stored as a whole number of cents so that the simulation never
// touches floating point for balances. Floating point differs across
// compilers and CPUs, and would make multiplayer lockstep desync.
class Money {
public:
    constexpr Money() = default;

    static constexpr Money cents(std::int64_t c) { return Money{c}; }
    static constexpr Money dollars(std::int64_t d) { return Money{d * 100}; }

    constexpr std::int64_t in_cents() const { return cents_; }
    constexpr std::int64_t whole_dollars() const { return cents_ / 100; }

    constexpr Money operator+(Money o) const { return Money{cents_ + o.cents_}; }
    constexpr Money operator-(Money o) const { return Money{cents_ - o.cents_}; }
    constexpr Money operator-() const { return Money{-cents_}; }
    constexpr Money operator*(std::int64_t k) const { return Money{cents_ * k}; }
    constexpr Money& operator+=(Money o) { cents_ += o.cents_; return *this; }
    constexpr Money& operator-=(Money o) { cents_ -= o.cents_; return *this; }

    // Scale by a rational factor, rounding toward zero.
    constexpr Money scaled(std::int64_t num, std::int64_t den) const {
        return Money{cents_ * num / den};
    }

    constexpr auto operator<=>(const Money&) const = default;

private:
    constexpr explicit Money(std::int64_t c) : cents_(c) {}
    std::int64_t cents_ = 0;
};

} // namespace railmaster::sim
