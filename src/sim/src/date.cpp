#include "railmaster/sim/date.hpp"

#include <array>
#include <stdexcept>

namespace railmaster::sim {

namespace {

// Howard Hinnant's days_from_civil / civil_from_days algorithms, which are
// exact for the proleptic Gregorian calendar.
std::int64_t days_from_civil(std::int64_t y, std::int64_t m, std::int64_t d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

CalendarDate civil_from_days(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp + (mp < 10 ? 3 : -9);
    return {static_cast<std::int32_t>(y + (m <= 2)), static_cast<std::int32_t>(m),
            static_cast<std::int32_t>(d)};
}

const std::int64_t kEpochOffset = days_from_civil(Date::kEpochYear, 1, 1);

} // namespace

bool is_leap_year(std::int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

std::int32_t days_in_month(std::int32_t year, std::int32_t month) {
    static constexpr std::array<std::int32_t, 12> kDays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) throw std::out_of_range("month out of range");
    if (month == 2 && is_leap_year(year)) return 29;
    return kDays[static_cast<std::size_t>(month - 1)];
}

Date Date::from_calendar(CalendarDate c) {
    if (c.day < 1 || c.day > days_in_month(c.year, c.month)) throw std::out_of_range("day out of range");
    return Date{static_cast<std::int32_t>(days_from_civil(c.year, c.month, c.day) - kEpochOffset)};
}

CalendarDate Date::calendar() const {
    return civil_from_days(static_cast<std::int64_t>(days_) + kEpochOffset);
}

std::string Date::month_year_label() const {
    static constexpr std::array<const char*, 12> kNames{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const CalendarDate c = calendar();
    return std::string(kNames[static_cast<std::size_t>(c.month - 1)]) + " " + std::to_string(c.year);
}

} // namespace railmaster::sim
