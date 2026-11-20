// Task 23, acceptance tests: parsing BookOrbit's dashboard, missing fields
// hiding their widgets, and the copy in remote_cache. SPEC: Stats ("Sources":
// "Achievements and highlight of the day are hidden when the server sends
// none"; "the screens work offline from the cache").
#include "test_support.h"

#include "bookorbit/dashboard.h"
#include "stats/snapshot.h"

#include <QJsonArray>
#include <QtTest>

using namespace smalt;
using namespace smalt::stats;
using namespace statstest;

namespace {

QJsonObject full()
{
    return loadFixtureObject(QStringLiteral("dashboard_full.json"));
}

StatsSnapshot snapshotOf(const QJsonObject &body)
{
    StatsInputs in;
    in.nowUtc = fixedNow();
    in.zone = fixedZone(0);
    in.heatmapWeeks = 26;
    in.hasDashboard = true;
    in.dashboard = parseDashboard(body);
    in.dashboardFetchedUtc = fixedNow().addSecs(-600);
    return buildSnapshot(in);
}

} // namespace

class TstDashboard : public QObject
{
    Q_OBJECT

private slots:

    void parsesEveryFieldOfTheBrief()
    {
        const QJsonObject body = full();
        QVERIFY(!body.isEmpty());
        const Dashboard d = parseDashboard(body);
        QVERIFY(!d.isEmpty());

        QVERIFY(d.hasReadingYear);
        QCOMPARE(d.readingYear.startDate, QDate(2025, 11, 15));
        QCOMPARE(d.readingYear.minutes.size(), 371);
        QCOMPARE(d.readingYear.minutes.last(), 30);
        QCOMPARE(d.readingYear.minutes.at(370 - 1), 45);

        QVERIFY(d.hasReadingStreak);
        QVERIFY(d.readingStreak.hasCurrent && d.readingStreak.hasLongest
                && d.readingStreak.hasLastSeven);
        QCOMPARE(d.readingStreak.current, 12);
        QCOMPARE(d.readingStreak.longest, 31);
        QCOMPARE(d.readingStreak.lastSevenDays.size(), 7);
        QVERIFY(d.readingStreak.lastSevenDays.at(5));
        QVERIFY(!d.readingStreak.lastSevenDays.at(6));

        QVERIFY(d.hasReadingGoal);
        QVERIFY(d.readingGoal.hasGoalBooks && d.readingGoal.hasCompleted && d.readingGoal.hasYear);
        QCOMPARE(d.readingGoal.goalBooks, 30);
        QCOMPARE(d.readingGoal.completedBooks, 18);
        QCOMPARE(d.readingGoal.year, 2026);

        QVERIFY(d.hasFinishedThisYear);
        QCOMPARE(d.finishedThisYear.size(), 12);
        QCOMPARE(d.finishedThisYear.first().id, qint64(1));
        QCOMPARE(d.finishedThisYear.first().title, QStringLiteral("Book 1"));
        QCOMPARE(d.finishedThisYear.first().finishedOn, QStringLiteral("2026-11-11"));

        QVERIFY(d.hasYearAgo);
        QCOMPARE(d.yearAgo.book.title, QStringLiteral("Piranesi"));
        QCOMPARE(d.yearAgo.book.authors, QStringList() << QStringLiteral("Susanna Clarke"));
        QVERIFY(d.yearAgo.hasMinutes && d.yearAgo.hasDays && d.yearAgo.hasEndProgress);
        QCOMPARE(d.yearAgo.minutes, 250);
        QCOMPARE(d.yearAgo.days, 6);
        QCOMPARE(d.yearAgo.endProgress, 62.0);
        QCOMPARE(d.yearAgo.firstDay, QDate(2025, 11, 17));

        QVERIFY(d.hasNextBadge);
        QCOMPARE(d.nextBadge.name, QStringLiteral("250 hours this year"));
        QCOMPARE(d.nextBadge.groupKey, QStringLiteral("hours_read"));
        QCOMPARE(d.nextBadge.threshold, 250.0);
        QCOMPARE(d.nextBadge.currentProgress, 212.0);

        QVERIFY(d.hasHighlight);
        QVERIFY(d.highlight.text.startsWith(QStringLiteral("The Beauty of the House")));
        QCOMPARE(d.highlight.bookTitle, QStringLiteral("Piranesi"));

        QVERIFY(d.generatedAtUtc.isValid());
    }

