// The reading part of BookOrbit's dashboard (GET plugin/catalog/dashboard) as a
// typed model, and its copy in the ledger's remote_cache.
//
// Spec: Stats ("Sources", "Screens"). Task 23 (docs/tasks/23-stats.md).
//
// Field names, types and the way the plugin reads them are checked against
// BookOrbit at commit c77966cc529423e993fae8a298aa77048b43bba2; each field's
// citation sits next to its parse in dashboard.cpp.
//
// Every field is optional (an older server leaves some out, a reader can turn
// achievements off): a section that is missing, null or the wrong type is left
// "not present" (its has* flag stays false) and the screen hides that widget.
// Nothing here throws or asserts on a bad body.
#pragma once

#include "smalt/status.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace smalt {

class Ledger;

// One entry of finishedThisYear, or the book of yearAgo
// (KoreaderCatalogBookListItem, packages/types/src/koreader.ts).
struct DashboardBook
{
    qint64 id = 0;
    QString title;
    QStringList authors;
    bool hasProgress = false;
    double progressPercentage = 0; // 0..100
    QString finishedOn; // yyyy-MM-dd, only on finishedThisYear entries; may be empty.
    bool hasCover = false;
};

// readingYear: minutes read per local day (the reader's time zone), oldest
// first and ending today.
struct DashboardReadingYear
{
    QDate startDate;
    QVector<int> minutes;
};

struct DashboardStreak
{
    bool hasCurrent = false;
    int current = 0;
    bool hasLongest = false;
    int longest = 0;
    // Seven entries, oldest first, the last one is today (server: computeStreakData).
    bool hasLastSeven = false;
    QVector<bool> lastSevenDays;
};

struct DashboardGoal
{
    bool hasGoalBooks = false; // goalBooks is null when no goal is set.
    int goalBooks = 0;
    bool hasCompleted = false;
    int completedBooks = 0;
    bool hasYear = false;
    int year = 0;
};

struct DashboardYearAgo
{
    DashboardBook book;
    bool hasMinutes = false;
    int minutes = 0;
    bool hasDays = false;
    int days = 0;
    bool hasEndProgress = false;
    double endProgress = 0; // 0..100: where you stopped.
    QDate firstDay; // Invalid if absent.
};

// achievements.next
struct DashboardBadge
{
    QString key;
    QString groupKey; // hours_read, pages_read, books_finished, ... or empty.
    QString name;
    QString description;
    bool hasThreshold = false;
    double threshold = 0;
    bool hasProgress = false;
    double currentProgress = 0;
};

struct DashboardHighlight
{
    QString text;
    QString note;
    QString bookTitle;
    qint64 bookId = 0;
};

struct Dashboard
{
    QDateTime generatedAtUtc; // Null if absent.

    bool hasReadingYear = false;
    DashboardReadingYear readingYear;

    bool hasReadingStreak = false; // At least one streak field present.
    DashboardStreak readingStreak;

    bool hasReadingGoal = false; // At least one goal field present.
    DashboardGoal readingGoal;

    bool hasFinishedThisYear = false; // The key is present and is a list (may be empty).
    QVector<DashboardBook> finishedThisYear;

    bool hasYearAgo = false;
    DashboardYearAgo yearAgo;

    bool hasNextBadge = false; // achievements.next, null when achievements are off.
    DashboardBadge nextBadge;

    bool hasHighlight = false;
    DashboardHighlight highlight;

    bool isEmpty() const
    {
        return !hasReadingYear && !hasReadingStreak && !hasReadingGoal && !hasFinishedThisYear
                && !hasYearAgo && !hasNextBadge && !hasHighlight;
    }
};

// Parses a dashboard body. Never fails: whatever is missing stays absent.
Dashboard parseDashboard(const QJsonObject &body);

// remote_cache entry (ledger.h): only the stats fields, so the cache stays small.
// Other modules refreshing the dashboard (sync pull) can call cacheDashboard().
constexpr const char *kDashboardCacheKey = "stats.dashboard";
constexpr const char *kDashboardCacheKind = "stats-dashboard";

// Keeps just the fields parseDashboard() reads.
QJsonObject trimDashboard(const QJsonObject &body);

Status cacheDashboard(Ledger *ledger, const QJsonObject &body, const QDateTime &nowUtc);

// Reads the cached copy. NotFound if none; an unreadable copy is NotFound too
// (the cache is disposable). `updatedUtc` is when the body was fetched.
Status loadCachedDashboard(Ledger *ledger, Dashboard *out, QDateTime *updatedUtc);

} // namespace smalt
