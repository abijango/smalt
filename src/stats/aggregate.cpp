#include "aggregate.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace smalt {
namespace stats {

namespace {

qint64 floorDiv(qint64 a, qint64 b)
{
    qint64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

QDate dateFromDayIndex(qint64 index)
{
    return QDate(1970, 1, 1).addDays(int(index));
}

qint64 dayIndexOf(const QDate &d)
{
    return QDate(1970, 1, 1).daysTo(d);
}

qint64 secsOf(const QDateTime &t)
{
    return t.isValid() ? t.toMSecsSinceEpoch() / 1000 : 0;
}

int clampedDuration(const PageEventRecord &e)
{
    return std::max(0, std::min(e.durationSeconds, kMaxPageSeconds));
}

// Calls f(unitIndex, positionInUnit, pieceSeconds) for each piece of
// [start, start + seconds) that falls in one local unit (a day or an hour).
// The offset is read at the start of each piece, so a zone change or DST step
// inside the span moves later pieces.
template <typename F>
void splitAtLocalUnits(qint64 start, qint64 seconds, qint64 unit, const ZoneOffsetFn &zone, F f)
{
    qint64 cur = start;
    const qint64 end = start + seconds;
    while (cur < end) {
        const int offset = zone(cur);
        const qint64 local = cur + offset;
        const qint64 index = floorDiv(local, unit);
        const qint64 boundaryUtc = (index + 1) * unit - offset;
        const qint64 stop = std::min(end, std::max(boundaryUtc, cur + 1));
        f(index, local - index * unit, stop - cur);
        cur = stop;
    }
}

} // namespace

ZoneOffsetFn systemZone()
{
    return [](qint64 t) {
        // Read the local wall clock for the instant and take its difference to UTC.
        QDateTime local = QDateTime::fromMSecsSinceEpoch(t * 1000);
        local.setTimeSpec(Qt::UTC);
        return int(local.toMSecsSinceEpoch() / 1000 - t);
    };
}

ZoneOffsetFn fixedZone(int offsetSeconds)
{
    return [offsetSeconds](qint64) { return offsetSeconds; };
}

QDate localDate(qint64 utcSecs, const ZoneOffsetFn &zone)
{
    return dateFromDayIndex(floorDiv(utcSecs + zone(utcSecs), 86400));
}

int secondsIntoLocalDay(qint64 utcSecs, const ZoneOffsetFn &zone)
{
    const qint64 local = utcSecs + zone(utcSecs);
    return int(local - floorDiv(local, 86400) * 86400);
}

QMap<QDate, qint64> secondsPerDay(const QVector<PageEventRecord> &events, const ZoneOffsetFn &zone)
{
    QMap<QDate, qint64> out;
    for (const PageEventRecord &e : events) {
        const int dur = clampedDuration(e);
        if (dur <= 0 || !e.startUtc.isValid())
            continue;
        splitAtLocalUnits(
                secsOf(e.startUtc), dur, 86400, zone,
                [&](qint64 day, qint64, qint64 piece) { out[dateFromDayIndex(day)] += piece; });
    }
    return out;
}

QVector<TimeBlock> blocksForDay(const QVector<PageEventRecord> &events, const QDate &day,
                                const ZoneOffsetFn &zone, int mergeGapSeconds)
{
    const qint64 target = dayIndexOf(day);
    QVector<TimeBlock> pieces;
    for (const PageEventRecord &e : events) {
        const int dur = clampedDuration(e);
        if (dur <= 0 || !e.startUtc.isValid())
            continue;
        splitAtLocalUnits(secsOf(e.startUtc), dur, 86400, zone,
                          [&](qint64 d, qint64 pos, qint64 piece) {
                              if (d != target)
                                  return;
                              TimeBlock b;
                              b.startSec = int(pos);
                              b.endSec = int(pos + piece);
                              pieces.append(b);
                          });
    }
    std::sort(pieces.begin(), pieces.end(), [](const TimeBlock &a, const TimeBlock &b) {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.endSec < b.endSec;
    });
    QVector<TimeBlock> merged;
    for (const TimeBlock &p : pieces) {
        if (!merged.isEmpty() && p.startSec - merged.last().endSec <= mergeGapSeconds)
            merged.last().endSec = std::max(merged.last().endSec, p.endSec);
        else
            merged.append(p);
    }
    return merged;
}

QVector<qint64> secondsByHour(const QVector<PageEventRecord> &events, const ZoneOffsetFn &zone,
                              const QDate &from, const QDate &to)
{
    QVector<qint64> hours(24, 0);
    const qint64 fromDay = dayIndexOf(from);
    const qint64 toDay = dayIndexOf(to);
    for (const PageEventRecord &e : events) {
        const int dur = clampedDuration(e);
        if (dur <= 0 || !e.startUtc.isValid())
            continue;
        splitAtLocalUnits(secsOf(e.startUtc), dur, 3600, zone,
                          [&](qint64 hourIndex, qint64, qint64 piece) {
                              const qint64 day = floorDiv(hourIndex, 24);
                              if (day < fromDay || day > toDay)
                                  return;
                              hours[int(hourIndex - day * 24)] += piece;
                          });
    }
    return hours;
}

QVector<double> averageMinutesByWeekday(const QMap<QDate, qint64> &perDay, const QDate &from,
                                        const QDate &to)
{
    QVector<double> sums(7, 0.0);
    QVector<int> counts(7, 0);
    for (QDate d = from; d <= to; d = d.addDays(1)) {
        const int dow = d.dayOfWeek() - 1;
        sums[dow] += double(perDay.value(d, 0)) / 60.0;
        counts[dow] += 1;
    }
    QVector<double> out(7, 0.0);
    for (int i = 0; i < 7; ++i)
        out[i] = counts[i] > 0 ? sums[i] / counts[i] : 0.0;
    return out;
}

int busiestTwoHours(const QVector<qint64> &hours)
{
    int best = -1;
    qint64 bestSum = 0;
    for (int h = 0; h + 1 < hours.size(); ++h) {
        const qint64 sum = hours.at(h) + hours.at(h + 1);
        if (sum > bestSum) {
            bestSum = sum;
            best = h;
        }
    }
    return best;
}

Records computeRecords(const QVector<SessionRecord> &sessions, const QMap<QDate, qint64> &perDay,
                       const ZoneOffsetFn &zone)
{
    Records r;
    qint64 total = 0;
    for (const SessionRecord &s : sessions) {
        if (s.state == SessionState::Dropped || s.activeSeconds <= 0)
            continue;
        ++r.sessionCount;
        total += s.activeSeconds;
        if (!r.hasLongestSession || s.activeSeconds > r.longestSessionSeconds) {
            r.hasLongestSession = true;
            r.longestSessionSeconds = s.activeSeconds;
            r.longestSessionDate = localDate(secsOf(s.startUtc), zone);
        }
    }
    if (r.sessionCount > 0)
        r.averageSessionSeconds = (total + r.sessionCount / 2) / r.sessionCount;
    for (QMap<QDate, qint64>::const_iterator it = perDay.constBegin(); it != perDay.constEnd();
         ++it) {
        if (it.value() > 0 && (!r.hasBestDay || it.value() > r.bestDaySeconds)) {
            r.hasBestDay = true;
            r.bestDaySeconds = it.value();
            r.bestDayDate = it.key();
        }
    }
    return r;
}

Pace pagesPerHour(const QVector<PageEventRecord> &events, qint64 fromSecs, qint64 toSecs)
{
    Pace p;
    QSet<QString> pages;
    for (const PageEventRecord &e : events) {
        const qint64 start = secsOf(e.startUtc);
        const int dur = clampedDuration(e);
        if (start < fromSecs || start >= toSecs || dur <= 0)
            continue;
        p.seconds += dur;
        pages.insert(e.sessionUuid + QLatin1Char('#') + QString::number(e.page));
    }
    p.pages = pages.size();
    if (p.pages > 0 && p.seconds >= 60) {
        p.valid = true;
        p.pagesPerHour = double(p.pages) * 3600.0 / double(p.seconds);
    }
    return p;
}

BookStats computeBookStats(const QVector<SessionRecord> &sessions,
                           const QVector<PageEventRecord> &events, const ZoneOffsetFn &zone)
{
    BookStats b;
    qint64 firstStart = 0;
    qint64 newestSessionStart = -1;
    int sessionPages = 0;
    for (const SessionRecord &s : sessions) {
        if (s.state == SessionState::Dropped)
            continue;
        ++b.sessionCount;
        b.totalSeconds += s.activeSeconds;
        const qint64 start = secsOf(s.startUtc);
        if (firstStart == 0 || start < firstStart)
            firstStart = start;
        if (start > newestSessionStart && s.totalPages > 0) {
            newestSessionStart = start;
            sessionPages = s.totalPages;
        }
    }
    if (b.sessionCount > 0)
        b.averageSessionSeconds = (b.totalSeconds + b.sessionCount / 2) / b.sessionCount;
    b.pace = pagesPerHour(events, 0, Q_INT64_C(0x7fffffffffff));
    b.perDay = secondsPerDay(events, zone);
    if (!b.perDay.isEmpty())
        b.firstDay = b.perDay.constBegin().key();
    else if (firstStart > 0)
        b.firstDay = localDate(firstStart, zone);
    qint64 newestEvent = -1;
    b.totalPages = sessionPages;
    for (const PageEventRecord &e : events) {
        const qint64 start = secsOf(e.startUtc);
        if (e.totalPages > 0 && start > newestEvent) {
            newestEvent = start;
            b.totalPages = e.totalPages;
        }
    }
    return b;
}

QVector<qint64> recentDays(const QMap<QDate, qint64> &perDay, const QDate &today, int days)
{
    QVector<qint64> out;
    for (int i = days - 1; i >= 0; --i)
        out.append(perDay.value(today.addDays(-i), 0));
    return out;
}

Forecast forecast(const ForecastInput &in)
{
    Forecast f;
    if (in.sessionCount < kForecastMinSessions)
        return f; // NotEnoughReading

    const Pace &pace = in.bookPace.valid ? in.bookPace : in.devicePace;
    if (!pace.valid || in.totalPages <= 0)
        return f;

    QVector<double> minutes;
    for (qint64 s : in.recentDaySeconds)
        if (s > 0)
            minutes.append(double(s) / 60.0);
    if (minutes.isEmpty())
        return f;

    const double pagesLeft = double(in.totalPages) * (1.0 - in.percent / 100.0);
    if (pagesLeft <= 0.0) {
        f.state = Forecast::Finished;
        return f;
    }

    std::sort(minutes.begin(), minutes.end());
    const int n = minutes.size();
    const double median =
            (n % 2 == 1) ? minutes.at(n / 2) : (minutes.at(n / 2 - 1) + minutes.at(n / 2)) / 2.0;
    const double perDay = std::max(median, 1.0);

    f.state = Forecast::Ok;
    f.minutesLeft = pagesLeft / pace.pagesPerHour * 60.0;
    f.perDayMinutes = int(perDay + 0.5);
    // Reading every day at that pace: the first day gets perDay minutes.
    f.daysOut = std::max(0, int(std::ceil(f.minutesLeft / perDay)) - 1);
    f.finishDate = in.today.addDays(f.daysOut);
    return f;
}

QMap<QDate, qint64> unsyncedSecondsPerDay(const QVector<PageEventRecord> &events,
                                          const QVector<bool> &confirmed, qint64 fetchedSecs,
                                          const SessionRecord *openSession,
                                          const ZoneOffsetFn &zone)
{
    QMap<QDate, qint64> out;
    auto add = [&](qint64 start, qint64 seconds) {
        splitAtLocalUnits(start, seconds, 86400, zone, [&](qint64 day, qint64, qint64 piece) {
            out[dateFromDayIndex(day)] += piece;
        });
    };

    for (int i = 0; i < events.size(); ++i) {
        const PageEventRecord &e = events.at(i);
        const int dur = clampedDuration(e);
        if (dur <= 0 || !e.startUtc.isValid())
            continue;
        const bool isConfirmed = i < confirmed.size() && confirmed.at(i);
        const qint64 start = secsOf(e.startUtc);
        if (fetchedSecs < 0 || !isConfirmed || start >= fetchedSecs)
            add(start, dur);
    }

    // The open session's page on screen is not a page event yet: its time is the
    // session's active time minus what its page events already account for.
    if (openSession && openSession->state == SessionState::Open && openSession->endUtc.isValid()) {
        qint64 inEvents = 0;
        for (const PageEventRecord &e : events)
            if (e.sessionUuid == openSession->uuid)
                inEvents += clampedDuration(e);
        const qint64 extra = openSession->activeSeconds - inEvents;
        if (extra > 0)
            add(secsOf(openSession->endUtc) - extra, extra);
    }
    return out;
}

QMap<QDate, int> mergeMinutes(const DashboardReadingYear *server,
                              const QMap<QDate, qint64> &unsyncedSeconds)
{
    QMap<QDate, qint64> seconds;
    if (server && server->startDate.isValid()) {
        for (int i = 0; i < server->minutes.size(); ++i)
            seconds[server->startDate.addDays(i)] += qint64(server->minutes.at(i)) * 60;
    }
    for (QMap<QDate, qint64>::const_iterator it = unsyncedSeconds.constBegin();
         it != unsyncedSeconds.constEnd(); ++it)
        seconds[it.key()] += it.value();

    QMap<QDate, int> out;
    for (QMap<QDate, qint64>::const_iterator it = seconds.constBegin(); it != seconds.constEnd();
         ++it)
        out.insert(it.key(), int((it.value() + 30) / 60));
    return out;
}

int adjustedStreak(int serverCurrent, int todayMinutes)
{
    if (serverCurrent == 0 && todayMinutes > 0)
        return 1;
    return serverCurrent;
}

int heatLevel(int minutes)
{
    if (minutes <= 0)
        return 0;
    if (minutes < 15)
        return 1;
    if (minutes < 30)
        return 2;
    if (minutes < 60)
        return 3;
    return 4;
}

HeatmapGrid buildHeatmap(const QMap<QDate, int> &minutesByDate, const QDate &today, int weeks)
{
    HeatmapGrid g;
    g.weeks = std::max(0, weeks);
    const QDate monday = today.addDays(-(today.dayOfWeek() - 1));
    g.firstDate = monday.addDays(-(g.weeks - 1) * 7);
    g.cells.reserve(g.weeks * 7);
    for (int i = 0; i < g.weeks * 7; ++i) {
        HeatCell c;
        c.date = g.firstDate.addDays(i);
        c.future = c.date > today;
        c.minutes = c.future ? 0 : minutesByDate.value(c.date, 0);
        c.level = heatLevel(c.minutes);
        g.cells.append(c);
    }
    return g;
}

} // namespace stats
} // namespace smalt