    void anEmptyBodyIsEmptyAndNothingBreaks()
    {
        QVERIFY(parseDashboard(QJsonObject()).isEmpty());
        // What the mock serves today: no stats fields at all.
        QJsonObject mock;
        mock.insert(QStringLiteral("continueReading"), QJsonArray());
        mock.insert(QStringLiteral("totalBooks"), 4);
        QVERIFY(parseDashboard(mock).isEmpty());
    }

    void nullsAndWrongTypesLeaveFieldsAbsent()
    {
        QJsonObject body;
        body.insert(QStringLiteral("readingYear"), QJsonValue::Null);
        body.insert(QStringLiteral("readingStreak"), QStringLiteral("many"));
        QJsonObject goal;
        goal.insert(QStringLiteral("goalBooks"), QJsonValue::Null); // No goal set.
        goal.insert(QStringLiteral("completedBooks"), 4);
        goal.insert(QStringLiteral("year"), 2026);
        body.insert(QStringLiteral("readingGoal"), goal);
        body.insert(QStringLiteral("yearAgo"), QJsonValue::Null);
        QJsonObject ach;
        ach.insert(QStringLiteral("next"), QJsonValue::Null);
        body.insert(QStringLiteral("achievements"), ach);
        body.insert(QStringLiteral("highlightOfTheDay"), QJsonValue::Null);
        body.insert(QStringLiteral("finishedThisYear"), QStringLiteral("none"));
        const Dashboard d = parseDashboard(body);
        QVERIFY(!d.hasReadingYear);
        QVERIFY(!d.hasReadingStreak);
        QVERIFY(d.hasReadingGoal);
        QVERIFY(!d.readingGoal.hasGoalBooks); // goalBooks is null.
        QVERIFY(!d.hasYearAgo);
        QVERIFY(!d.hasNextBadge);
        QVERIFY(!d.hasHighlight);
        QVERIFY(!d.hasFinishedThisYear);
    }

    void aReadingYearNeedsADateAndAList()
    {
        QJsonObject body;
        QJsonObject y;
        y.insert(QStringLiteral("minutes"), QJsonArray::fromVariantList(QVariantList() << 1 << 2));
        body.insert(QStringLiteral("readingYear"), y); // No startDate.
        QVERIFY(!parseDashboard(body).hasReadingYear);
        y.insert(QStringLiteral("startDate"), QStringLiteral("not a date"));
        body.insert(QStringLiteral("readingYear"), y);
        QVERIFY(!parseDashboard(body).hasReadingYear);
        y.insert(QStringLiteral("startDate"), QStringLiteral("2026-11-19"));
        y.insert(QStringLiteral("minutes"),
                 QJsonArray::fromVariantList(QVariantList()
                                             << 5 << QStringLiteral("x") << -3 << 7.6));
        body.insert(QStringLiteral("readingYear"), y);
        const Dashboard d = parseDashboard(body);
        QVERIFY(d.hasReadingYear);
        // Bad and negative entries read as zero (the plugin: tonumber(v) or 0); fractions round.
        QCOMPARE(d.readingYear.minutes, QVector<int>() << 5 << 0 << 0 << 8);
    }

    // Every widget goes when its field goes; the others stay.
    void missingFieldsHideTheirWidgets_data()
    {
        QTest::addColumn<QString>("key");
        QTest::newRow("readingYear") << "readingYear";
        QTest::newRow("readingStreak") << "readingStreak";
        QTest::newRow("readingGoal") << "readingGoal";
        QTest::newRow("finishedThisYear") << "finishedThisYear";
        QTest::newRow("yearAgo") << "yearAgo";
        QTest::newRow("achievements") << "achievements";
        QTest::newRow("highlightOfTheDay") << "highlightOfTheDay";
    }

