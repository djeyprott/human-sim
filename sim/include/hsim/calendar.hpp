#pragma once
// Simulation time.
//
// Time is a signed count of days. Day 0 is 1 January 1 CE in the proleptic
// Gregorian calendar. Years are shown historian-style: there is no year zero,
// so the year before 1 CE is 1 BCE. Internally we use astronomical years
// (1 BCE = 0, 2 BCE = -1, ...), which makes arithmetic simple.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hsim {

using Day = std::int64_t;

struct Date {
    std::int64_t year = 1;  // astronomical year: 0 = 1 BCE, -1 = 2 BCE
    int month = 1;          // 1..12
    int day = 1;            // 1..31

    friend bool operator==(const Date&, const Date&) = default;
};

Day to_day(const Date& date);
Date to_date(Day day);

bool is_leap_year(std::int64_t astronomical_year);
int days_in_month(std::int64_t astronomical_year, int month);

// Historian-style year, e.g. -9999 -> "10000 BCE", 1500 -> "1500 CE".
std::string format_year(std::int64_t astronomical_year);
// e.g. "3 Mar 10000 BCE".
std::string format_date(Day day);

// Parses "10000BCE", "10000 BCE", "1500CE", "1500 CE", "1500" (CE),
// or "-9999" (astronomical). Returns the astronomical year.
std::optional<std::int64_t> parse_year(std::string_view text);

// The first day of a historian-style year given as astronomical.
inline Day first_day_of_year(std::int64_t astronomical_year) { return to_day(Date{astronomical_year, 1, 1}); }

}  // namespace hsim
