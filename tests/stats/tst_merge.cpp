// Task 23, acceptance tests: merging this Kobo's unsent minutes into the
// server's heatmap. SPEC: Stats ("Sources"): reading recorded here but not yet
// confirmed by the server is added to the cached heatmap, and once the
// server's day includes it the local addition is dropped. Never double counted.
#include "test_support.h"

#include "stats/aggregate.h"
#include "stats/snapshot.h"

#include <QJsonArray>
#include <QtTest>

using namespace smalt;
using namespace smalt::stats;
using namespace statstest;

namespace {

const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/A/B.epub");

// The server's year: Nov 10 to Nov 20, ten minutes a day, `today` minutes today.
DashboardReadingYear serverYear(int today)
{
    DashboardReadingYear y;
    y.startDate = QDate(2026, 11, 10);
    for (int i = 0; i < 10; ++i)
        y.minutes.append(10);
    y.minutes.append(today);
    return y;
}

int todayMinutes(const DashboardReadingYear &server, const QMap<QDate, qint64> &unsynced)
{
    return mergeMinutes(&server, unsynced).value(QDate(2026, 11, 20));
}

// The fixture's last seven days, today included.
int fixtureWeekMinutes()
{
    const Dashboard d = parseDashboard(loadFixtureObject(QStringLiteral("dashboard_full.json")));
    int sum = 0;
    for (int i = d.readingYear.minutes.size() - 7; i < d.readingYear.minutes.size(); ++i)
        sum += d.readingYear.minutes.at(i);
    return sum;
}

PageEventRecord page(int n, const QDateTime &start, int seconds,
                     const QString &session = QStringLiteral("s1"))
{
    return pageEvent(session, kBook, n, start, seconds);
}

} // namespace

class TstMerge : public QObject
{
    Q_OBJECT

private slots:

    // --- Pure merge ---

    void unsyncedMinutesAppearToday()
    {
        QVector<PageEventRecord> events;
        events << page(1, utc(2026, 11, 20, 8, 0), 1200); // 20 minutes, unconfirmed.
        const QVector<bool> confirmed = QVector<bool>() << false;
        const qint64 fetched = unixSecs(utc(2026, 11, 20, 7, 0));
        const QMap<QDate, qint64> unsynced =
                unsyncedSecondsPerDay(events, confirmed, fetched, nullptr, fixedZone(0));
        QCOMPARE(unsynced.value(QDate(2026, 11, 20)), qint64(1200));
        const DashboardReadingYear server = serverYear(30);
        QCOMPARE(todayMinutes(server, unsynced), 50);
        // Other days are the server's own.
        QCOMPARE(mergeMinutes(&server, unsynced).value(QDate(2026, 11, 19)), 10);
    }

    void theAdditionDisappearsOnceTheServerDayIncludesIt()
    {
        QVector<PageEventRecord> events;
        events << page(1, utc(2026, 11, 20, 8, 0), 1200);
        // The push went through (the page is confirmed) and the dashboard was fetched
        // after it: the server's today already says 50 and nothing is added.
        const QVector<bool> confirmed = QVector<bool>() << true;
        const qint64 fetched = unixSecs(utc(2026, 11, 20, 9, 0));
        const QMap<QDate, qint64> unsynced =
                unsyncedSecondsPerDay(events, confirmed, fetched, nullptr, fixedZone(0));
        QVERIFY(unsynced.isEmpty());
        QCOMPARE(todayMinutes(serverYear(50), unsynced), 50);
    }

    void pushedWhileReadingStillCountsUntilTheDashboardIsFetchedAgain()
    {
        QVector<PageEventRecord> events;
        events << page(1, utc(2026, 11, 20, 8, 0), 1200);
        const QVector<bool> confirmed = QVector<bool>() << true; // Pushed at 08:21...
        // ...but the cached dashboard is from 07:00 and does not have it yet.
        const QMap<QDate, qint64> stale = unsyncedSecondsPerDay(
                events, confirmed, unixSecs(utc(2026, 11, 20, 7, 0)), nullptr, fixedZone(0));
        QCOMPARE(todayMinutes(serverYear(30), stale), 50);
        // A fetch at 08:30 has it: the same event is no longer added.
        const QMap<QDate, qint64> fresh = unsyncedSecondsPerDay(
                events, confirmed, unixSecs(utc(2026, 11, 20, 8, 30)), nullptr, fixedZone(0));
        QCOMPARE(todayMinutes(serverYear(50), fresh), 50);
    }

