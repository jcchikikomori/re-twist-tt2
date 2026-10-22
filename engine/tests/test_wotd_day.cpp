// Tests for the Word of the Day clocks (wotd_day.hpp): the word day, the
// scene day and the date text under both rules
// (docs/reverse-engineering/game-scene.md §8.1; design D-4, D-12, D-15),
// and the front end's clock reading (wall_clock_from, D-3).

#include <ctime>
#include <string>

#include "test_harness.hpp"
#include "tt2/wotd_day.hpp"

using tt2::WallClock;
using tt2::WotdDayRule;
using tt2::wotd_days;

namespace {
constexpr std::int64_t MS = 1000;

// Manila (UTC+8, no DST): `timezone` = -28800 s, so -480 minutes west.
WallClock manila(std::int64_t unix_s) { return WallClock{unix_s * MS, -480, 480}; }
// Berlin in July (CEST): standard offset -60 minutes west, real offset +120.
WallClock berlin_summer(std::int64_t unix_s) { return WallClock{unix_s * MS, -60, 120}; }
// New York in January (EST, UTC-5): 300 minutes west, real offset -300.
WallClock new_york_winter(std::int64_t unix_s) { return WallClock{unix_s * MS, 300, -300}; }
}  // namespace

TEST(wotd_days_manila_morning_original_rule) {
    // 2026-10-23 07:30 Manila = 2026-10-22 23:30 UTC (unix 1792711800).
    const auto d = wotd_days(manila(1792711800), WotdDayRule::Original);
    ASSERT_EQ(d.word_day, std::int64_t(6623));    // UTC day 20748 - 14125
    ASSERT_EQ(d.scene_day, std::int64_t(6624));   // local standard day already 10-23
    ASSERT_EQ(d.date, std::string("2026-10-22")); // UTC date
}

TEST(wotd_days_manila_morning_local_rule) {
    const auto d = wotd_days(manila(1792711800), WotdDayRule::Local);
    ASSERT_EQ(d.word_day, std::int64_t(6624));
    ASSERT_EQ(d.scene_day, std::int64_t(6624));
    ASSERT_EQ(d.date, std::string("2026-10-23"));
}

TEST(wotd_days_berlin_summer_ignores_dst_in_original_rule) {
    // 2026-07-15 00:30 CEST = 2026-07-14 22:30 UTC. The exe never reads
    // `dstflag`: 00:30 CEST is 23:30 standard time, still 07-14 (day 6523).
    const auto orig = wotd_days(berlin_summer(1784068200), WotdDayRule::Original);
    ASSERT_EQ(orig.word_day, std::int64_t(6523));
    ASSERT_EQ(orig.scene_day, std::int64_t(6523));
    ASSERT_EQ(orig.date, std::string("2026-07-14"));
    // Local day uses the real offset, so it is already 07-15.
    const auto local = wotd_days(berlin_summer(1784068200), WotdDayRule::Local);
    ASSERT_EQ(local.word_day, std::int64_t(6524));
    ASSERT_EQ(local.scene_day, std::int64_t(6524));
    ASSERT_EQ(local.date, std::string("2026-07-15"));
    // Original flips at 01:00 CEST (23:00 UTC).
    const auto one_am = wotd_days(berlin_summer(1784070000), WotdDayRule::Original);
    ASSERT_EQ(one_am.scene_day, std::int64_t(6524));
}

TEST(wotd_days_new_york_late_evening) {
    // 2026-01-15 23:30 EST = 2026-01-16 04:30 UTC.
    const auto orig = wotd_days(new_york_winter(1768537800), WotdDayRule::Original);
    ASSERT_EQ(orig.word_day, std::int64_t(6344));   // UTC already 01-16
    ASSERT_EQ(orig.scene_day, std::int64_t(6343));  // local still 01-15
    ASSERT_EQ(orig.date, std::string("2026-01-16"));
    const auto local = wotd_days(new_york_winter(1768537800), WotdDayRule::Local);
    ASSERT_EQ(local.word_day, std::int64_t(6343));
    ASSERT_EQ(local.scene_day, std::int64_t(6343));
    ASSERT_EQ(local.date, std::string("2026-01-15"));
}

TEST(wotd_days_day_zero_is_2008_09_03) {
    // 2008-09-03 00:00 UTC = unix 1220400000 = unix day 14125.
    const auto d = wotd_days(WallClock{1220400000LL * MS, 0, 0}, WotdDayRule::Original);
    ASSERT_EQ(d.word_day, std::int64_t(0));
    ASSERT_EQ(d.scene_day, std::int64_t(0));
    ASSERT_EQ(d.date, std::string("2008-09-03"));
    // One millisecond earlier is day -1, 2008-09-02.
    const auto before = wotd_days(WallClock{1220400000LL * MS - 1, 0, 0}, WotdDayRule::Original);
    ASSERT_EQ(before.word_day, std::int64_t(-1));
    ASSERT_EQ(before.date, std::string("2008-09-02"));
}

