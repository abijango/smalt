// What the Stats screens show, computed in one place.
//
// Two steps so the logic stays testable: loadInputs() reads the ledger and the
// cached dashboard (the only I/O), buildSnapshot() turns the inputs into the
// numbers and flags the screens draw (pure). A widget is hidden when its flag
// is false: missing dashboard fields hide their widgets (Stats "Sources").
#pragma once

#include "aggregate.h"
#include "format.h"
#include "smalt/device.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace smalt {
namespace stats {

// How much history the Patterns and records read from the ledger. A year of
// page events is a few tens of thousands of rows; sessions are read in full.
constexpr int kHistoryDays = 366;
constexpr int kPatternDays = 90; // Time of day and day of week
constexpr int kLatestSessions = 5; // "Your reading": latest sessions listed

// --- Overview, Patterns and Year ---

struct StatsInputs
{
    QDateTime nowUtc;
    ZoneOffsetFn zone;
    int heatmapWeeks = 26;

    bool hasDashboard = false;
    Dashboard dashboard;
    QDateTime dashboardFetchedUtc; // When the dashboard was fetched (cache time).

    QVector<PageEventRecord> events; // Last kHistoryDays, oldest first.
    QVector<bool> confirmed; // One per event (ledger.h pageEventsInRange).
    QVector<SessionRecord> sessions; // All of them (Dropped excluded by the ledger).
    bool hasOpenSession = false;
    SessionRecord openSession;
};

struct StatsSnapshot
{
    QDate today;
    int heatmapWeeks = 26;

    // Overview: all devices
    bool showHeatmap = false; // Needs the server's readingYear.
    HeatmapGrid heatmap;
    bool showTiles = true; // Today and this week; this Kobo only when there is no server year.
    bool tilesFromThisKoboOnly = false;
    int todayMinutes = 0;
    int weekMinutes = 0;

    bool showStreak = false;
    bool hasStreakCurrent = false;
    int streakCurrent = 0;
    bool hasStreakLongest = false;
    int streakLongest = 0;
    QVector<bool> streakDots; // Seven, oldest first, today last.
    QVector<QString> streakInitials;

    bool showGoal = false;
    QString goalTitle; // "18 of 30 books"
    QString goalPace; // "2026 goal · 2 behind"
    double goalFraction = 0;

    // Overview: this Kobo
    QVector<TimeBlock> todayBlocks;

    // Patterns: this Kobo
    bool hasReading = false; // Any reading recorded on this Kobo.
    QVector<qint64> secondsByHour; // 24
    QString hourSummary;
    QVector<double> weekdayAverages; // Monday first, minutes
    Records records;
    Pace pace30; // Pages per hour, last 30 days

    // Year: all devices
    bool showFinished = false;
    int finishedYear = 0;
    int finishedCount = 0;
    bool hasYearSeconds = false;
    qint64 yearSeconds = 0;
    QVector<DashboardBook> finishedBooks;

    bool showYearAgo = false;
    DashboardYearAgo yearAgo;

    bool showBadge = false;
    DashboardBadge badge;

    bool showHighlight = false;
    DashboardHighlight highlight;
};

StatsSnapshot buildSnapshot(const StatsInputs &in);

// Reads the ledger (and its cached dashboard). Ledger errors leave the part empty.
StatsInputs loadInputs(Ledger *ledger, const DeviceProfile &profile, const QDateTime &nowUtc,
                       const ZoneOffsetFn &zone);

// --- Book detail, "Your reading" ---

struct SessionRow
{
    QString day; // "Today", "Yesterday", "3 Mar"
    QString range; // "22:05–23:10"
    QString duration; // "65 min"
};

struct BookInputs
{
    QDateTime nowUtc;
    ZoneOffsetFn zone;
    QVector<SessionRecord> sessions; // All of the book's sessions (Dropped are skipped here).
    QVector<SessionRecord> latest; // Newest first, at most kLatestSessions.
    QVector<PageEventRecord> bookEvents; // All of the book's page events.
    QVector<PageEventRecord> deviceEvents; // Every book, last kPaceDays + a day.
    bool hasPercent = false;
    double percent = 0; // 0..100
};

struct BookSnapshot
{
    QDate today;
    bool hasReading = false;
    BookStats stats;
    Forecast forecast;
    QMap<QDate, int> minutesPerDay; // Days the book was read.
    QVector<SessionRow> latest;
};

BookSnapshot buildBookSnapshot(const BookInputs &in);

BookInputs loadBookInputs(Ledger *ledger, const QString &contentId, const QDateTime &nowUtc,
                          const ZoneOffsetFn &zone);

} // namespace stats
} // namespace smalt
