#include "railmaster/sim/date.hpp"

#include <doctest/doctest.h>

using namespace railmaster::sim;

TEST_CASE("epoch is day zero") {
    CHECK(Date::from_ymd(1800, 1, 1).days_since_epoch() == 0);
}

TEST_CASE("calendar round-trips across two centuries") {
    for (std::int32_t d = 0; d < 365 * 250; d += 7) {
        const Date date = Date::from_days(d);
        CHECK(Date::from_calendar(date.calendar()) == date);
    }
}

TEST_CASE("leap years follow the Gregorian rule") {
    CHECK(is_leap_year(1832));
    CHECK_FALSE(is_leap_year(1900));
    CHECK(is_leap_year(2000));
    CHECK(days_in_month(1900, 2) == 28);
    CHECK(days_in_month(1904, 2) == 29);
    CHECK(Date::from_ymd(1901, 1, 1) - Date::from_ymd(1900, 1, 1) == 365);
}

TEST_CASE("invalid dates are rejected") {
    CHECK_THROWS(Date::from_ymd(1850, 2, 29));
    CHECK_THROWS(Date::from_ymd(1850, 13, 1));
}

TEST_CASE("month-year label") {
    CHECK(Date::from_ymd(1856, 3, 14).month_year_label() == "Mar 1856");
}
