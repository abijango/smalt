// Stats aggregation: pure functions over ledger data. No I/O, no clock reads,
// no Qt GUI; every input (including the time zone) is a parameter so the
// results are reproducible in tests.
//
// Spec: Stats ("Sources", "Screens", "Rules"), On-device data model ("How
// BookOrbit builds sessions"). Task 23.
//
// Time: all instants are Unix seconds (UTC). "Day" means a local calendar day
// in the time zone given by a ZoneOffsetFn, which returns the UTC offset in
// force at an instant (so DST and a changed zone are handled per instant).
#pragma once

#include "bookorbit/dashboard.h"
#include "smalt/ledger.h"

#include <QDate>
#include <QMap>
#include <QString>
#include <QVector>

#include <functional>

namespace smalt {
namespace stats {

// --- Time zone ---

// UTC offset in seconds at a given instant.
using ZoneOffsetFn = std::function<int(qint64 utcSecs)>;

ZoneOffsetFn systemZone(); // The device's current local zone (Qt, honours DST).
ZoneOffsetFn fixedZone(int offsetSeconds);

QDate localDate(qint64 utcSecs, const ZoneOffsetFn &zone);
int secondsIntoLocalDay(qint64 utcSecs, const ZoneOffsetFn &zone);

// BookOrbit clamps a page's duration to 0..86400 s on upload (page_stats.h);
// Stats counts the same, so the numbers match the server's.
constexpr int kMaxPageSeconds = 86400;

// --- Minutes per day ---

// Seconds read per local day, each page event split at local midnight.
QMap<QDate, qint64> secondsPerDay(const QVector<PageEventRecord> &events, const ZoneOffsetFn &zone);

// --- Today's blocks (24-hour strip) ---

struct TimeBlock
{
    int startSec = 0; // Seconds since local midnight, 0..86400
    int endSec = 0;
};

// Contiguous reading on `day`, from page events. Time between events (sleep,
// a closed book) is never inside a block: events whose gap is at most
// `mergeGapSeconds` (whole-second rounding, a quick close and reopen) merge.
constexpr int kBlockMergeGapSeconds = 60;
QVector<TimeBlock> blocksForDay(const QVector<PageEventRecord> &events, const QDate &day,
                                const ZoneOffsetFn &zone,
                                int mergeGapSeconds = kBlockMergeGapSeconds);

// --- Patterns ---

// Seconds read in each local hour of the day, for days in [from, to] inclusive.
QVector<qint64> secondsByHour(const QVector<PageEventRecord> &events, const ZoneOffsetFn &zone,
                              const QDate &from, const QDate &to);

// Average minutes read per day of the week over [from, to] inclusive; index 0
// is Monday. Days without reading count as zero (a weekday you never read on
// averages 0).
QVector<double> averageMinutesByWeekday(const QMap<QDate, qint64> &perDay, const QDate &from,
                                        const QDate &to);

// Hours of the day with the most reading: the two consecutive hours with the
// largest total, as the first hour (0..22). -1 if nothing was read.
int busiestTwoHours(const QVector<qint64> &secondsByHour);

struct Records
{
    bool hasLongestSession = false;
    qint64 longestSessionSeconds = 0;
    QDate longestSessionDate;
    bool hasBestDay = false;
    qint64 bestDaySeconds = 0;
    QDate bestDayDate;
    int sessionCount = 0;
    qint64 averageSessionSeconds = 0; // 0 if there are no sessions.
};

// Dropped sessions never count. The best day comes from `perDay`.
Records computeRecords(const QVector<SessionRecord> &sessions, const QMap<QDate, qint64> &perDay,
                       const ZoneOffsetFn &zone);

// Pages per hour over page events starting in [fromSecs, toSecs): distinct
// (session, page) pairs read divided by the hours spent on them, so turning
// back to a page does not raise the speed. Needs at least a minute of reading.
struct Pace
{
    bool valid = false;
    double pagesPerHour = 0;
    int pages = 0;
    qint64 seconds = 0;
};

Pace pagesPerHour(const QVector<PageEventRecord> &events, qint64 fromSecs, qint64 toSecs);

// --- A book ---

struct BookStats
{
    qint64 totalSeconds = 0; // Sum of sessions' active seconds (Dropped excluded).
    int sessionCount = 0;
    qint64 averageSessionSeconds = 0;
    Pace pace; // Over all of the book's page events.
    QMap<QDate, qint64> perDay; // Days the book was read.
    QDate firstDay; // Invalid if never read.
    int totalPages = 0; // From the newest page event.
};

BookStats computeBookStats(const QVector<SessionRecord> &sessions,
                           const QVector<PageEventRecord> &events, const ZoneOffsetFn &zone);

// --- Finish forecast (Stats "Rules") ---
//
// The median minutes per reading day over the last 14 days and the pages per
// hour over the last 30 days; fewer than three sessions says "Not enough
// reading yet".
constexpr int kForecastMinSessions = 3;
constexpr int kForecastDays = 14;
constexpr int kPaceDays = 30;

struct ForecastInput
{
    double percent = 0; // 0..100, where you are in the book.
    int totalPages = 0;
    int sessionCount = 0; // This book's sessions.
    Pace bookPace; // Pages per hour on this book, last 30 days.
    Pace devicePace; // Pages per hour on every book, last 30 days (fallback).
    // Seconds read on each of the last 14 days, today last (zeros included).
    QVector<qint64> recentDaySeconds;
    QDate today;
};

struct Forecast
{
    enum State { Ok, NotEnoughReading, Finished };

