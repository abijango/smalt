// Task 23, acceptance tests: the Stats tab as a widget. Offline use of the
// cache, the chip switcher remembering the last chip, redraw after a sync and
// not otherwise, and fetching the dashboard from the mock server.
// SPEC: Stats ("Screens", "Rules"), UX details ("E Ink rules").
#include "test_support.h"

#include "../bookorbit/mock_server.h"
#include "kit/components.h"
#include "smalt/ui_kit.h"
#include "stats_tab.h"
#include "widgets/blocks.h"
#include "widgets/charts.h"

#include <QJsonArray>
#include <QTimer>
#include <QtTest>

using namespace smalt;
using namespace smalt::ui;
using namespace smalt::ui::stats;
using namespace statstest;
using testing::MockServer;

namespace {

const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/A/B.epub");

QString todayTile(QWidget *page)
{
    TilesWidget *tiles = page->findChild<TilesWidget *>();
    return tiles && !tiles->tiles().isEmpty() ? tiles->tiles().first().value : QString();
}

bool hasTitle(QWidget *page, const QString &title)
{
    const QList<ScopeTitle *> titles = page->findChildren<ScopeTitle *>();
    for (ScopeTitle *t : titles)
        if (t->title() == title)
            return true;
    return false;
}

} // namespace

class TstTab : public QObject
{
    Q_OBJECT

    LedgerFixture *m_fx = nullptr;
    std::unique_ptr<ComponentFactory> m_kit;
    FakeSync m_sync;
    MockServer *m_server = nullptr;

    StatsOptions options() const
    {
        StatsOptions o;
        o.now = [] { return fixedNow(); };
        o.zone = smalt::stats::fixedZone(0);
        return o;
    }

    StatsDeps deps(BookOrbitClient *client = nullptr)
    {
        StatsDeps d;
        d.kit = m_kit.get();
        d.ledger = m_fx->ledger();
        d.client = client;
        d.sync = &m_sync;
        return d;
    }

    StatsTab *makeTab(BookOrbitClient *client = nullptr)
    {
        StatsTab *tab = new StatsTab(deps(client), options(), nullptr);
        const DeviceProfile &p = m_kit->profile();
        tab->resize(p.widthPx, p.heightPx);
        return tab;
    }

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void init()
    {
        m_fx = new LedgerFixture;
        QVERIFY(m_fx->ok());
        m_kit = createComponentFactory(DeviceModel::ClaraColour, nullptr);
        // The tab remembers its chip in the ledger: a fresh ledger starts on Overview.
    }

    void cleanup()
    {
        delete m_server;
        m_server = nullptr;
        m_kit.reset();
        delete m_fx;
        m_fx = nullptr;
    }

    // --- Offline ---

    void offlineUsesTheCachedDashboard()
    {
        m_fx->cacheDashboard(loadFixtureObject(QStringLiteral("dashboard_full.json")),
                             fixedNow().addSecs(-7200));
        std::unique_ptr<StatsTab> tab(makeTab()); // No client at all.
        QCOMPARE(tab->currentChip(), 0);
        QVERIFY(tab->page());
        HeatmapWidget *heat = tab->page()->findChild<HeatmapWidget *>();
        QVERIFY(heat);
        QCOMPARE(heat->grid().weeks, 26);
        QCOMPARE(todayTile(tab->page()), QStringLiteral("30 min"));
        QVERIFY(hasTitle(tab->page(), QStringLiteral("Last 26 weeks")));
        StreakWidget *streak = tab->page()->findChild<StreakWidget *>();
        QVERIFY(streak);
        QCOMPARE(streak->big(), QStringLiteral("12 days"));
        QCOMPARE(streak->caption(), QStringLiteral("Streak · longest 31"));
        GoalWidget *goal = tab->page()->findChild<GoalWidget *>();
        QVERIFY(goal);
        QCOMPARE(goal->title(), QStringLiteral("18 of 30 books"));
    }