    void aPartlyPushedOpenSessionIsCountedSecondForSecond()
    {
        // Open since 20:00. Page 1 (20:00-20:10) was pushed while reading, page 2
        // (20:10-20:20) was not, and the page on screen since 20:20 is not an event yet.
        // It is 20:27: the session has 27 minutes of reading.
        QVector<PageEventRecord> events;
        events << page(1, utc(2026, 11, 20, 20, 0), 600) << page(2, utc(2026, 11, 20, 20, 10), 600);
        const QVector<bool> confirmed = QVector<bool>() << true << false;
        SessionRecord open;
        open.uuid = QStringLiteral("s1");
        open.contentId = kBook;
        open.state = SessionState::Open;
        open.startUtc = utc(2026, 11, 20, 20, 0);
        open.endUtc = utc(2026, 11, 20, 20, 27);
        open.activeSeconds = 27 * 60;

        // The dashboard was fetched at 19:00, before the session.
        QMap<QDate, qint64> unsynced = unsyncedSecondsPerDay(
                events, confirmed, unixSecs(utc(2026, 11, 20, 19, 0)), &open, fixedZone(0));
        QCOMPARE(unsynced.value(QDate(2026, 11, 20)), qint64(27 * 60));
        QCOMPARE(todayMinutes(serverYear(30), unsynced), 30 + 27);

        // A fetch at 20:15 has page 1 (pushed at 20:12): only page 2 and the live page remain.
        unsynced = unsyncedSecondsPerDay(events, confirmed, unixSecs(utc(2026, 11, 20, 20, 15)),
                                         &open, fixedZone(0));
        QCOMPARE(unsynced.value(QDate(2026, 11, 20)), qint64(17 * 60));
        QCOMPARE(todayMinutes(serverYear(40), unsynced), 40 + 17);
    }

    void theOpenSessionsLivePageSplitsAtMidnight()
    {
        SessionRecord open;
        open.uuid = QStringLiteral("s1");
        open.state = SessionState::Open;
        open.startUtc = utc(2026, 11, 19, 23, 40);
        open.endUtc = utc(2026, 11, 20, 0, 10);
        open.activeSeconds = 30 * 60; // No page events yet.
        const QMap<QDate, qint64> unsynced = unsyncedSecondsPerDay(
                QVector<PageEventRecord>(), QVector<bool>(), 0, &open, fixedZone(0));
        QCOMPARE(unsynced.value(QDate(2026, 11, 19)), qint64(20 * 60));
        QCOMPARE(unsynced.value(QDate(2026, 11, 20)), qint64(10 * 60));
    }

    void staleActiveSecondsNeverMakeANegativeAddition()
    {
        QVector<PageEventRecord> events;
        events << page(1, utc(2026, 11, 20, 20, 0), 600);
        SessionRecord open;
        open.uuid = QStringLiteral("s1");
        open.state = SessionState::Open;
        open.startUtc = utc(2026, 11, 20, 20, 0);
        open.endUtc = utc(2026, 11, 20, 20, 5);
        open.activeSeconds = 300; // Not updated since: less than the events already say.
        const QMap<QDate, qint64> unsynced =
                unsyncedSecondsPerDay(events, QVector<bool>() << false, 0, &open, fixedZone(0));
        QCOMPARE(unsynced.value(QDate(2026, 11, 20)), qint64(600));
    }

    void withoutADashboardEveryEventCounts()
    {
        QVector<PageEventRecord> events;
        events << page(1, utc(2026, 11, 20, 8, 0), 600) << page(2, utc(2026, 11, 19, 8, 0), 300);
        const QMap<QDate, qint64> all = unsyncedSecondsPerDay(
                events, QVector<bool>() << true << true, -1, nullptr, fixedZone(0));
        const QMap<QDate, int> merged = mergeMinutes(nullptr, all);
        QCOMPARE(merged.value(QDate(2026, 11, 20)), 10);
        QCOMPARE(merged.value(QDate(2026, 11, 19)), 5);
    }

    void mergedDaysRoundToWholeMinutes()
    {
        QMap<QDate, qint64> add;
        add[QDate(2026, 11, 20)] = 29; // 10 min + 29 s
        QCOMPARE(todayMinutes(serverYear(10), add), 10);
        add[QDate(2026, 11, 20)] = 30;
        QCOMPARE(todayMinutes(serverYear(10), add), 11);
    }

    void streakCountsTodayThatTheServerHasNotSeen()
    {
        QCOMPARE(adjustedStreak(0, 5), 1);
        QCOMPARE(adjustedStreak(0, 0), 0);
        QCOMPARE(adjustedStreak(12, 0), 12);
        QCOMPARE(adjustedStreak(12, 30), 12);
    }

    // --- Through the real ledger and the cache ---

