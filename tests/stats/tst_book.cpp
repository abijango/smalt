// Task 23, acceptance tests: "Your reading" on book detail, from a real
// ledger. SPEC: Stats ("Screens": finish forecast, strip of days, tiles, latest
// sessions; "Rules": forecast).
#include "test_support.h"

#include "smalt/ui_kit.h"
#include "stats/snapshot.h"
#include "stats_tab.h"
#include "widgets/blocks.h"
#include "widgets/charts.h"

#include <QtTest>

using namespace smalt;
using namespace smalt::ui;
using namespace smalt::ui::stats;
using namespace statstest;

namespace {

const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/A/B.epub");

} // namespace

class TstBook : public QObject
{
    Q_OBJECT

    LedgerFixture *m_fx = nullptr;
    std::unique_ptr<ComponentFactory> m_kit;
    FakeSync m_sync;
    QWidget m_host; // Never shown: the section is a child, as on book detail.

    StatsOptions options() const
    {
        StatsOptions o;
        o.now = [] { return fixedNow(); };
        o.zone = smalt::stats::fixedZone(0);
        return o;
    }

    StatsDeps deps() const
    {
        StatsDeps d;
        d.kit = m_kit.get();
        d.ledger = m_fx->ledger();
        d.sync = const_cast<FakeSync *>(&m_sync);
        return d;
    }

    // Three 45-minute sessions on the 18th, 19th and 20th: 30 pages each.
    void readThreeDays(double percentNow = 41.0)
    {
        m_fx->read(kBook, utc(2026, 11, 18, 21, 0), 30, 90, 10);
        m_fx->read(kBook, utc(2026, 11, 19, 21, 0), 30, 90, 40);
        m_fx->read(kBook, utc(2026, 11, 20, 8, 0), 30, 90, 70);
        Position p;
        p.percent = percentNow;
        QVERIFY(m_fx->ledger()->setLocalProgress(kBook, p, utc(2026, 11, 20, 8, 46)).ok());
    }

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void init()
    {
        m_fx = new LedgerFixture;
        QVERIFY(m_fx->ok());
        m_kit = createComponentFactory(DeviceModel::ClaraColour, nullptr);
    }

    void cleanup()
    {
        m_kit.reset();
        delete m_fx;
        m_fx = nullptr;
    }

    void aBookNeverReadHasNoSection()
    {
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        QVERIFY(s->isHidden());
        QVERIFY(!s->content());
    }

    void forecastTilesAndSessionsComeFromTheLedger()
    {
        readThreeDays();
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        QVERIFY(!s->isHidden());
        QWidget *c = s->content();
        QVERIFY(c);

        ForecastBox *box = c->findChild<ForecastBox *>();
        QVERIFY(box);
        // 300 pages, 41% read: 177 pages left; 90 pages in 2.25 h = 40 pages an hour; 265.5 minutes
        // left. Median of the three reading days: 45 minutes: 5.9 days, so five days out.
        QCOMPARE(box->headline(), QStringLiteral("About 4 h 25 m left"));
        QCOMPARE(box->detail(),
                 QStringLiteral("Done around Wednesday at your pace of 45 min a day"));

        TilesWidget *tiles = c->findChild<TilesWidget *>();
        QVERIFY(tiles);
        QCOMPARE(tiles->tiles().size(), 4);
        QCOMPARE(tiles->tiles()[0].value, QStringLiteral("2 h 15 m")); // Time read
        QCOMPARE(tiles->tiles()[0].caption, QStringLiteral("Time read"));
        QCOMPARE(tiles->tiles()[1].value, QStringLiteral("3")); // Sessions
        QCOMPARE(tiles->tiles()[2].value, QStringLiteral("40")); // Pages per hour
        QCOMPARE(tiles->tiles()[3].value, QStringLiteral("45 min")); // Average session

        DayStrip *strip = c->findChild<DayStrip *>();
        QVERIFY(strip);
        QCOMPARE(strip->cellCount(), 3); // From the 18th to today.

        RowsWidget *rows = c->findChild<RowsWidget *>();
        QVERIFY(rows);
        QCOMPARE(rows->rows().size(), 3);
        // Newest first.
        QCOMPARE(rows->rows()[0].first, QStringLiteral("Today"));
        QCOMPARE(rows->rows()[0].second, QStringLiteral("08:00–08:45"));
        QCOMPARE(rows->rows()[0].third, QStringLiteral("45 min"));
        QCOMPARE(rows->rows()[1].first, QStringLiteral("Yesterday"));
        QCOMPARE(rows->rows()[2].first, QStringLiteral("18 Nov"));
    }

