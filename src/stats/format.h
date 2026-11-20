// Text for the Stats screens: durations, dates and the forecast sentences.
// English only (the app has no translations yet); month and day names are
// fixed so output never depends on the device's locale.
#pragma once

#include "aggregate.h"

#include <QDate>
#include <QString>
#include <QVector>

namespace smalt {
namespace stats {

// "34 min" under an hour, otherwise "1 h 46 m" (minutes zero-padded: "4 h 05 m").
QString formatDuration(qint64 seconds);
// "65 min": minutes only, for lists of sessions.
QString formatMinutes(qint64 seconds);
// "212 h": whole hours.
QString formatHours(qint64 seconds);
// "22:05"
QString formatClock(int secondsIntoDay);
// "22:05–23:10"
QString formatRange(int startSec, int endSec);
// "3 Mar"
QString formatShortDate(const QDate &d);
// "Today", "Yesterday", otherwise "3 Mar".
QString formatDayLabel(const QDate &day, const QDate &today);
QString weekdayName(int dayOfWeek); // 1 = Monday, "Thursday"
QString weekdayInitial(int dayOfWeek); // "M", "T", "W", "T", "F", "S", "S"

// "Mostly 21:00–23:00", or empty when nothing was read.
QString hourSummary(const QVector<qint64> &secondsByHour);

// "About 3 h 20 m left" (rounded to 5 minutes), or "Not enough reading yet".
QString forecastHeadline(const Forecast &f);
// "Done around Thursday at your pace of 25 min a day"; empty unless the forecast is Ok.
QString forecastDetail(const Forecast &f, const QDate &today);

// Badge text: "38 h to go", "120 pages to go", "2 books to go"; empty if unknown.
QString badgeToGo(const DashboardBadge &b);
// 0..1 progress towards the badge, or -1 if unknown.
double badgeFraction(const DashboardBadge &b);

// "N of M books" and the pace line ("2 behind", "3 ahead", "on pace"); `dayOfYear`
// and `daysInYear` come from the caller's clock. Empty when there is no goal.
QString goalTitle(const DashboardGoal &g, int completedOverride = -1);
QString goalPace(const DashboardGoal &g, int completed, int dayOfYear, int daysInYear);
double goalFraction(const DashboardGoal &g, int completed);

} // namespace stats
} // namespace smalt