    State state = NotEnoughReading;
    double minutesLeft = 0;
    int perDayMinutes = 0; // The median, as shown ("25 min a day").
    int daysOut = 0; // 0 = today.
    QDate finishDate;
};

Forecast forecast(const ForecastInput &input);

// Seconds read on each of the 14 days ending `today` (oldest first), from
// secondsPerDay().
QVector<qint64> recentDays(const QMap<QDate, qint64> &perDay, const QDate &today, int days);

// --- Merging this Kobo's unsent minutes into the server's heatmap ---
//
// A page event is added on top of the server's day when the server cannot have
// it yet: it is not confirmed (above its book's watermark), or it is confirmed
// but starts after the dashboard was fetched (pushed while reading since). Once
// the dashboard is refreshed after the push, its fetch time passes the event and
// the addition drops away on its own, so nothing is counted twice. The open
// session's current page (not yet a page event) is added too. With no dashboard
// (fetchedSecs < 0) every event counts: this Kobo is all there is.
QMap<QDate, qint64> unsyncedSecondsPerDay(const QVector<PageEventRecord> &events,
                                          const QVector<bool> &confirmed, qint64 fetchedSecs,
                                          const SessionRecord *openSession,
                                          const ZoneOffsetFn &zone);

// Server minutes plus local seconds per day, whole minutes (server days are
// already rounded). `server` may be null.
QMap<QDate, int> mergeMinutes(const DashboardReadingYear *server,
                              const QMap<QDate, qint64> &unsyncedSeconds);

// BookOrbit's streak is the account's; a device whose own reading has not
// synced yet still counts today (PLUGIN bookorbit_board_data.lua 725).
int adjustedStreak(int serverCurrent, int todayMinutes);

// --- Heatmap ---

// Fixed minute bands (Stats "Rules"): 0, under 15, under 30, under 60, 60 or more.
int heatLevel(int minutes);

struct HeatCell
{
    QDate date;
    int minutes = 0;
    int level = 0;
    bool future = false; // After today: left blank.
};

struct HeatmapGrid
{
    int weeks = 0;
    // Column-major: cell(week, row) is cells[week * 7 + row]; row 0 is Monday,
    // the last column is the week that holds today.
    QVector<HeatCell> cells;
    QDate firstDate;

    const HeatCell &cell(int week, int row) const { return cells.at(week * 7 + row); }
};

HeatmapGrid buildHeatmap(const QMap<QDate, int> &minutesByDate, const QDate &today, int weeks);

} // namespace stats
} // namespace smalt