    void fewerThanThreeSessionsShowsTheFallbackText()
    {
        m_fx->read(kBook, utc(2026, 11, 19, 21, 0), 30, 90, 40);
        m_fx->read(kBook, utc(2026, 11, 20, 8, 0), 30, 90, 70);
        Position p;
        p.percent = 41;
        QVERIFY(m_fx->ledger()->setLocalProgress(kBook, p, utc(2026, 11, 20, 8, 46)).ok());
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        ForecastBox *box = s->content()->findChild<ForecastBox *>();
        QVERIFY(box);
        QCOMPARE(box->headline(), QStringLiteral("Not enough reading yet"));
        QVERIFY(box->detail().isEmpty());
    }

    void aFinishedBookHasNoForecastBox()
    {
        readThreeDays(100.0);
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        QVERIFY(s->content());
        QVERIFY(!s->content()->findChild<ForecastBox *>());
        QVERIFY(s->content()->findChild<TilesWidget *>());
    }

    void onlyTheLatestFiveSessionsAreListed()
    {
        for (int d = 0; d < 7; ++d)
            m_fx->read(kBook, utc(2026, 11, 10 + d, 21, 0), 10, 60, 10 + d * 10);
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        RowsWidget *rows = s->content()->findChild<RowsWidget *>();
        QVERIFY(rows);
        QCOMPARE(rows->rows().size(), 5);
        QCOMPARE(rows->rows()[0].first, QStringLiteral("16 Nov"));
        QCOMPARE(rows->rows()[4].first, QStringLiteral("12 Nov"));
        TilesWidget *tiles = s->content()->findChild<TilesWidget *>();
        QCOMPARE(tiles->tiles()[1].value, QStringLiteral("7")); // All seven are counted.
    }

    void anotherBooksReadingIsNotThisBooks()
    {
        const QString other = QStringLiteral("file:///mnt/onboard/BookOrbit/C/D.epub");
        m_fx->read(other, utc(2026, 11, 19, 21, 0), 30, 90, 10);
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        QVERIFY(s->isHidden());
    }

    void redrawsAfterASync()
    {
        readThreeDays();
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        QWidget *before = s->content();
        m_fx->read(kBook, utc(2026, 11, 20, 10, 0), 10, 60, 100);
        QCOMPARE(s->content(), before); // Nothing happens on its own.
        m_sync.finish();
        QVERIFY(s->content() != before);
        QCOMPARE(s->content()->findChild<TilesWidget *>()->tiles()[1].value, QStringLiteral("4"));
    }

    void aBookWithReadingAppearsAfterASync()
    {
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        QVERIFY(s->isHidden());
        readThreeDays();
        m_sync.finish();
        QVERIFY(!s->isHidden());
        QVERIFY(s->content());
    }

    void theDayStripShowsTheLastDaysThatFitAndSaysSo()
    {
        // 40 days of reading: more squares than fit on a Clara.
        for (int d = 0; d < 40; ++d)
            m_fx->read(kBook, utc(2026, 11, 20, 8, 0).addDays(-d), 4, 30, 10 + d * 5);
        const smalt::stats::BookSnapshot snap =
                smalt::stats::buildBookSnapshot(smalt::stats::loadBookInputs(
                        m_fx->ledger(), kBook, fixedNow(), smalt::stats::fixedZone(0)));
        QVERIFY(snap.hasReading);
        QCOMPARE(snap.stats.sessionCount, 40);
        std::unique_ptr<YourReadingSection> s(
                new YourReadingSection(deps(), kBook, options(), &m_host));
        DayStrip *strip = s->content()->findChild<DayStrip *>();
        QVERIFY(strip);
        const Style style(*m_kit);
        QCOMPARE(strip->cellCount(), DayStrip::maxCells(style));
        QVERIFY(strip->cellCount() < 40);
        bool saidSo = false;
        for (ScopeTitle *t : s->content()->findChildren<ScopeTitle *>())
            if (t->title() == QStringLiteral("Your reading"))
                saidSo = true;
        QVERIFY(saidSo);
    }
};

QTEST_MAIN(TstBook)
#include "tst_book.moc"
