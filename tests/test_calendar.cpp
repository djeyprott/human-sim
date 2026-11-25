#include <doctest/doctest.h>

#include "hsim/calendar.hpp"

using namespace hsim;

TEST_CASE("calendar: day 0 is 1 January 1 CE") {
    CHECK(to_day(Date{1, 1, 1}) == 0);
    CHECK(to_date(0) == Date{1, 1, 1});
    CHECK(to_date(-1) == Date{0, 12, 31});  // 31 Dec 1 BCE
}

TEST_CASE("calendar: round trips across eras") {
    for (Day d = to_day(Date{-12000, 1, 1}); d < to_day(Date{2100, 1, 1}); d += 997) {
        Date date = to_date(d);
        CHECK(to_day(date) == d);
        CHECK(date.month >= 1);
        CHECK(date.month <= 12);
        CHECK(date.day >= 1);
        CHECK(date.day <= days_in_month(date.year, date.month));
    }
}

TEST_CASE("calendar: consecutive days advance the date by one") {
    Date prev = to_date(to_day(Date{-9999, 1, 1}) - 1);
    for (Day d = to_day(Date{-9999, 1, 1}); d < to_day(Date{-9995, 1, 1}); ++d) {
        Date cur = to_date(d);
        if (cur.day == 1) {
            CHECK(prev.day == days_in_month(prev.year, prev.month));
        } else {
            CHECK(cur.day == prev.day + 1);
        }
        prev = cur;
    }
}

TEST_CASE("calendar: leap years") {
    CHECK(is_leap_year(2000));
    CHECK_FALSE(is_leap_year(1900));
    CHECK(is_leap_year(2024));
    CHECK(is_leap_year(0));      // 1 BCE
    CHECK(is_leap_year(-4));     // 5 BCE
    CHECK(days_in_month(0, 2) == 29);
}

TEST_CASE("calendar: historian-style years") {
    CHECK(format_year(1) == "1 CE");
    CHECK(format_year(0) == "1 BCE");
    CHECK(format_year(-9999) == "10000 BCE");
    CHECK(format_date(to_day(Date{-9999, 3, 4})) == "4 Mar 10000 BCE");
}

TEST_CASE("calendar: parsing years") {
    CHECK(parse_year("10000BCE") == -9999);
    CHECK(parse_year("10000 bce") == -9999);
    CHECK(parse_year("1 BC") == 0);
    CHECK(parse_year("1500CE") == 1500);
    CHECK(parse_year("1500 AD") == 1500);
    CHECK(parse_year("1500") == 1500);
    CHECK(parse_year("-9999") == -9999);
    CHECK_FALSE(parse_year("abc").has_value());
    CHECK_FALSE(parse_year("0BCE").has_value());
    CHECK_FALSE(parse_year("").has_value());
}
