#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace railmaster::sim {

struct CalendarDate {
    std::int32_t year;
    std::int32_t month; // 1..12
    std::int32_t day;   // 1..31

    auto operator<=>(const CalendarDate&) const = default;
};

// A point in game time, stored as a day count since 1 January 1800
// (proleptic Gregorian). The original game spans roughly 1830-2030, and
// scenarios may start earlier, so 1800 gives some headroom.
class Date {
public:
    static constexpr std::int32_t kEpochYear = 1800;

    constexpr Date() = default;
    static constexpr Date from_days(std::int32_t days) { return Date{days}; }
    static Date from_calendar(CalendarDate c);
    static Date from_ymd(std::int32_t y, std::int32_t m, std::int32_t d) { return from_calendar({y, m, d}); }

    constexpr std::int32_t days_since_epoch() const { return days_; }
    CalendarDate calendar() const;
    std::int32_t year() const { return calendar().year; }
    std::int32_t month() const { return calendar().month; }

    // "Mar 1856"-style label, used in the UI status bar.
    std::string month_year_label() const;

    constexpr Date operator+(std::int32_t days) const { return Date{days_ + days}; }
    constexpr std::int32_t operator-(Date o) const { return days_ - o.days_; }
    constexpr Date& operator+=(std::int32_t days) { days_ += days; return *this; }
    constexpr auto operator<=>(const Date&) const = default;

private:
    constexpr explicit Date(std::int32_t days) : days_(days) {}
    std::int32_t days_ = 0;
};

bool is_leap_year(std::int32_t year);
std::int32_t days_in_month(std::int32_t year, std::int32_t month);

} // namespace railmaster::sim
