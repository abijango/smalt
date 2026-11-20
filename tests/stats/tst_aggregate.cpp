// Task 23, acceptance tests: aggregation against fixture data. SPEC: Stats
// ("Sources", "Screens", "Rules"); On-device data model ("How BookOrbit builds
// sessions"). Pure functions only: no ledger, no clock, a fixed time zone.
#include "test_support.h"

#include "stats/aggregate.h"
#include "stats/format.h"

#include <QtTest>

using namespace smalt;
using namespace smalt::stats;
using namespace statstest;

namespace {

PageEventRecord ev(const QDateTime &start, int seconds, int page = 1,
                   const QString &session = QStringLiteral("s1"),
                   const QString &book = QStringLiteral("book-a"))
{
    return pageEvent(session, book, page, start, seconds);
}

SessionRecord session(const QDateTime &start, int active,
                      SessionState state = SessionState::Finished)
{
    SessionRecord s;
    s.uuid = QStringLiteral("u%1").arg(unixSecs(start));
    s.contentId = QStringLiteral("book-a");
    s.state = state;
    s.startUtc = start;
    s.endUtc = start.addSecs(active + 30);
    s.activeSeconds = active;
    return s;
}

} // namespace

class TstAggregate : public QObject
{
    Q_OBJECT

private slots:

    // --- Minutes per day, split at midnight ---