    void missingFieldsHideTheirWidgets()
    {
        QFETCH(QString, key);
        const StatsSnapshot all = snapshotOf(full());
        QVERIFY(all.showHeatmap && all.showStreak && all.showGoal && all.showFinished
                && all.showYearAgo && all.showBadge && all.showHighlight && all.showTiles);

        QJsonObject body = full();
        body.remove(key);
        const StatsSnapshot s = snapshotOf(body);
        QCOMPARE(s.showHeatmap, key != QLatin1String("readingYear"));
        QCOMPARE(s.showStreak, key != QLatin1String("readingStreak"));
        QCOMPARE(s.showGoal, key != QLatin1String("readingGoal"));
        // The finished shelf shows from either the list or the goal's count: it goes with both.
        QVERIFY(s.showFinished);
        QCOMPARE(s.showYearAgo, key != QLatin1String("yearAgo"));
        QCOMPARE(s.showBadge, key != QLatin1String("achievements"));
        QCOMPARE(s.showHighlight, key != QLatin1String("highlightOfTheDay"));
    }

    void theFinishedShelfGoesWhenBothTheListAndTheGoalAreMissing()
    {
        QJsonObject body = full();
        body.remove(QStringLiteral("finishedThisYear"));
        body.remove(QStringLiteral("readingGoal"));
        QVERIFY(!snapshotOf(body).showFinished);
    }

    void achievementsAndHighlightAreHiddenWhenTheServerSendsNone()
    {
        QJsonObject body = full();
        body.insert(QStringLiteral("achievements"), QJsonValue::Null);
        body.insert(QStringLiteral("highlightOfTheDay"), QJsonValue::Null);
        const StatsSnapshot s = snapshotOf(body);
        QVERIFY(!s.showBadge);
        QVERIFY(!s.showHighlight);
        QVERIFY(s.showHeatmap && s.showFinished);
    }

    void noGoalHidesTheProgressBarButKeepsTheFinishedCount()
    {
        QJsonObject body = full();
        QJsonObject goal = body.value(QStringLiteral("readingGoal")).toObject();
        goal.insert(QStringLiteral("goalBooks"), QJsonValue::Null);
        body.insert(QStringLiteral("readingGoal"), goal);
        const StatsSnapshot s = snapshotOf(body);
        QVERIFY(!s.showGoal);
        QVERIFY(s.showFinished);
        QCOMPARE(s.finishedCount, 18);
    }

    void streakWithoutLongestOrDotsStillShows()
    {
        QJsonObject body = full();
        QJsonObject streak;
        streak.insert(QStringLiteral("currentStreak"), 3);
        body.insert(QStringLiteral("readingStreak"), streak);
        const StatsSnapshot s = snapshotOf(body);
        QVERIFY(s.showStreak);
        QVERIFY(s.hasStreakCurrent && !s.hasStreakLongest);
        QCOMPARE(s.streakCurrent, 3);
        QCOMPARE(s.streakDots.size(), 7);
    }

    void streakDotsAreTheServersDaysPlusTodaysLocalReading()
    {
        QJsonObject body = full();
        const StatsSnapshot s = snapshotOf(body);
        // lastSevenDays: six read, today not. The server's minutes for today are 30,
        // so today reads as read too.
        QCOMPARE(s.streakDots, QVector<bool>(7, true));
        QCOMPARE(s.streakInitials.size(), 7);
        QCOMPARE(s.streakInitials.last(), QStringLiteral("F")); // Friday
    }

    void trimmedBodyKeepsOnlyWhatStatsReads()
    {
        const QJsonObject trimmed = trimDashboard(full());
        QVERIFY(!trimmed.contains(QStringLiteral("continueReading")));
        QVERIFY(!trimmed.contains(QStringLiteral("discover")));
        QVERIFY(trimmed.contains(QStringLiteral("readingYear")));
        QVERIFY(trimmed.contains(QStringLiteral("achievements")));
    }

    // --- remote_cache ---

    void theCacheRoundTripsAndKeepsItsFetchTime()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        Dashboard d;
        QDateTime updated;
        QCOMPARE(loadCachedDashboard(fx.ledger(), &d, &updated).code, StatusCode::NotFound);