    void withNoCacheTheAllDevicesWidgetsAreAbsentButTodayStays()
    {
        std::unique_ptr<StatsTab> tab(makeTab());
        QVERIFY(!tab->page()->findChild<HeatmapWidget *>());
        QVERIFY(!tab->page()->findChild<StreakWidget *>());
        QVERIFY(!tab->page()->findChild<GoalWidget *>());
        QVERIFY(hasTitle(tab->page(), QStringLiteral("Today")));
        QVERIFY(tab->page()->findChild<SessionStrip *>());
    }

    void localReadingShowsInTodaysStripAndTiles()
    {
        m_fx->read(kBook, utc(2026, 11, 20, 9, 0), 12, 60);
        std::unique_ptr<StatsTab> tab(makeTab());
        QCOMPARE(todayTile(tab->page()), QStringLiteral("12 min"));
        SessionStrip *strip = tab->page()->findChild<SessionStrip *>();
        QVERIFY(strip);
        QCOMPARE(strip->blockCount(), 1);
    }

    // --- Chips ---

    void chipsSwitchScreensAndTheLastOneIsRemembered()
    {
        std::unique_ptr<StatsTab> tab(makeTab());
        tab->show();
        QVERIFY(QTest::qWaitForWindowExposed(tab.get()));
        kit::ChipRow *chips = qobject_cast<kit::ChipRow *>(tab->chips());
        QVERIFY(chips);
        QCOMPARE(chips->selected(), QList<int>() << 0);
        QVERIFY(hasTitle(tab->page(), QStringLiteral("Today")));

        const QList<QRect> rects = chips->chipRects();
        QCOMPARE(rects.size(), 3);
        QTest::mouseClick(chips, Qt::LeftButton, Qt::NoModifier, rects[1].center());
        QCOMPARE(tab->currentChip(), 1);
        QCOMPARE(chips->selected(), QList<int>() << 1);
        QVERIFY(hasTitle(tab->page(), QStringLiteral("Time of day")));

        // Tapping the chip that is on keeps it on.
        QTest::mouseClick(chips, Qt::LeftButton, Qt::NoModifier, rects[1].center());
        QCOMPARE(chips->selected(), QList<int>() << 1);

        QTest::mouseClick(chips, Qt::LeftButton, Qt::NoModifier, rects[2].center());
        QCOMPARE(tab->currentChip(), 2);
        QVERIFY(tab->page());

        // A new tab (the shell rebuilt it) opens on the last chip used.
        std::unique_ptr<StatsTab> again(makeTab());
        QCOMPARE(again->currentChip(), 2);
        kit::ChipRow *chips2 = qobject_cast<kit::ChipRow *>(again->chips());
        QCOMPARE(chips2->selected(), QList<int>() << 2);
    }

    // --- Empty states ---