    void sessionCrossingMidnightSplitsAcrossDays()
    {
        // 23:50 to 00:10: ten minutes on each day.
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 19, 23, 50), 1200);
        const QMap<QDate, qint64> days = secondsPerDay(events, fixedZone(0));
        QCOMPARE(days.size(), 2);
        QCOMPARE(days.value(QDate(2026, 11, 19)), qint64(600));
        QCOMPARE(days.value(QDate(2026, 11, 20)), qint64(600));
    }

    void aPageLeftForDaysSplitsAcrossEveryDayAndIsClampedToOneDay()
    {
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 10, 18, 0), 5 * 86400); // Clamped to 86400 s like the upload.
        qint64 total = 0;
        const QMap<QDate, qint64> days = secondsPerDay(events, fixedZone(0));
        for (qint64 v : days)
            total += v;
        QCOMPARE(total, qint64(86400));
        QCOMPARE(days.value(QDate(2026, 11, 10)), qint64(6 * 3600));
        QCOMPARE(days.value(QDate(2026, 11, 11)), qint64(18 * 3600));
    }

    void sleepGapsAreNeverCounted()
    {
        // 21:00-21:10, the device sleeps for 50 minutes, 22:00-22:05: 15 minutes read, not 65.
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 20, 21, 0), 600, 1) << ev(utc(2026, 11, 20, 22, 0), 300, 2);
        const QMap<QDate, qint64> days = secondsPerDay(events, fixedZone(0));
        QCOMPARE(days.value(QDate(2026, 11, 20)), qint64(900));

        const QVector<TimeBlock> blocks = blocksForDay(events, QDate(2026, 11, 20), fixedZone(0));
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].startSec, 21 * 3600);
        QCOMPARE(blocks[0].endSec, 21 * 3600 + 600);
        QCOMPARE(blocks[1].startSec, 22 * 3600);
        QCOMPARE(blocks[1].endSec, 22 * 3600 + 300);
    }

    void contiguousPagesFormOneBlockAndOverlongGapsSplit()
    {
        QVector<PageEventRecord> events;
        const QDateTime t = utc(2026, 11, 20, 8, 0);
        events << ev(t, 60, 1) << ev(t.addSecs(60), 45, 2)
               << ev(t.addSecs(105 + 30), 30, 3) // A 30 s gap still merges (<= 60 s).
               << ev(t.addSecs(105 + 30 + 30 + 61), 30, 4); // A 61 s gap does not.
        const QVector<TimeBlock> blocks = blocksForDay(events, QDate(2026, 11, 20), fixedZone(0));
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].startSec, 8 * 3600);
        QCOMPARE(blocks[0].endSec, 8 * 3600 + 165);
    }

    void blocksAreClippedToTheirDay()
    {
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 19, 23, 50), 1200);
        const QVector<TimeBlock> today = blocksForDay(events, QDate(2026, 11, 20), fixedZone(0));
        QCOMPARE(today.size(), 1);
        QCOMPARE(today[0].startSec, 0);
        QCOMPARE(today[0].endSec, 600);
        const QVector<TimeBlock> yesterday =
                blocksForDay(events, QDate(2026, 11, 19), fixedZone(0));
        QCOMPARE(yesterday.size(), 1);
        QCOMPARE(yesterday[0].startSec, 23 * 3600 + 50 * 60);
        QCOMPARE(yesterday[0].endSec, 86400);
    }

    // --- Time zones ---

    void theSameReadingFallsOnDifferentDaysInDifferentZones()
    {
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 20, 23, 30), 600); // 23:30 UTC
        QCOMPARE(secondsPerDay(events, fixedZone(0)).keys(), QList<QDate>() << QDate(2026, 11, 20));
        // UTC+2: 01:30 on the 21st. UTC-8: 15:30 on the 20th.
        QCOMPARE(secondsPerDay(events, fixedZone(2 * 3600)).keys(),
                 QList<QDate>() << QDate(2026, 11, 21));
        QCOMPARE(secondsPerDay(events, fixedZone(-8 * 3600)).keys(),
                 QList<QDate>() << QDate(2026, 11, 20));
        QCOMPARE(localDate(unixSecs(utc(2026, 11, 20, 23, 30)), fixedZone(2 * 3600)),
                 QDate(2026, 11, 21));
        QCOMPARE(secondsIntoLocalDay(unixSecs(utc(2026, 11, 20, 23, 30)), fixedZone(2 * 3600)),
                 90 * 60);
    }

    void aZoneChangeInsideTheDataMovesOnlyWhatComesAfter()
    {
        // The device moved from UTC to UTC+3 at 12:00 UTC on the 20th.
        const qint64 change = unixSecs(utc(2026, 11, 20, 12, 0));
        const ZoneOffsetFn zone = [change](qint64 t) { return t < change ? 0 : 3 * 3600; };
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 19, 23, 0), 600, 1) // Before: still the 19th.
               << ev(utc(2026, 11, 20, 22, 0), 600, 2); // After: 01:00 on the 21st.
        const QMap<QDate, qint64> days = secondsPerDay(events, zone);
        QCOMPARE(days.value(QDate(2026, 11, 19)), qint64(600));
        QCOMPARE(days.value(QDate(2026, 11, 21)), qint64(600));
        QVERIFY(!days.contains(QDate(2026, 11, 20)));
    }

    void midnightFallsWhereTheLocalClockSaysAcrossASpanInAnotherZone()
    {
        // UTC+5:30: the span 18:00-19:00 UTC runs from 23:30 to 00:30 local.
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 20, 18, 0), 3600);
        const QMap<QDate, qint64> days = secondsPerDay(events, fixedZone(5 * 3600 + 1800));
        QCOMPARE(days.value(QDate(2026, 11, 20)), qint64(1800));
        QCOMPARE(days.value(QDate(2026, 11, 21)), qint64(1800));
    }

    void theSystemZoneAgreesWithQt()
    {
        const qint64 t = unixSecs(utc(2026, 7, 1, 12, 0));
        const QDateTime local = QDateTime::fromMSecsSinceEpoch(t * 1000);
        QCOMPARE(localDate(t, systemZone()), local.date());
        QCOMPARE(secondsIntoLocalDay(t, systemZone()), QTime(0, 0).secsTo(local.time()));
    }

    // --- Empty ledger ---

    void anEmptyLedgerGivesEmptyResults()
    {
        const QVector<PageEventRecord> none;
        QVERIFY(secondsPerDay(none, fixedZone(0)).isEmpty());
        QVERIFY(blocksForDay(none, QDate(2026, 11, 20), fixedZone(0)).isEmpty());
        const QVector<qint64> hours =
                secondsByHour(none, fixedZone(0), QDate(2026, 8, 1), QDate(2026, 11, 20));
        QCOMPARE(hours.size(), 24);
        for (qint64 h : hours)
            QCOMPARE(h, qint64(0));
        QCOMPARE(busiestTwoHours(hours), -1);
        QVERIFY(hourSummary(hours).isEmpty());
        const QVector<double> weekdays = averageMinutesByWeekday(
                QMap<QDate, qint64>(), QDate(2026, 8, 23), QDate(2026, 11, 20));
        QCOMPARE(weekdays.size(), 7);
        for (double d : weekdays)
            QCOMPARE(d, 0.0);
        const Records r =
                computeRecords(QVector<SessionRecord>(), QMap<QDate, qint64>(), fixedZone(0));
        QVERIFY(!r.hasLongestSession && !r.hasBestDay);
        QCOMPARE(r.sessionCount, 0);
        QCOMPARE(r.averageSessionSeconds, qint64(0));
        QVERIFY(!pagesPerHour(none, 0, Q_INT64_C(0x7fffffffffff)).valid);
        const BookStats b = computeBookStats(QVector<SessionRecord>(), none, fixedZone(0));
        QCOMPARE(b.sessionCount, 0);
        QVERIFY(!b.firstDay.isValid());
    }

    // --- Patterns ---

    void minutesByHourSplitAtHourBoundariesWithinTheLast90Days()
    {
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 19, 21, 30), 3600, 1) // 30 min in hour 21, 30 in hour 22.
               << ev(utc(2026, 11, 20, 21, 0), 3600, 2) // Full hour 21.
               << ev(utc(2026, 8, 1, 9, 0), 3600, 3); // Older than 90 days: left out.
        const QVector<qint64> hours = secondsByHour(
                events, fixedZone(0), QDate(2026, 11, 20).addDays(-89), QDate(2026, 11, 20));
        QCOMPARE(hours[21], qint64(1800 + 3600));
        QCOMPARE(hours[22], qint64(1800));
        QCOMPARE(hours[9], qint64(0));
        QCOMPARE(busiestTwoHours(hours), 21);
        QCOMPARE(hourSummary(hours), QStringLiteral("Mostly 21:00–23:00"));
    }

    void averageMinutesByWeekdayCountsDaysWithoutReadingAsZero()
    {
        // 14 days Mon 2 Nov .. Sun 15 Nov: two of each weekday. Only the Saturdays are read.
        QMap<QDate, qint64> perDay;
        perDay[QDate(2026, 11, 7)] = 60 * 60; // Saturday
        perDay[QDate(2026, 11, 14)] = 30 * 60; // Saturday
        const QVector<double> avg =
                averageMinutesByWeekday(perDay, QDate(2026, 11, 2), QDate(2026, 11, 15));
        QCOMPARE(avg[5], 45.0); // Saturday
        QCOMPARE(avg[0], 0.0); // Monday
        QCOMPARE(avg[6], 0.0); // Sunday
    }

    void recordsIgnoreDroppedSessions()
    {
        QVector<SessionRecord> sessions;
        sessions << session(utc(2026, 3, 3, 20, 0), 8040) // 2 h 14 m
                 << session(utc(2026, 5, 1, 20, 0), 1800) << session(utc(2026, 5, 2, 20, 0), 2400)
                 << session(utc(2026, 5, 3, 20, 0), 99999, SessionState::Dropped);
        QMap<QDate, qint64> perDay;
        perDay[QDate(2026, 12, 26)] = 4 * 3600 + 5 * 60;
        perDay[QDate(2026, 3, 3)] = 8040;
        const Records r = computeRecords(sessions, perDay, fixedZone(0));
        QVERIFY(r.hasLongestSession);
        QCOMPARE(r.longestSessionSeconds, qint64(8040));
        QCOMPARE(r.longestSessionDate, QDate(2026, 3, 3));
        QCOMPARE(r.bestDaySeconds, qint64(4 * 3600 + 5 * 60));
        QCOMPARE(r.bestDayDate, QDate(2026, 12, 26));
        QCOMPARE(r.sessionCount, 3);
        QCOMPARE(r.averageSessionSeconds, qint64((8040 + 1800 + 2400) / 3));
    }

    void pagesPerHourCountsDistinctPagesPerSession()
    {
        QVector<PageEventRecord> events;
        const QDateTime t = utc(2026, 11, 18, 20, 0);
        // 20 pages in 30 minutes, then turning back to page 3 and 4 again (2 more minutes).
        for (int i = 0; i < 20; ++i)
            events << ev(t.addSecs(i * 90), 90, i + 1);
        events << ev(t.addSecs(1800), 60, 3) << ev(t.addSecs(1860), 60, 4);
        const Pace p = pagesPerHour(events, unixSecs(t) - 1, unixSecs(t) + 86400);
        QVERIFY(p.valid);
        QCOMPARE(p.pages, 20);
        QCOMPARE(p.seconds, qint64(1800 + 120));
        QVERIFY(qAbs(p.pagesPerHour - 20.0 * 3600.0 / 1920.0) < 1e-9);
    }

    void paceNeedsAMinuteOfReadingAndRespectsItsWindow()
    {
        QVector<PageEventRecord> events;
        events << ev(utc(2026, 11, 18, 20, 0), 30, 1);
        QVERIFY(!pagesPerHour(events, 0, Q_INT64_C(0x7fffffffffff)).valid);
        events << ev(utc(2026, 10, 1, 20, 0), 600, 2);
        const qint64 from = unixSecs(utc(2026, 10, 21));
        const Pace p = pagesPerHour(events, from, unixSecs(utc(2026, 11, 21)));
        QCOMPARE(p.seconds, qint64(30)); // The October page is outside the window.
    }

    void bookStatsGatherTotalsDaysAndPages()
    {
        QVector<SessionRecord> sessions;
        sessions << session(utc(2026, 10, 28, 20, 0), 1200)
                 << session(utc(2026, 11, 2, 20, 0), 1800)
                 << session(utc(2026, 11, 3, 20, 0), 99, SessionState::Dropped);
        QVector<PageEventRecord> events;
        events << pageEvent(QStringLiteral("a"), QStringLiteral("b"), 1, utc(2026, 10, 28, 20, 0),
                            600, 280)
               << pageEvent(QStringLiteral("a"), QStringLiteral("b"), 2, utc(2026, 10, 28, 20, 10),
                            600, 280)
               << pageEvent(QStringLiteral("b"), QStringLiteral("b"), 3, utc(2026, 11, 2, 20, 0),
                            1800, 312);
        const BookStats b = computeBookStats(sessions, events, fixedZone(0));
        QCOMPARE(b.sessionCount, 2);
        QCOMPARE(b.totalSeconds, qint64(3000));
        QCOMPARE(b.averageSessionSeconds, qint64(1500));
        QCOMPARE(b.firstDay, QDate(2026, 10, 28));
        QCOMPARE(b.perDay.size(), 2);
        QCOMPARE(b.totalPages, 312); // From the newest page event.
    }

    // --- Heatmap ---

    void heatLevelsAreFixedMinuteBands()
    {
        QCOMPARE(heatLevel(0), 0);
        QCOMPARE(heatLevel(1), 1);
        QCOMPARE(heatLevel(14), 1);
        QCOMPARE(heatLevel(15), 2);
        QCOMPARE(heatLevel(29), 2);
        QCOMPARE(heatLevel(30), 3);
        QCOMPARE(heatLevel(59), 3);
        QCOMPARE(heatLevel(60), 4);
        QCOMPARE(heatLevel(600), 4);
    }

    void heatmapGridEndsWithTodaysWeekAndBlanksTheFuture()
    {
        QMap<QDate, int> minutes;
        minutes[QDate(2026, 11, 20)] = 45; // Friday
        minutes[QDate(2026, 11, 16)] = 5; // Monday
        const HeatmapGrid g = buildHeatmap(minutes, QDate(2026, 11, 20), 26);
        QCOMPARE(g.weeks, 26);
        QCOMPARE(g.cells.size(), 26 * 7);
        QCOMPARE(g.firstDate, QDate(2026, 11, 16).addDays(-25 * 7));
        QCOMPARE(g.firstDate.dayOfWeek(), 1);
        QCOMPARE(g.cell(25, 0).date, QDate(2026, 11, 16));
        QCOMPARE(g.cell(25, 0).level, 1);
        QCOMPARE(g.cell(25, 4).date, QDate(2026, 11, 20));
        QCOMPARE(g.cell(25, 4).level, 3);
        QVERIFY(!g.cell(25, 4).future);
        QVERIFY(g.cell(25, 5).future); // Saturday
        QVERIFY(g.cell(25, 6).future);
        QCOMPARE(g.cell(25, 5).minutes, 0);
        QCOMPARE(buildHeatmap(minutes, QDate(2026, 11, 20), 52).weeks, 52);
    }

    // --- Formatting ---

    void durationsReadLikeTheMockup()
    {
        QCOMPARE(formatDuration(34 * 60), QStringLiteral("34 min"));
        QCOMPARE(formatDuration(106 * 60), QStringLiteral("1 h 46 m"));
        QCOMPARE(formatDuration(245 * 60), QStringLiteral("4 h 05 m"));
        QCOMPARE(formatDuration(0), QStringLiteral("0 min"));
        QCOMPARE(formatMinutes(65 * 60), QStringLiteral("65 min"));
        QCOMPARE(formatHours(212 * 3600 + 600), QStringLiteral("212 h"));
        QCOMPARE(formatRange(22 * 3600 + 5 * 60, 23 * 3600 + 10 * 60),
                 QStringLiteral("22:05–23:10"));
        QCOMPARE(formatShortDate(QDate(2026, 3, 3)), QStringLiteral("3 Mar"));
        const QDate today(2026, 11, 20);
        QCOMPARE(formatDayLabel(today, today), QStringLiteral("Today"));
        QCOMPARE(formatDayLabel(today.addDays(-1), today), QStringLiteral("Yesterday"));
        QCOMPARE(formatDayLabel(today.addDays(-5), today), QStringLiteral("15 Nov"));
    }

    void goalPaceSaysAheadOrBehind()
    {
        DashboardGoal g;
        g.hasGoalBooks = true;
        g.goalBooks = 30;
        g.hasCompleted = true;
        g.completedBooks = 18;
        g.hasYear = true;
        g.year = 2026;
        // Day 324 of 365: 26.6 books expected; 18 done is 9 behind.
        QCOMPARE(goalPace(g, 18, 324, 365), QStringLiteral("2026 goal · 9 behind"));
        QCOMPARE(goalPace(g, 30, 324, 365), QStringLiteral("2026 goal · 3 ahead"));
        QCOMPARE(goalPace(g, 27, 324, 365), QStringLiteral("2026 goal · on pace"));
        QCOMPARE(goalTitle(g), QStringLiteral("18 of 30 books"));
        g.hasGoalBooks = false;
        QVERIFY(goalTitle(g).isEmpty());
        QVERIFY(goalPace(g, 1, 1, 365).isEmpty());
    }
};

QTEST_MAIN(TstAggregate)
#include "tst_aggregate.moc"