TEST(wotd_days_floor_division_before_1970) {
    // Unix 0 is 1970-01-01, day -14125.
    const auto epoch = wotd_days(WallClock{0, 0, 0}, WotdDayRule::Original);
    ASSERT_EQ(epoch.word_day, std::int64_t(-14125));
    ASSERT_EQ(epoch.date, std::string("1970-01-01"));
    // -1 ms floors to the previous day; truncation would have said day -14125.
    const auto before = wotd_days(WallClock{-1, 0, 0}, WotdDayRule::Original);
    ASSERT_EQ(before.word_day, std::int64_t(-14126));
    ASSERT_EQ(before.scene_day, std::int64_t(-14126));
    ASSERT_EQ(before.date, std::string("1969-12-31"));
}

TEST(wotd_days_original_scene_day_ignores_utc_offset) {
    // Negative: the real offset only drives the Local rule.
    const auto a = wotd_days(WallClock{1792711800LL * MS, -480, 0}, WotdDayRule::Original);
    const auto b = wotd_days(WallClock{1792711800LL * MS, -480, 600}, WotdDayRule::Original);
    ASSERT_EQ(a.word_day, b.word_day);
    ASSERT_EQ(a.scene_day, b.scene_day);
    ASSERT_EQ(a.date, b.date);
    // ... and the standard offset never moves the Local rule.
    const auto c = wotd_days(WallClock{1792711800LL * MS, 0, 480}, WotdDayRule::Local);
    const auto d = wotd_days(WallClock{1792711800LL * MS, 600, 480}, WotdDayRule::Local);
    ASSERT_EQ(c.scene_day, d.scene_day);
    ASSERT_EQ(c.date, d.date);
}

TEST(civil_date_handles_leap_days_and_centuries) {
    ASSERT_EQ(tt2::civil_date(0), std::string("1970-01-01"));
    ASSERT_EQ(tt2::civil_date(-1), std::string("1969-12-31"));
    ASSERT_EQ(tt2::civil_date(11016), std::string("2000-02-29"));
    ASSERT_EQ(tt2::civil_date(19782), std::string("2024-02-29"));
    ASSERT_EQ(tt2::civil_date(20748), std::string("2026-10-22"));
    // 2100 is not a leap year: the day after 02-28 is 03-01.
    ASSERT_EQ(tt2::civil_date(47540), std::string("2100-02-28"));
    ASSERT_EQ(tt2::civil_date(47541), std::string("2100-03-01"));
}

TEST(civil_date_pads_short_years_and_signs_negative_years) {
    // Years under 1000 keep four digits.
    ASSERT_EQ(tt2::civil_date(-354285), std::string("1000-01-01"));
    ASSERT_EQ(tt2::civil_date(-354286), std::string("0999-12-31"));
    ASSERT_EQ(tt2::civil_date(-719162), std::string("0001-01-01"));
    // Year 0 is a leap year (proleptic Gregorian); the year before it is -1.
    ASSERT_EQ(tt2::civil_date(-719468), std::string("0000-03-01"));
    ASSERT_EQ(tt2::civil_date(-719469), std::string("0000-02-29"));
    ASSERT_EQ(tt2::civil_date(-719834), std::string("-0001-03-01"));
}

TEST(wotd_day_number_floors_local_seconds) {
    ASSERT_EQ(tt2::wotd_day_number(14125LL * 86400), std::int64_t(0));
    ASSERT_EQ(tt2::wotd_day_number(14125LL * 86400 + 86399), std::int64_t(0));
    ASSERT_EQ(tt2::wotd_day_number(14125LL * 86400 - 1), std::int64_t(-1));
    ASSERT_EQ(tt2::wotd_day_number(-1), std::int64_t(-14126));
}

// ---- days_from_civil / wall_clock_from: the front end's clock reading ----