    void ledgerRoundTripUnsyncedThenServerCatchesUp()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        fx.link(kBook, 7);
        // Yesterday: 60 minutes, pushed. Today at 08:00: 20 minutes, not pushed yet.
        const SessionRecord y = fx.read(kBook, utc(2026, 11, 19, 21, 0), 60, 60, 1);
        const SessionRecord t = fx.read(kBook, utc(2026, 11, 20, 8, 0), 20, 60, 70);
        QVERIFY(!y.uuid.isEmpty() && !t.uuid.isEmpty());
        QVERIFY(fx.ledger()->setStatsWatermark(kBook, unixSecs(utc(2026, 11, 19, 21, 59))).ok());

        // The cached dashboard (fetched at 07:00 today) says 45 minutes yesterday, 30 today.
        QJsonObject body = loadFixtureObject(QStringLiteral("dashboard_full.json"));
        fx.cacheDashboard(body, utc(2026, 11, 20, 7, 0));

        const DeviceProfile profile = deviceProfile(DeviceModel::ClaraColour);
        StatsInputs in = loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0));
        QVERIFY(in.hasDashboard);
        QCOMPARE(in.events.size(), 80);
        QCOMPARE(in.confirmed.size(), 80);
        StatsSnapshot s = buildSnapshot(in);
        QCOMPARE(s.todayMinutes, 30 + 20); // The server's 30 plus today's unsent 20.
        QCOMPARE(s.weekMinutes, fixtureWeekMinutes() + 20);

        // The push goes through and the dashboard is fetched again: today's server
        // minutes now include the 20, and the local addition is gone.
        QVERIFY(fx.ledger()->setStatsWatermark(kBook, unixSecs(utc(2026, 11, 20, 8, 19))).ok());
        QJsonObject caught = body;
        QJsonObject year = caught.value(QStringLiteral("readingYear")).toObject();
        QJsonArray minutes = year.value(QStringLiteral("minutes")).toArray();
        minutes.replace(minutes.size() - 1, 50);
        year.insert(QStringLiteral("minutes"), minutes);
        caught.insert(QStringLiteral("readingYear"), year);
        fx.cacheDashboard(caught, utc(2026, 11, 20, 11, 50));
        in = loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0));
        s = buildSnapshot(in);
        QCOMPARE(s.todayMinutes, 50); // Not 70.
        QCOMPARE(s.weekMinutes, fixtureWeekMinutes() + 20);
    }

    void ledgerPartlyPushedOpenSession()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        fx.link(kBook, 7);
        QJsonObject body = loadFixtureObject(QStringLiteral("dashboard_full.json"));
        fx.cacheDashboard(body, utc(2026, 11, 20, 8, 0));

        SessionRecord s = fx.beginOpen(kBook, utc(2026, 11, 20, 10, 0));
        QVERIFY(!s.uuid.isEmpty());
        QVERIFY(fx.ledger()
                        ->appendPageEvent(
                                pageEvent(s.uuid, kBook, 1, utc(2026, 11, 20, 10, 0), 600))
                        .ok());
        QVERIFY(fx.ledger()
                        ->appendPageEvent(
                                pageEvent(s.uuid, kBook, 2, utc(2026, 11, 20, 10, 10), 600))
                        .ok());
        // Page 1 was pushed while reading; page 2 and the page on screen were not.
        QVERIFY(fx.ledger()->setStatsWatermark(kBook, unixSecs(utc(2026, 11, 20, 10, 0))).ok());
        s.endUtc = utc(2026, 11, 20, 10, 27);
        s.activeSeconds = 27 * 60;
        QVERIFY(fx.ledger()->updateOpenSession(s).ok());

        const DeviceProfile profile = deviceProfile(DeviceModel::ClaraColour);
        const StatsSnapshot snap =
                buildSnapshot(loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0)));
        // Server 30 (fetched 08:00) plus every second of the session once: 27.
        QCOMPARE(snap.todayMinutes, 30 + 27);
        // And its three stretches are on the day strip as one block.
        QCOMPARE(snap.todayBlocks.size(), 1);
        QCOMPARE(snap.todayBlocks[0].startSec, 10 * 3600);
        QCOMPARE(snap.todayBlocks[0].endSec, 10 * 3600 + 27 * 60);
    }

    void withNoDashboardTheTilesCountThisKoboOnly()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        fx.read(kBook, utc(2026, 11, 20, 8, 0), 10, 60);
        const DeviceProfile profile = deviceProfile(DeviceModel::ClaraColour);
        const StatsSnapshot s =
                buildSnapshot(loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0)));
        QVERIFY(!s.showHeatmap); // "All devices" has nothing without the server.
        QVERIFY(s.tilesFromThisKoboOnly);
        QVERIFY(s.showTiles);
        QCOMPARE(s.todayMinutes, 10);
    }
};

QTEST_MAIN(TstMerge)
#include "tst_merge.moc"