    void patternsAndYearSayWhatIsMissingInsteadOfDrawingEmptyCharts()
    {
        auto chipNow = [&](int chip) {
            QVERIFY(m_fx->ledger()
                            ->cachePut(QStringLiteral("stats.chip"), QStringLiteral("stats-ui"),
                                       QByteArray::number(chip), fixedNow())
                            .ok());
        };
        chipNow(1);
        {
            std::unique_ptr<StatsTab> tab(makeTab());
            QCOMPARE(tab->currentChip(), 1);
            QVERIFY(hasTitle(tab->page(), QStringLiteral("Time of day")));
            QVERIFY(tab->page()->findChild<NoteWidget *>()); // "No reading recorded..."
            QVERIFY(!tab->page()->findChild<HourHistogram *>());
            QVERIFY(!tab->page()->findChild<WeekdayBars *>());
            QVERIFY(!tab->page()->findChild<RowsWidget *>());
        }
        chipNow(2);
        {
            std::unique_ptr<StatsTab> tab(makeTab());
            QCOMPARE(tab->currentChip(), 2);
            QVERIFY(tab->page()->findChild<NoteWidget *>()); // "Connect to BookOrbit once..."
            QVERIFY(!hasTitle(tab->page(), QStringLiteral("Next badge")));
        }
        // With reading and the dashboard the same screens fill in.
        m_fx->read(kBook, utc(2026, 11, 20, 9, 0), 12, 60);
        m_fx->cacheDashboard(loadFixtureObject(QStringLiteral("dashboard_full.json")),
                             fixedNow().addSecs(-60));
        {
            std::unique_ptr<StatsTab> tab(makeTab());
            QVERIFY(hasTitle(tab->page(), QStringLiteral("Next badge")));
            QVERIFY(hasTitle(tab->page(), QStringLiteral("A year ago")));
            QVERIFY(hasTitle(tab->page(), QStringLiteral("Finished in 2026")));
        }
        chipNow(1);
        {
            std::unique_ptr<StatsTab> tab(makeTab());
            QVERIFY(tab->page()->findChild<HourHistogram *>());
            QVERIFY(tab->page()->findChild<WeekdayBars *>());
            RowsWidget *rows = tab->page()->findChild<RowsWidget *>();
            QVERIFY(rows);
            QCOMPARE(rows->rows().first().first, QStringLiteral("Longest session"));
        }
    }

    // --- Redraw ---

    void redrawsAfterASyncAndNotOtherwise()
    {
        m_fx->cacheDashboard(loadFixtureObject(QStringLiteral("dashboard_full.json")),
                             fixedNow().addSecs(-3600));
        std::unique_ptr<StatsTab> tab(makeTab());
        tab->show();
        QVERIFY(QTest::qWaitForWindowExposed(tab.get()));
        QWidget *before = tab->page();
        QCOMPARE(todayTile(before), QStringLiteral("30 min"));

        // The ledger changes (the recorder wrote a session): nothing happens on screen.
        m_fx->read(kBook, utc(2026, 11, 20, 9, 0), 15, 60);
        QTest::qWait(150);
        QCOMPARE(tab->page(), before);
        QCOMPARE(todayTile(tab->page()), QStringLiteral("30 min"));

        // A sync that failed or was skipped changes nothing either.
        m_sync.finish(SyncOutcome::Failed);
        m_sync.finish(SyncOutcome::Skipped);
        QCOMPARE(tab->page(), before);

        // A sync that got through redraws: 30 on the server plus 15 not confirmed.
        m_sync.finish(SyncOutcome::Succeeded);
        QVERIFY(tab->page() != before);
        QCOMPARE(todayTile(tab->page()), QStringLiteral("45 min"));
    }

    void nothingUpdatesLive()
    {
        std::unique_ptr<StatsTab> tab(makeTab());
        tab->show();
        QVERIFY(QTest::qWaitForWindowExposed(tab.get()));
        QVERIFY(tab->findChildren<QTimer *>().isEmpty()); // No timers anywhere in the tab.
        QWidget *before = tab->page();
        m_fx->read(kBook, utc(2026, 11, 20, 9, 0), 15, 60);
        QTest::qWait(300);
        QCOMPARE(tab->page(), before);
    }

    void aSyncWhileHiddenRedrawsWhenOpened()
    {
        m_fx->cacheDashboard(loadFixtureObject(QStringLiteral("dashboard_full.json")),
                             fixedNow().addSecs(-3600));
        std::unique_ptr<StatsTab> tab(makeTab()); // Never shown yet.
        m_fx->read(kBook, utc(2026, 11, 20, 9, 0), 15, 60);
        m_sync.finish();
        tab->show();
        QVERIFY(QTest::qWaitForWindowExposed(tab.get()));
        QCOMPARE(todayTile(tab->page()), QStringLiteral("45 min"));
    }

    // --- The mock server ---