TEST(days_from_civil_inverts_civil_date) {
    ASSERT_EQ(tt2::days_from_civil(1970, 1, 1), std::int64_t(0));
    ASSERT_EQ(tt2::days_from_civil(1969, 12, 31), std::int64_t(-1));
    ASSERT_EQ(tt2::days_from_civil(2000, 2, 29), std::int64_t(11016));
    ASSERT_EQ(tt2::days_from_civil(2008, 9, 3), tt2::WOTD_EPOCH_DAY);
    ASSERT_EQ(tt2::days_from_civil(2026, 10, 22), std::int64_t(20748));
    ASSERT_EQ(tt2::days_from_civil(2027, 1, 1), std::int64_t(20819));
    // 2100 is not a leap year.
    ASSERT_EQ(tt2::days_from_civil(2100, 3, 1), std::int64_t(47541));
    ASSERT_EQ(tt2::civil_date(tt2::days_from_civil(2024, 2, 29)), std::string("2024-02-29"));
    ASSERT_EQ(tt2::civil_date(tt2::days_from_civil(1, 1, 1)), std::string("0001-01-01"));
}

namespace {
// A broken-down time as localtime_r / gmtime_r fill it (tm_year from 1900,
// tm_mon from 0).
std::tm broken_down(int year, int month, int day, int hour, int minute, int second = 0) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return t;
}
}  // namespace

// Design worked example: Manila, `timezone` = -28800 s -> -480 minutes west,
// and the real offset +480.
TEST(wall_clock_from_manila_turns_seconds_into_minutes) {
    const WallClock c = tt2::wall_clock_from(1792711800LL * MS, -28800, broken_down(2026, 10, 23, 7, 30),
                                             broken_down(2026, 10, 22, 23, 30));
    ASSERT_EQ(c.unix_ms, 1792711800LL * MS);
    ASSERT_EQ(c.tz_minutes_west, -480);
    ASSERT_EQ(c.utc_offset_minutes, 480);
    const auto d = wotd_days(c);
    ASSERT_EQ(d.word_day, std::int64_t(6623));
    ASSERT_EQ(d.scene_day, std::int64_t(6624));
}

// Berlin in July: the standard offset stays CET (-60 west) while the real
// offset is CEST (+120), so the Original scene day is still yesterday.
TEST(wall_clock_from_berlin_summer_keeps_standard_and_dst_offsets_apart) {
    const WallClock c = tt2::wall_clock_from(1784068200LL * MS, -3600, broken_down(2026, 7, 15, 0, 30),
                                             broken_down(2026, 7, 14, 22, 30));
    ASSERT_EQ(c.tz_minutes_west, -60);
    ASSERT_EQ(c.utc_offset_minutes, 120);
    ASSERT_EQ(wotd_days(c, WotdDayRule::Original).scene_day, std::int64_t(6523));
    ASSERT_EQ(wotd_days(c, WotdDayRule::Local).scene_day, std::int64_t(6524));
}

// West of UTC across a year boundary: tm_yday would wrap, day numbers don't.
TEST(wall_clock_from_new_york_new_year_is_negative) {
    const WallClock c = tt2::wall_clock_from(1798774200LL * MS, 18000, broken_down(2026, 12, 31, 22, 30),
                                             broken_down(2027, 1, 1, 3, 30));
    ASSERT_EQ(c.tz_minutes_west, 300);
    ASSERT_EQ(c.utc_offset_minutes, -300);
    ASSERT_EQ(wotd_days(c, WotdDayRule::Local).date, std::string("2026-12-31"));
}

// Half-hour zones and the milliseconds pass through untouched.
TEST(wall_clock_from_india_half_hour_offset_keeps_milliseconds) {
    const WallClock c = tt2::wall_clock_from(1792699200123LL, -19800, broken_down(2026, 10, 23, 1, 30),
                                             broken_down(2026, 10, 22, 20, 0));
    ASSERT_EQ(c.unix_ms, 1792699200123LL);
    ASSERT_EQ(c.tz_minutes_west, -330);
    ASSERT_EQ(c.utc_offset_minutes, 330);
}

// UTC itself: both offsets are zero.
TEST(wall_clock_from_utc_has_no_offsets) {
    const WallClock c = tt2::wall_clock_from(1792711800LL * MS, 0, broken_down(2026, 10, 22, 23, 30),
                                             broken_down(2026, 10, 22, 23, 30));
    ASSERT_EQ(c.tz_minutes_west, 0);
    ASSERT_EQ(c.utc_offset_minutes, 0);
}

// Negative: an offset that is not whole minutes (Liberia before 1972,
// -0:44:30) truncates toward zero like the CRT's `timezone / 60` that
// fills `_ftime64_s`, not down to -45.
TEST(wall_clock_from_truncates_partial_minutes_toward_zero) {
    const WallClock c = tt2::wall_clock_from(0, -2670, broken_down(1970, 1, 1, 0, 44, 30),
                                             broken_down(1970, 1, 1, 0, 0));
    ASSERT_EQ(c.tz_minutes_west, -44);
    ASSERT_EQ(c.utc_offset_minutes, 44);
}
