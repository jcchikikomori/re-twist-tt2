#pragma once

// Word of the Day clocks (docs/reverse-engineering/game-scene.md §8.1).
//
// The original reads three different clocks for one Word of the Day game:
//
//   - word day   (0x43d690): `(uint32)time() / 86400 - 14125`, UTC; picks
//                the rack word.
//   - scene day  (0x425c50, 0x42f1a0, 0x42a460): `(uint32)(_time64() -
//                tz * 60) / 86400 - 14125`, local *standard* time (the
//                `_ftime64_s` `timezone` field; `dstflag` is never read).
//                Drives the menu gate, the save `DAY` and `player.WOD`.
//   - date text  (0x43ff60): `GetSystemTime` → `yyyy'-'MM'-'dd`, the UTC
//                calendar date written next to a high score.
//
// `WotdDayRule::Original` copies them. `WotdDayRule::Local` is the port's
// IMPROVEMENTS option (deviation UX-12): all three follow the player's
// local calendar day, DST included.
//
// The exe divides the low 32 bits unsigned; the port uses floor division,
// which gives the same day for any real clock and keeps days consecutive
// before 1970. Pure: no clock or time-zone calls, the front end fills
// `WallClock` (wall_clock_from turns its C runtime readings into one).

#include <cstdint>
#include <ctime>
#include <string>

namespace tt2 {

// Day 0 of Word of the Day: 2008-09-03 (unix day 14125, CONFIRMED
// at 0x43d71e: `sub edx, 0x372d` = -14125).
inline constexpr std::int64_t WOTD_EPOCH_DAY = 14125;

inline constexpr std::int64_t SECONDS_PER_DAY = 86'400;

// One reading of the wall clock, taken by the front end.
struct WallClock {
    std::int64_t unix_ms = 0;            // milliseconds since 1970-01-01 UTC
    std::int32_t tz_minutes_west = 0;    // standard-time offset, west positive (`_ftime64_s` timezone)
    std::int32_t utc_offset_minutes = 0; // real offset including DST, east positive (Local rule only)
};

enum class WotdDayRule {
    Original,  // the exe's three clocks (default)
    Local,     // the local calendar day with DST for all three (UX-12)
};

struct WotdDays {
    std::int64_t word_day = 0;   // picks the rack word
    std::int64_t scene_day = 0;  // menu gate, save DAY, player.WOD
    std::string date;            // high-score date, YYYY-MM-DD
};

// Floor division for a positive divisor (C++ `/` truncates toward zero).
inline constexpr std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

// Word of the Day day number for a count of seconds already shifted to
// the wanted clock (UTC, local standard or local with DST).
inline constexpr std::int64_t wotd_day_number(std::int64_t seconds) noexcept {
    return floor_div(seconds, SECONDS_PER_DAY) - WOTD_EPOCH_DAY;
}

// `YYYY-MM-DD` for a day count since 1970-01-01 (Howard Hinnant's
// civil-from-days algorithm, proleptic Gregorian; no locale involved).
inline std::string civil_date(std::int64_t unix_day) {
    const std::int64_t z = unix_day + 719'468;
    const std::int64_t era = floor_div(z, 146'097);
    const std::int64_t doe = z - era * 146'097;                                // [0, 146096]
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36'524 - doe / 146'096) / 365;  // [0, 399]
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);          // [0, 365]
    const std::int64_t mp = (5 * doy + 2) / 153;                               // [0, 11]
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;                     // [1, 31]
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;                      // [1, 12]
    const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);

    const auto two = [](std::int64_t v) {
        return std::string(v < 10 ? "0" : "") + std::to_string(v);
    };
    std::string y = std::to_string(year < 0 ? -year : year);
    if (y.size() < 4) y.insert(0, 4 - y.size(), '0');
    return (year < 0 ? "-" : "") + y + "-" + two(month) + "-" + two(day);
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days-from-civil, the inverse of civil_date). `month` 1..12, `day` 1..31.
inline constexpr std::int64_t days_from_civil(std::int64_t year, int month, int day) noexcept {
    const std::int64_t y = year - (month <= 2 ? 1 : 0);
    const std::int64_t era = floor_div(y, 400);
    const std::int64_t yoe = y - era * 400;                                        // [0, 399]
    const std::int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;   // [0, 365]
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                // [0, 146096]
    return era * 146'097 + doe - 719'468;
}

// Seconds since 1970-01-01 of a broken-down time, read as if it were UTC.
inline std::int64_t seconds_from_civil(const std::tm& t) noexcept {
    return days_from_civil(static_cast<std::int64_t>(t.tm_year) + 1900, t.tm_mon + 1, t.tm_mday) * SECONDS_PER_DAY +
           t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
}

// One clock reading from what the C runtime reports (D-3). The front end
// takes `unix_ms` from the system clock, `tz_seconds_west` from
// `_get_timezone` (Windows) or the POSIX / bionic `timezone` after
// `tzset()` (both standard time, seconds west of UTC, as `_ftime64_s`
// reports it in minutes), and `local` / `utc` from localtime_* and gmtime_*
// of the same instant. Their difference is the real offset with DST, with
// no `tm_gmtoff` (Windows has none). Both divisions truncate toward zero,
// like the CRT's `timezone / 60`.
inline WallClock wall_clock_from(std::int64_t unix_ms, long tz_seconds_west, const std::tm& local,
                                 const std::tm& utc) noexcept {
    WallClock c;
    c.unix_ms = unix_ms;
    c.tz_minutes_west = static_cast<std::int32_t>(tz_seconds_west / 60);
    c.utc_offset_minutes = static_cast<std::int32_t>((seconds_from_civil(local) - seconds_from_civil(utc)) / 60);
    return c;
}

// The three day values for one clock reading under one rule.
inline WotdDays wotd_days(WallClock now, WotdDayRule rule = WotdDayRule::Original) {
    const std::int64_t unix_s = floor_div(now.unix_ms, 1000);
    WotdDays d;
    if (rule == WotdDayRule::Local) {
        const std::int64_t local_s = unix_s + static_cast<std::int64_t>(now.utc_offset_minutes) * 60;
        d.word_day = wotd_day_number(local_s);
        d.scene_day = d.word_day;
        d.date = civil_date(floor_div(local_s, SECONDS_PER_DAY));
        return d;
    }
    d.word_day = wotd_day_number(unix_s);
    d.scene_day = wotd_day_number(unix_s - static_cast<std::int64_t>(now.tz_minutes_west) * 60);
    d.date = civil_date(floor_div(unix_s, SECONDS_PER_DAY));
    return d;
}

}  // namespace tt2