    void fetchesTheDashboardWhenOpenedAndAfterASyncAndCachesIt()
    {
        m_server = new MockServer;
        QVERIFY2(m_server->start().isEmpty(), "the mock server did not start");
        QJsonObject rule = MockServer::rule(QStringLiteral("/plugin/catalog/dashboard"),
                                            QStringLiteral("GET"));
        rule.insert(QStringLiteral("status"), 200);
        rule.insert(QStringLiteral("body"),
                    loadFixtureObject(QStringLiteral("dashboard_full.json")));
        m_server->setRules(QJsonArray() << rule);
        std::unique_ptr<BookOrbitClient> client = createBookOrbitClient(m_server->clientConfig());

        Dashboard none;
        QVERIFY(!loadCachedDashboard(m_fx->ledger(), &none, nullptr).ok());

        std::unique_ptr<StatsTab> tab(makeTab(client.get()));
        QVERIFY(!tab->page()->findChild<HeatmapWidget *>()); // Nothing cached yet.
        tab->show();
        QVERIFY(QTest::qWaitFor(
                [&] { return tab->page()->findChild<HeatmapWidget *>() != nullptr; }, 10000));
        Dashboard cached;
        QDateTime when;
        QVERIFY(loadCachedDashboard(m_fx->ledger(), &cached, &when).ok());
        QVERIFY(cached.hasReadingYear);
        QCOMPARE(todayTile(tab->page()), QStringLiteral("30 min"));
        QCOMPARE(m_server->requestsTo(QStringLiteral("/plugin/catalog/dashboard")).size(), 1);

        // Reopening with a fresh cache does not fetch again.
        tab->hide();
        tab->show();
        QTest::qWait(300);
        QCOMPARE(m_server->requestsTo(QStringLiteral("/plugin/catalog/dashboard")).size(), 1);

        // A sync that got through fetches again, and the page shows the new day.
        QJsonObject body = loadFixtureObject(QStringLiteral("dashboard_full.json"));
        QJsonObject year = body.value(QStringLiteral("readingYear")).toObject();
        QJsonArray minutes = year.value(QStringLiteral("minutes")).toArray();
        minutes.replace(minutes.size() - 1, 99);
        year.insert(QStringLiteral("minutes"), minutes);
        body.insert(QStringLiteral("readingYear"), year);
        rule.insert(QStringLiteral("body"), body);
        m_server->setRules(QJsonArray() << rule);
        m_sync.finish();
        QVERIFY(QTest::qWaitFor(
                [&] { return todayTile(tab->page()) == QStringLiteral("1 h 39 m"); }, 10000));
        QCOMPARE(m_server->requestsTo(QStringLiteral("/plugin/catalog/dashboard")).size(), 2);

        // The server goes away: the tab keeps showing the cached dashboard.
        m_server->stop();
        m_sync.finish();
        QTest::qWait(300);
        QVERIFY(tab->page()->findChild<HeatmapWidget *>());
        QCOMPARE(todayTile(tab->page()), QStringLiteral("1 h 39 m"));
    }

    void aServerWithoutStatsFieldsLeavesTheAllDevicesWidgetsOut()
    {
        // The mock's own dashboard has no readingYear and the rest: widgets stay hidden.
        m_server = new MockServer;
        QVERIFY2(m_server->start().isEmpty(), "the mock server did not start");
        std::unique_ptr<BookOrbitClient> client = createBookOrbitClient(m_server->clientConfig());
        std::unique_ptr<StatsTab> tab(makeTab(client.get()));
        tab->show();
        QVERIFY(QTest::qWaitFor(
                [&] {
                    return !m_server->requestsTo(QStringLiteral("/plugin/catalog/dashboard"))
                                    .isEmpty();
                },
                10000));
        QTest::qWait(300);
        QVERIFY(!tab->page()->findChild<HeatmapWidget *>());
        QVERIFY(!tab->page()->findChild<GoalWidget *>());
        QVERIFY(tab->page()->findChild<SessionStrip *>());
    }
};

QTEST_MAIN(TstTab)
#include "tst_tab.moc"