        fx.cacheDashboard(full(), utc(2026, 11, 20, 11, 0));
        QVERIFY(loadCachedDashboard(fx.ledger(), &d, &updated).ok());
        QCOMPARE(updated, utc(2026, 11, 20, 11, 0));
        QVERIFY(d.hasReadingYear && d.hasNextBadge && d.hasFinishedThisYear);
        QCOMPARE(d.readingYear.minutes.size(), 371);

        // A newer fetch replaces it.
        QJsonObject newer = full();
        newer.remove(QStringLiteral("highlightOfTheDay"));
        fx.cacheDashboard(newer, utc(2026, 11, 20, 12, 0));
        QVERIFY(loadCachedDashboard(fx.ledger(), &d, &updated).ok());
        QVERIFY(!d.hasHighlight);
        QCOMPARE(updated, utc(2026, 11, 20, 12, 0));
    }

    void anUnreadableCacheEntryReadsAsAbsent()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        QVERIFY(fx.ledger()
                        ->cachePut(QString::fromLatin1(kDashboardCacheKey),
                                   QString::fromLatin1(kDashboardCacheKind),
                                   QByteArray("{not json"), fixedNow())
                        .ok());
        Dashboard d;
        QCOMPARE(loadCachedDashboard(fx.ledger(), &d, nullptr).code, StatusCode::NotFound);
        QCOMPARE(loadCachedDashboard(nullptr, &d, nullptr).code, StatusCode::InvalidArgument);
    }

    void snapshotWorksFromTheCacheWithNoNetwork()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        fx.cacheDashboard(full(), fixedNow().addSecs(-3 * 3600));
        const DeviceProfile profile = deviceProfile(DeviceModel::ClaraColour);
        const StatsSnapshot s =
                buildSnapshot(loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0)));
        QVERIFY(s.showHeatmap);
        QCOMPARE(s.heatmap.weeks, 26);
        QCOMPARE(s.todayMinutes, 30);
        QVERIFY(s.showYearAgo && s.showBadge && s.showFinished);
        QCOMPARE(s.finishedCount, 18);
        QCOMPARE(s.finishedBooks.size(), 12);
        QVERIFY(s.hasYearSeconds);
    }

    void withNothingCachedTheAllDevicesWidgetsAreAbsent()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        const DeviceProfile profile = deviceProfile(DeviceModel::ClaraColour);
        const StatsSnapshot s =
                buildSnapshot(loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0)));
        QVERIFY(!s.showHeatmap && !s.showStreak && !s.showGoal && !s.showFinished && !s.showYearAgo
                && !s.showBadge && !s.showHighlight && !s.showTiles);
        QVERIFY(!s.hasReading);
        QVERIFY(s.todayBlocks.isEmpty());
        // A closed ledger is the same.
        fx.ledger()->close();
        const StatsSnapshot closed =
                buildSnapshot(loadInputs(fx.ledger(), profile, fixedNow(), fixedZone(0)));
        QVERIFY(!closed.showHeatmap && !closed.hasReading);
    }

    void libraGetsFiftyTwoWeeks()
    {
        LedgerFixture fx;
        QVERIFY(fx.ok());
        fx.cacheDashboard(full(), fixedNow().addSecs(-600));
        const StatsSnapshot s = buildSnapshot(loadInputs(
                fx.ledger(), deviceProfile(DeviceModel::LibraColour), fixedNow(), fixedZone(0)));
        QCOMPARE(s.heatmap.weeks, 52);
    }

    void yearHoursSumTheServersMinutesForThisYear()
    {
        const StatsSnapshot s = snapshotOf(full());
        // Jan 1 to Nov 20 2026 is 324 days of the fixture's pattern.
        const Dashboard d = parseDashboard(full());
        qint64 minutes = 0;
        for (int i = 0; i < d.readingYear.minutes.size(); ++i)
            if (d.readingYear.startDate.addDays(i).year() == 2026)
                minutes += d.readingYear.minutes.at(i);
        QVERIFY(s.hasYearSeconds);
        QCOMPARE(s.yearSeconds, minutes * 60);
    }
};

QTEST_MAIN(TstDashboard)
#include "tst_dashboard.moc"
