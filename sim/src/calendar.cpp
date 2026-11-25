#include "hsim/calendar.hpp"

#include <cctype>
#include <charconv>

namespace hsim {

namespace {
// Howard Hinnant's civil-date algorithms (public domain), shifted so that
// day 0 is 0001-01-01. They are exact for all representable years.
// His functions count days from 1970-01-01; 0001-01-01 is 719162 days before that.
constexpr std::int64_t kEpochShift = 719162;

std::int64_t days_from_civil(std::int64_t y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;                                         // [0, 399]
    const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;        // [0, 365]
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                 // [0, 146096]
    return era * 146097 + doe - 719468;                                             // days since 1970-01-01
}

Date civil_from_days(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    return Date{y + (m <= 2), m, d};
}

constexpr const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
}  // namespace

Day to_day(const Date& date) { return days_from_civil(date.year, date.month, date.day) + kEpochShift; }

Date to_date(Day day) { return civil_from_days(day - kEpochShift); }

bool is_leap_year(std::int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(std::int64_t y, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(y)) return 29;
    return kDays[month - 1];
}

std::string format_year(std::int64_t y) {
    if (y <= 0) return std::to_string(1 - y) + " BCE";
    return std::to_string(y) + " CE";
}

std::string format_date(Day day) {
    Date d = to_date(day);
    return std::to_string(d.day) + " " + kMonthNames[d.month - 1] + " " + format_year(d.year);
}

std::optional<std::int64_t> parse_year(std::string_view text) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (s.empty()) return std::nullopt;

    bool bce = false;
    if (s.size() > 3 && s.ends_with("BCE")) {
        bce = true;
        s.resize(s.size() - 3);
    } else if (s.size() > 2 && s.ends_with("BC")) {
        bce = true;
        s.resize(s.size() - 2);
    } else if (s.size() > 2 && s.ends_with("CE")) {
        s.resize(s.size() - 2);
    } else if (s.size() > 2 && s.ends_with("AD")) {
        s.resize(s.size() - 2);
    }

    std::int64_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || ptr != s.data() + s.size()) return std::nullopt;

    if (bce) {
        if (value < 1) return std::nullopt;
        return 1 - value;
    }
    return value;  // CE years, or an astronomical year if negative/zero was typed
}

}  // namespace hsim
