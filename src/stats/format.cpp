#include "format.h"

#include <QChar>

#include <algorithm>
#include <cmath>

namespace smalt {
namespace stats {

namespace {

const char *const kMonths[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
const char *const kWeekdays[7] = { "Monday", "Tuesday",  "Wednesday", "Thursday",
                                   "Friday", "Saturday", "Sunday" };

qint64 wholeMinutes(qint64 seconds)
{
    return (std::max<qint64>(0, seconds) + 30) / 60;
}

QString twoDigits(qint64 n)
{
    return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0'));
}

} // namespace

QString formatDuration(qint64 seconds)
{
    const qint64 m = wholeMinutes(seconds);
    if (m < 60)
        return QStringLiteral("%1 min").arg(m);
    return QStringLiteral("%1 h %2 m").arg(m / 60).arg(twoDigits(m % 60));
}

QString formatMinutes(qint64 seconds)
{
    return QStringLiteral("%1 min").arg(wholeMinutes(seconds));
}

QString formatHours(qint64 seconds)
{
    return QStringLiteral("%1 h").arg((std::max<qint64>(0, seconds) + 1800) / 3600);
}

QString formatClock(int secondsIntoDay)
{
    const int minutes = std::max(0, secondsIntoDay) / 60;
    return QStringLiteral("%1:%2")
            .arg(minutes / 60, 2, 10, QLatin1Char('0'))
            .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString formatRange(int startSec, int endSec)
{
    return formatClock(startSec) + QChar(0x2013) + formatClock(endSec);
}

QString formatShortDate(const QDate &d)
{
    if (!d.isValid())
        return QString();
    return QStringLiteral("%1 %2").arg(d.day()).arg(QLatin1String(kMonths[d.month() - 1]));
}

QString formatDayLabel(const QDate &day, const QDate &today)
{
    if (day == today)
        return QStringLiteral("Today");
    if (day == today.addDays(-1))
        return QStringLiteral("Yesterday");
    return formatShortDate(day);
}

QString weekdayName(int dayOfWeek)
{
    if (dayOfWeek < 1 || dayOfWeek > 7)
        return QString();
    return QLatin1String(kWeekdays[dayOfWeek - 1]);
}

QString weekdayInitial(int dayOfWeek)
{
    const QString n = weekdayName(dayOfWeek);
    return n.isEmpty() ? n : n.left(1);
}

QString hourSummary(const QVector<qint64> &secondsByHour)
{
    const int h = busiestTwoHours(secondsByHour);
    if (h < 0)
        return QString();
    return QStringLiteral("Mostly %1:00%2%3:00")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(QChar(0x2013))
            .arg(h + 2, 2, 10, QLatin1Char('0'));
}

QString forecastHeadline(const Forecast &f)
{
    if (f.state != Forecast::Ok)
        return QStringLiteral("Not enough reading yet");
    // Round to 5 minutes, never to nothing.
    const qint64 minutes = std::max<qint64>(5, qint64(std::floor(f.minutesLeft / 5.0 + 0.5)) * 5);
    return QStringLiteral("About %1 left").arg(formatDuration(minutes * 60));
}

QString forecastDetail(const Forecast &f, const QDate &today)
{
    if (f.state != Forecast::Ok)
        return QString();
    QString when;
    if (f.daysOut == 0)
        when = QStringLiteral("today");
    else if (f.daysOut == 1)
        when = QStringLiteral("tomorrow");
    else if (f.daysOut <= 6)
        when = weekdayName(f.finishDate.dayOfWeek());
    else
        when = formatShortDate(f.finishDate);
    Q_UNUSED(today);
    return QStringLiteral("Done around %1 at your pace of %2 min a day")
            .arg(when)
            .arg(f.perDayMinutes);
}

double badgeFraction(const DashboardBadge &b)
{
    if (!b.hasThreshold || !b.hasProgress || b.threshold <= 0)
        return -1;
    return std::max(0.0, std::min(1.0, b.currentProgress / b.threshold));
}

QString badgeToGo(const DashboardBadge &b)
{
    if (!b.hasThreshold || !b.hasProgress)
        return QString();
    const double left = b.threshold - b.currentProgress;
    if (left <= 0)
        return QString();
    const qint64 n = qint64(std::ceil(left));
    if (b.groupKey == QLatin1String("hours_read"))
        return QStringLiteral("%1 h to go").arg(n);
    if (b.groupKey == QLatin1String("pages_read"))
        return QStringLiteral("%1 pages to go").arg(n);
    if (b.groupKey == QLatin1String("books_finished"))
        return QStringLiteral("%1 %2 to go")
                .arg(n)
                .arg(n == 1 ? QStringLiteral("book") : QStringLiteral("books"));
    return QStringLiteral("%1 to go").arg(n);
}

QString goalTitle(const DashboardGoal &g, int completedOverride)
{
    if (!g.hasGoalBooks || g.goalBooks <= 0)
        return QString();
    const int done = completedOverride >= 0 ? completedOverride : g.completedBooks;
    return QStringLiteral("%1 of %2 books").arg(done).arg(g.goalBooks);
}

QString goalPace(const DashboardGoal &g, int completed, int dayOfYear, int daysInYear)
{
    if (!g.hasGoalBooks || g.goalBooks <= 0 || daysInYear <= 0)
        return QString();
    const double expected = double(g.goalBooks) * double(dayOfYear) / double(daysInYear);
    const int diff = int(std::floor(double(completed) - expected + 0.5));
    QString pace;
    if (diff == 0)
        pace = QStringLiteral("on pace");
    else if (diff > 0)
        pace = QStringLiteral("%1 ahead").arg(diff);
    else
        pace = QStringLiteral("%1 behind").arg(-diff);
    if (g.hasYear)
        return QStringLiteral("%1 goal %2 %3").arg(g.year).arg(QChar(0x00B7)).arg(pace);
    return QStringLiteral("Goal %1 %2").arg(QChar(0x00B7)).arg(pace);
}

double goalFraction(const DashboardGoal &g, int completed)
{
    if (!g.hasGoalBooks || g.goalBooks <= 0)
        return -1;
    return std::max(0.0, std::min(1.0, double(completed) / double(g.goalBooks)));
}

} // namespace stats
} // namespace smalt
