// Task 10, schema version 2: open sessions, page events and the page-stats
// watermark. SPEC: On-device data model ("How sessions are captured", "How
// BookOrbit builds sessions", "Sleeping inside a book", "Correcting a
// session"); Sync engine ("Order of a full sync", "Conflict rules"); Stats
// ("Sources").
#include "core_test_util.h"

#include "smalt/ledger.h"
#include "smalt/outbox.h"
#include "smalt/paths.h"

#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace smalt;
using namespace coretest;

namespace {

const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/A/B.epub");
const QString kOther = QStringLiteral("file:///mnt/onboard/BookOrbit/C/D.epub");
const QString kUnmatched = QStringLiteral("file:///mnt/onboard/Sideloaded/E.epub");

qint64 unixSecs(const QDateTime &t)
{
    return t.toMSecsSinceEpoch() / 1000;
}

PageEventRecord pageEvent(const SessionRecord &s, int page, const QDateTime &start, int seconds)
{
    PageEventRecord e;
    e.sessionUuid = s.uuid;
    e.contentId = s.contentId;
    e.page = page;
    e.totalPages = 310;
    e.startUtc = start;
    e.durationSeconds = seconds;
    return e;
}

BookMapEntry mapEntry(const QString &contentId, qint64 bookId)
{
    BookMapEntry e;
    e.contentId = contentId;
    e.bookId = bookId;
    e.bookFileId = bookId * 10;
    e.createdUtc = utc(2026, 11, 1);
    return e;
}

QStringList uuidsOf(const QVector<SessionRecord> &sessions)
{
    QStringList out;
    for (const SessionRecord &s : sessions)
        out << s.uuid;
    return out;
}

} // namespace

class TstLedgerSessions : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_dir = nullptr;
    std::unique_ptr<Ledger> m_ledger;

    // Begins a session, writes `pages` page events of `seconds` each and
    // finishes it. Returns the finished record.
    SessionRecord readFor(const QString &book, const QDateTime &start, int pages, int seconds,
                          int firstPage = 10)
    {
        SessionRecord s;
        s.contentId = book;
        s.startUtc = start;
        s.startPage = firstPage;
        if (!m_ledger->beginSession(&s).ok())
            return SessionRecord();
        for (int i = 0; i < pages; ++i) {
            if (!m_ledger->appendPageEvent(
                                 pageEvent(s, firstPage + i, start.addSecs(i * seconds), seconds))
                         .ok())
                return SessionRecord();
        }
        s.endUtc = start.addSecs(pages * seconds);
        s.activeSeconds = pages * seconds;
        s.endPage = firstPage + pages - 1;
        s.pagesTurned = pages;
        s.totalPages = 310;
        if (!m_ledger->finishSession(s).ok())
            return SessionRecord();
        SessionRecord out;
        m_ledger->session(s.uuid, &out);
        return out;
    }

private slots:

    void init()
    {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
        m_ledger = createLedger(Paths::forRoot(m_dir->path()));
        Status s = m_ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        // Only linked books are pending; both test books are linked (kUnmatched isn't).
        QVERIFY(m_ledger->putBookMap(mapEntry(kBook, 1)).ok());
        QVERIFY(m_ledger->putBookMap(mapEntry(kOther, 2)).ok());
    }

    void cleanup()
    {
        m_ledger.reset();
        delete m_dir;
        m_dir = nullptr;
    }

    void specValues()
    {
        QCOMPARE(kMinSessionSeconds, 10);
        QCOMPARE(kDefaultNewSessionAfterSleepSeconds, 15 * 60);
    }

    // --- open sessions ---

    void openSessionThroughShortSleep()
    {
        // 21:00 open the book and read pages 40, 41 and 42; 21:05 sleep on
        // page 42; 21:15 wake (within "New session after sleep") and read
        // pages 42 and 43; 21:20 close.
        const QDateTime t0 = utc(2026, 11, 3, 21);
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = t0;
        s.start = Position{ QStringLiteral("OEBPS/ch3.xhtml#a"), 41.0 };
        s.startPage = 40;
        QVERIFY(m_ledger->beginSession(&s).ok());
        QVERIFY(!s.uuid.isEmpty());
        QCOMPARE(s.state, SessionState::Open);

        SessionRecord open;
        QVERIFY(m_ledger->openSession(&open).ok());
        QCOMPARE(open.uuid, s.uuid);
        QCOMPARE(open.endUtc, t0); // No end yet: the start.

        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 40, t0, 120)).ok());
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 41, t0.addSecs(120), 90)).ok());
        // Sleep at 21:05: the page on screen ends at the moment of sleep.
        const QDateTime sleep = t0.addSecs(300);
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 42, t0.addSecs(210), 90)).ok());
        s.endUtc = sleep;
        s.activeSeconds = 300;
        s.end = Position{ QStringLiteral("OEBPS/ch3.xhtml#a"), 41.0 };
        s.approximate = true; // Kobalt hadn't saved the position yet.
        s.endPage = 42;
        s.pagesTurned = 2;
        s.totalPages = 310;
        QVERIFY(m_ledger->updateOpenSession(s).ok());

        QVERIFY(m_ledger->openSession(&open).ok());
        QCOMPARE(open.endUtc, sleep);
        QCOMPARE(open.activeSeconds, qint64(300));
        QVERIFY(open.approximate);
        QCOMPARE(open.state, SessionState::Open);

        // Wake at 21:15: same session, page 42 again, then 43.
        const QDateTime wake = t0.addSecs(900);
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 42, wake, 60)).ok());
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 43, wake.addSecs(60), 240)).ok());
        s.endUtc = wake.addSecs(300);
        s.activeSeconds = 600; // 300 before sleep + 300 after; sleep never counted.
        s.end = Position{ QStringLiteral("OEBPS/ch4.xhtml#b"), 43.5 };
        s.approximate = false;
        s.endPage = 43;
        s.pagesTurned = 4;
        QVERIFY(m_ledger->finishSession(s).ok());

        const QVector<SessionRecord> all = m_ledger->sessionsForBook(kBook);
        QCOMPARE(all.size(), 1);
        const SessionRecord &done = all[0];
        QCOMPARE(done.uuid, s.uuid);
        QCOMPARE(done.state, SessionState::Finished);
        QCOMPARE(done.startUtc, t0);
        QCOMPARE(done.endUtc, wake.addSecs(300));
        QCOMPARE(done.activeSeconds, qint64(600));
        QVERIFY(done.activeSeconds < done.startUtc.secsTo(done.endUtc)); // Sleep excluded.
        QCOMPARE(done.start.percent, 41.0);
        QCOMPARE(done.end.percent, 43.5);
        QCOMPARE(done.startPage, 40);
        QCOMPARE(done.endPage, 43);
        QCOMPARE(done.totalPages, 310);
        QCOMPARE(done.pagesTurned, 4);
        QVERIFY(!done.approximate);
        QVERIFY(!done.confirmed);

        // The page events add up to the active time, oldest first.
        const QVector<PageEventRecord> events = m_ledger->pageEventsAfter(kBook, 0);
        QCOMPARE(events.size(), 5);
        int sum = 0;
        for (const PageEventRecord &e : events) {
            QCOMPARE(e.sessionUuid, s.uuid);
            QCOMPARE(e.totalPages, 310);
            sum += e.durationSeconds;
        }
        QCOMPARE(sum, 600);
        QCOMPARE(events[0].page, 40);
        QCOMPARE(events[0].startUtc, t0);
        QCOMPARE(events[3].page, 42);
        QCOMPARE(events[3].startUtc, wake);

        QCOMPARE(m_ledger->openSession(&open).code, StatusCode::NotFound);
        QCOMPARE(m_ledger->updateOpenSession(s).code, StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->finishSession(s).code, StatusCode::InvalidArgument);
    }

    void beginRules()
    {
        SessionRecord s;
        QCOMPARE(m_ledger->beginSession(&s).code, StatusCode::InvalidArgument); // No book.
        s.contentId = kBook;
        QCOMPARE(m_ledger->beginSession(&s).code, StatusCode::InvalidArgument); // No start.
        QCOMPARE(m_ledger->beginSession(nullptr).code, StatusCode::InvalidArgument);
        s.startUtc = utc(2026, 11, 3, 9);
        s.endUtc = s.startUtc.addSecs(-1);
        QCOMPARE(m_ledger->beginSession(&s).code, StatusCode::InvalidArgument);
        s.endUtc = QDateTime();
        s.uuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
        QVERIFY(m_ledger->beginSession(&s).ok());
        QCOMPARE(s.uuid, QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e"));

        // At most one open session per device.
        SessionRecord second;
        second.contentId = kOther;
        second.startUtc = utc(2026, 11, 3, 10);
        QCOMPARE(m_ledger->beginSession(&second).code, StatusCode::InvalidArgument);
        QVERIFY(second.uuid.isEmpty());
        // Nor can the file hold two.
        QSqlQuery q(ledgerdb::database(m_ledger.get()));
        QVERIFY(!q.exec(QStringLiteral("INSERT INTO session (uuid, content_id, state, start_utc,"
                                       " end_utc, active_seconds, created_utc)"
                                       " VALUES ('x', 'b', 0, 1, 1, 0, 1)")));

        // Duplicate UUID refused, even once the first is finished.
        s.endUtc = s.startUtc.addSecs(60);
        s.activeSeconds = 60;
        QVERIFY(m_ledger->finishSession(s).ok());
        SessionRecord dup;
        dup.uuid = s.uuid;
        dup.contentId = kBook;
        dup.startUtc = utc(2026, 11, 4);
        QCOMPARE(m_ledger->beginSession(&dup).code, StatusCode::InvalidArgument);
        QVERIFY(m_ledger->beginSession(&second).ok()); // The old one is finished now.
    }

    void updateAndFinishRules()
    {
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = utc(2026, 11, 3, 9);
        QVERIFY(m_ledger->beginSession(&s).ok());

        SessionRecord bad = s;
        bad.endUtc = s.startUtc.addSecs(-5);
        QCOMPARE(m_ledger->updateOpenSession(bad).code, StatusCode::InvalidArgument);
        bad = s;
        bad.endUtc = QDateTime();
        QCOMPARE(m_ledger->updateOpenSession(bad).code, StatusCode::InvalidArgument);
        bad = s;
        bad.endUtc = s.startUtc.addSecs(10);
        bad.activeSeconds = -1;
        QCOMPARE(m_ledger->finishSession(bad).code, StatusCode::InvalidArgument);
        bad.activeSeconds = 10;
        bad.contentId = kOther;
        QCOMPARE(m_ledger->finishSession(bad).code, StatusCode::InvalidArgument);
        bad = s;
        bad.uuid = QStringLiteral("unknown");
        bad.endUtc = s.startUtc.addSecs(10);
        QCOMPARE(m_ledger->updateOpenSession(bad).code, StatusCode::NotFound);

        // Still open and untouched.
        SessionRecord got;
        QVERIFY(m_ledger->openSession(&got).ok());
        QCOMPARE(got.endUtc, s.startUtc);
        QCOMPARE(got.activeSeconds, qint64(0));
    }

    void finishUnderTenSecondsIsDroppedAndKept_data()
    {
        QTest::addColumn<int>("seconds");
        QTest::addColumn<bool>("dropped");
        QTest::newRow("0") << 0 << true;
        QTest::newRow("9") << 9 << true;
        QTest::newRow("10") << 10 << false;
    }

    void finishUnderTenSecondsIsDroppedAndKept()
    {
        QFETCH(int, seconds);
        QFETCH(bool, dropped);
        const QDateTime t0 = utc(2026, 11, 3, 8);
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = t0;
        QVERIFY(m_ledger->beginSession(&s).ok());
        if (seconds > 0)
            QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 1, t0, seconds)).ok());
        s.endUtc = t0.addSecs(seconds);
        s.activeSeconds = seconds;
        QVERIFY(m_ledger->finishSession(s).ok());

        SessionRecord got;
        QVERIFY(m_ledger->session(s.uuid, &got).ok());
        QCOMPARE(got.state, dropped ? SessionState::Dropped : SessionState::Finished);
        QCOMPARE(m_ledger->sessionsForBook(kBook).size(), 1); // Kept either way.
        const QVector<SessionRecord> inRange = m_ledger->sessionsInRange(t0, t0.addDays(1));
        QCOMPARE(inRange.size(), dropped ? 0 : 1);
        QCOMPARE(m_ledger->recentSessionsForBook(kBook, 5).size(), dropped ? 0 : 1);

        if (dropped) {
            // Never counted: not waiting for the server, events not sent.
            QVERIFY(m_ledger->unconfirmedSessions().isEmpty());
            QVERIFY(m_ledger->booksWithPendingPageEvents().isEmpty());
            QVERIFY(m_ledger->pageEventsAfter(kBook, 0).isEmpty());
            QCOMPARE(m_ledger->appendPageEvent(pageEvent(s, 2, t0.addSecs(5), 3)).code,
                     StatusCode::InvalidArgument);
            // The page event stays in the file.
            QCOMPARE(countRows(ledgerdb::database(m_ledger.get()),
                               QStringLiteral("SELECT count(*) FROM page_event")),
                     seconds > 0 ? 1 : 0);
            // Dropped stays dropped.
            QSqlQuery q(ledgerdb::database(m_ledger.get()));
            QVERIFY(!q.exec(QStringLiteral("UPDATE session SET state = 1")));
            QVERIFY(!q.exec(QStringLiteral("UPDATE session SET state = 0")));
        } else {
            QCOMPARE(m_ledger->unconfirmedSessions().size(), 1);
        }
    }

    void appendSessionRefusesShortOnes()
    {
        SessionRecord r = makeSession(kBook, utc(2026, 11, 1), 9);
        QCOMPARE(m_ledger->appendSession(&r).code, StatusCode::InvalidArgument);
        r = makeSession(kBook, utc(2026, 11, 1), 10);
        r.estimated = true;
        QVERIFY(m_ledger->appendSession(&r).ok());
        // An estimated session gets page events built from its counters.
        QVERIFY(m_ledger->appendPageEvent(pageEvent(r, 5, r.startUtc, 10)).ok());
        QCOMPARE(m_ledger->unconfirmedSessions().size(), 1);
        QVERIFY(m_ledger->unconfirmedSessions()[0].estimated);
    }

    // --- correcting a session ---

    void correctSessionEndAfterItsEventsAreConfirmed()
    {
        // SPEC example: 21:00 at 41%, sleep at 21:47, the approximate 41%
        // corrected at 22:30 to 54% after the page events were sent.
        QVERIFY(m_ledger->putBookMap(mapEntry(kBook, 1)).ok());
        const QDateTime t0 = utc(2026, 11, 3, 21);
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = t0;
        s.start = Position{ QStringLiteral("ch11"), 41.0 };
        QVERIFY(m_ledger->beginSession(&s).ok());
        for (int i = 0; i < 4; ++i)
            QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 100 + i, t0.addSecs(i * 660), 660))
                            .ok());
        s.endUtc = t0.addSecs(47 * 60);
        s.activeSeconds = 44 * 60;
        s.end = Position{ QStringLiteral("ch11"), 41.0 };
        s.approximate = true;
        QVERIFY(m_ledger->finishSession(s).ok());

        // The page events are acknowledged: the watermark passes them all.
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(t0.addSecs(3 * 660))).ok());
        SessionRecord before;
        QVERIFY(m_ledger->session(s.uuid, &before).ok());
        QVERIFY(before.confirmed);

        QVERIFY(m_ledger->correctSessionEnd(s.uuid, Position{ QStringLiteral("ch12"), 54.0 }, false)
                        .ok());
        SessionRecord after;
        QVERIFY(m_ledger->session(s.uuid, &after).ok());
        QCOMPARE(after.end.locator, QStringLiteral("ch12"));
        QCOMPARE(after.end.percent, 54.0);
        QVERIFY(!after.approximate);
        // No time changes.
        QCOMPARE(after.startUtc, before.startUtc);
        QCOMPARE(after.endUtc, before.endUtc);
        QCOMPARE(after.activeSeconds, before.activeSeconds);
        QCOMPARE(after.start.percent, 41.0);
        QCOMPARE(after.state, SessionState::Finished);
        QVERIFY(after.confirmed);

        // Neither the API nor the file lets a finished session's times move.
        SessionRecord moved = after;
        moved.endUtc = after.endUtc.addSecs(600);
        moved.activeSeconds = 9999;
        QCOMPARE(m_ledger->updateOpenSession(moved).code, StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->finishSession(moved).code, StatusCode::InvalidArgument);
        QSqlQuery q(ledgerdb::database(m_ledger.get()));
        QVERIFY(!q.exec(QStringLiteral("UPDATE session SET end_utc = end_utc + 1")));
        QVERIFY(!q.exec(QStringLiteral("UPDATE session SET active_seconds = 1")));
        QVERIFY(!q.exec(QStringLiteral("UPDATE session SET start_utc = 1")));
        QVERIFY(!q.exec(QStringLiteral("UPDATE session SET pages_turned = 99")));
        QVERIFY(q.exec(QStringLiteral("UPDATE session SET approximate = 1"))); // A mark only.
        QVERIFY(m_ledger->session(s.uuid, &after).ok());
        QCOMPARE(after.endUtc, before.endUtc);

        QCOMPARE(m_ledger->correctSessionEnd(QStringLiteral("nope"), Position(), false).code,
                 StatusCode::NotFound);
        Position nan;
        nan.percent = qQNaN();
        QCOMPARE(m_ledger->correctSessionEnd(s.uuid, nan, false).code, StatusCode::InvalidArgument);
    }

    void correctOpenAndDroppedSessions()
    {
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = utc(2026, 11, 3, 7);
        QVERIFY(m_ledger->beginSession(&s).ok());
        QVERIFY(m_ledger->correctSessionEnd(s.uuid, Position{ QStringLiteral("x"), 5.0 }, true)
                        .ok());
        SessionRecord got;
        QVERIFY(m_ledger->openSession(&got).ok());
        QCOMPARE(got.end.percent, 5.0);
        QVERIFY(got.approximate);
        s.endUtc = s.startUtc.addSecs(3);
        s.activeSeconds = 3;
        QVERIFY(m_ledger->finishSession(s).ok());
        QVERIFY(m_ledger->correctSessionEnd(s.uuid, Position{ QStringLiteral("y"), 6.0 }, false)
                        .ok());
        QVERIFY(m_ledger->session(s.uuid, &got).ok());
        QCOMPARE(got.state, SessionState::Dropped);
        QCOMPARE(got.end.percent, 6.0);
    }

    // --- page events ---

    void pageEventsAreAppendOnly()
    {
        const QDateTime t0 = utc(2026, 11, 3, 12);
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = t0;
        QVERIFY(m_ledger->beginSession(&s).ok());
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 7, t0, 30)).ok());

        QSqlDatabase db = ledgerdb::database(m_ledger.get());
        QSqlQuery q(db);
        QVERIFY(!q.exec(QStringLiteral("UPDATE page_event SET duration_seconds = 99")));
        QVERIFY(!q.exec(QStringLiteral("UPDATE page_event SET page = 8")));
        QVERIFY(!q.exec(QStringLiteral("DELETE FROM page_event")));
        // Nor a page event without a live session behind it.
        QVERIFY(!q.exec(QStringLiteral("INSERT INTO page_event (session_uuid, content_id, page,"
                                       " total_pages, start_unix, duration_seconds, created_utc)"
                                       " VALUES ('ghost', 'b', 1, 1, 1, 1, 1)")));

        // Duplicate (book, page, start second) refused, whatever else differs.
        PageEventRecord dup = pageEvent(s, 7, t0, 45);
        QCOMPARE(m_ledger->appendPageEvent(dup).code, StatusCode::InvalidArgument);
        dup.startUtc = t0.addMSecs(400); // Same second: BookOrbit's key.
        QCOMPARE(m_ledger->appendPageEvent(dup).code, StatusCode::InvalidArgument);
        // Same page at another time, or another page at the same time: fine.
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 7, t0.addSecs(600), 20)).ok());
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 8, t0, 20)).ok());

        // Bad values.
        PageEventRecord bad = pageEvent(s, -1, t0.addSecs(1), 5);
        QCOMPARE(m_ledger->appendPageEvent(bad).code, StatusCode::InvalidArgument);
        bad = pageEvent(s, 9, t0.addSecs(1), -5);
        QCOMPARE(m_ledger->appendPageEvent(bad).code, StatusCode::InvalidArgument);
        bad = pageEvent(s, 9, QDateTime(), 5);
        QCOMPARE(m_ledger->appendPageEvent(bad).code, StatusCode::InvalidArgument);
        bad = pageEvent(s, 9, t0.addSecs(1), 5);
        bad.totalPages = -1;
        QCOMPARE(m_ledger->appendPageEvent(bad).code, StatusCode::InvalidArgument);
        bad = pageEvent(s, 9, t0.addSecs(1), 5);
        bad.sessionUuid = QStringLiteral("unknown");
        QCOMPARE(m_ledger->appendPageEvent(bad).code, StatusCode::InvalidArgument);
        bad = pageEvent(s, 9, t0.addSecs(1), 5);
        bad.contentId = kOther; // Not this session's book.
        QCOMPARE(m_ledger->appendPageEvent(bad).code, StatusCode::InvalidArgument);

        const QVector<PageEventRecord> events = m_ledger->pageEventsAfter(kBook, 0);
        QCOMPARE(events.size(), 3);
        QCOMPARE(events[0].page, 7);
        QCOMPARE(events[0].durationSeconds, 30);
        QCOMPARE(events[1].page, 8);
        QCOMPARE(events[2].startUtc, t0.addSecs(600));
    }

    void pageEventsAfterAndLimit()
    {
        const QDateTime t0 = utc(2026, 11, 3, 12);
        const SessionRecord s = readFor(kBook, t0, 30, 20);
        QVERIFY(!s.uuid.isEmpty());
        QCOMPARE(m_ledger->pageEventsAfter(kBook, 0).size(), 30);
        QCOMPARE(m_ledger->pageEventsAfter(kBook, 0, 0).size(), 30);
        const QVector<PageEventRecord> first = m_ledger->pageEventsAfter(kBook, 0, 20);
        QCOMPARE(first.size(), 20);
        QCOMPARE(first.last().page, 29);
        const QVector<PageEventRecord> rest =
                m_ledger->pageEventsAfter(kBook, unixSecs(first.last().startUtc), 20);
        QCOMPARE(rest.size(), 10);
        QCOMPARE(rest.first().page, 30);
        QVERIFY(m_ledger->pageEventsAfter(kOther, 0).isEmpty());
        QVERIFY(m_ledger->pageEventsAfter(kBook, 0, -1).isEmpty());
    }

    // --- watermark ---

    void watermarkMovesForwardOnlyAndDrivesConfirmed()
    {
        const QDateTime t0 = utc(2026, 11, 3, 18);
        QCOMPARE(m_ledger->setStatsWatermark(kUnmatched, 5).code, StatusCode::NotFound);

        const SessionRecord a = readFor(kBook, t0, 3, 60); // Events at t0, +60, +120.
        const SessionRecord b = readFor(kBook, t0.addSecs(3600), 2, 60); // +3600, +3660.
        const SessionRecord c = readFor(kOther, t0.addSecs(600), 2, 60);
        QVERIFY(!a.uuid.isEmpty() && !b.uuid.isEmpty() && !c.uuid.isEmpty());
        QVERIFY(!a.confirmed && !b.confirmed && !c.confirmed);
        QCOMPARE(uuidsOf(m_ledger->unconfirmedSessions()),
                 QStringList() << a.uuid << c.uuid << b.uuid);
        QCOMPARE(m_ledger->booksWithPendingPageEvents(), QStringList() << kBook << kOther);

        // Part of a's events acknowledged: a is still unconfirmed.
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(t0.addSecs(60))).ok());
        SessionRecord got;
        QVERIFY(m_ledger->session(a.uuid, &got).ok());
        QVERIFY(!got.confirmed);
        QCOMPARE(m_ledger->pageEventsAfter(kBook, unixSecs(t0.addSecs(60))).size(), 3);

        // The watermark passes a's last event: a flips to confirmed.
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(t0.addSecs(120))).ok());
        QVERIFY(m_ledger->session(a.uuid, &got).ok());
        QVERIFY(got.confirmed);
        QCOMPARE(uuidsOf(m_ledger->unconfirmedSessions()), QStringList() << c.uuid << b.uuid);

        // Backwards is ignored, and the file refuses it too.
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(t0)).ok());
        BookMapEntry map;
        QVERIFY(m_ledger->bookMapByContentId(kBook, &map).ok());
        QCOMPARE(map.statsWatermark, unixSecs(t0.addSecs(120)));
        QSqlQuery q(ledgerdb::database(m_ledger.get()));
        QVERIFY(!q.exec(QStringLiteral("UPDATE book_map SET stats_watermark = 0")));
        QCOMPARE(m_ledger->setStatsWatermark(kBook, -1).code, StatusCode::InvalidArgument);

        // Re-linking the book keeps the watermark (no event goes twice).
        BookMapEntry relinked = mapEntry(kBook, 1);
        relinked.bookFileId = 11;
        QVERIFY(m_ledger->putBookMap(relinked).ok());
        QVERIFY(m_ledger->bookMapByContentId(kBook, &map).ok());
        QCOMPARE(map.statsWatermark, unixSecs(t0.addSecs(120)));
        QCOMPARE(map.bookFileId, qint64(11));
        // Unlinked rows keep it as well.
        QVERIFY(m_ledger->unlinkBook(kBook).ok());
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(t0.addSecs(3660))).ok());

        QVERIFY(m_ledger->session(b.uuid, &got).ok());
        QVERIFY(got.confirmed);
        QCOMPARE(m_ledger->booksWithPendingPageEvents(), QStringList() << kOther);
        QVERIFY(m_ledger->setStatsWatermark(kOther, unixSecs(t0.addSecs(660))).ok());
        QVERIFY(m_ledger->booksWithPendingPageEvents().isEmpty());
        QVERIFY(m_ledger->unconfirmedSessions().isEmpty());

        // New reading after the watermark is pending again.
        const SessionRecord d = readFor(kOther, t0.addDays(1), 1, 30);
        QCOMPARE(uuidsOf(m_ledger->unconfirmedSessions()), QStringList() << d.uuid);
        QCOMPARE(m_ledger->booksWithPendingPageEvents(), QStringList() << kOther);
    }

    void openSessionWithEventsIsUnconfirmed()
    {
        // While reading, finished pages are pending (the push while reading
        // sends them) and the open session counts as unconfirmed.
        const QDateTime t0 = utc(2026, 11, 3, 18);
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = t0;
        QVERIFY(m_ledger->beginSession(&s).ok());
        QVERIFY(m_ledger->unconfirmedSessions().isEmpty()); // No events yet.
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 1, t0, 40)).ok());
        QCOMPARE(uuidsOf(m_ledger->unconfirmedSessions()), QStringList() << s.uuid);
        QCOMPARE(m_ledger->unconfirmedSessions()[0].state, SessionState::Open);
        QCOMPARE(m_ledger->booksWithPendingPageEvents(), QStringList() << kBook);
    }

    void onlyLinkedBooksArePending()
    {
        // Reading of a book that isn't matched stays in the ledger but isn't
        // pending; it becomes pending once the book is matched, and stops
        // being pending while the book is unlinked.
        const QDateTime t0 = utc(2026, 11, 3, 18);
        const SessionRecord s = readFor(kUnmatched, t0, 3, 60);
        QVERIFY(!s.uuid.isEmpty());
        QVERIFY(!s.confirmed); // Not on the server: Stats still counts its minutes.
        QVERIFY(m_ledger->booksWithPendingPageEvents().isEmpty());
        QVERIFY(m_ledger->unconfirmedSessions().isEmpty());
        QCOMPARE(m_ledger->pageEventsAfter(kUnmatched, 0).size(), 3); // Kept.

        auto outbox = createOutbox(m_ledger.get());
        QVERIFY(outbox->enqueue(OutboxKind::Position, "pos-u", kUnmatched, "{}").ok());
        QCOMPARE(outbox->pendingCount(), 1); // The position only.
        QCOMPARE(outbox->nextBatch(t0.addDays(1)).size(), 1); // Not held.

        // Matched later: its reading goes out first, the position waits.
        QVERIFY(m_ledger->putBookMap(mapEntry(kUnmatched, 3)).ok());
        QCOMPARE(m_ledger->booksWithPendingPageEvents(), QStringList() << kUnmatched);
        QCOMPARE(uuidsOf(m_ledger->unconfirmedSessions()), QStringList() << s.uuid);
        QCOMPARE(outbox->pendingCount(), 2);
        QVERIFY(outbox->nextBatch(t0.addDays(1)).isEmpty());

        // Unlinked ("Keep, unlinked"): not pending, nothing held, reading kept.
        QVERIFY(m_ledger->unlinkBook(kUnmatched).ok());
        QVERIFY(m_ledger->booksWithPendingPageEvents().isEmpty());
        QVERIFY(m_ledger->unconfirmedSessions().isEmpty());
        QCOMPARE(outbox->pendingCount(), 1);
        QCOMPARE(outbox->nextBatch(t0.addDays(1)).size(), 1);
        QCOMPARE(m_ledger->sessionsForBook(kUnmatched).size(), 1);

        // Linked again: pending again.
        QVERIFY(m_ledger->putBookMap(mapEntry(kUnmatched, 3)).ok());
        QCOMPARE(m_ledger->booksWithPendingPageEvents(), QStringList() << kUnmatched);
    }

    void appendPageEventBackstops()
    {
        const QDateTime t0 = utc(2026, 11, 3, 18);
        // At or below the book's watermark: refused (the server would ignore
        // it, so it would never be counted).
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = t0;
        QVERIFY(m_ledger->beginSession(&s).ok());
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 1, t0, 30)).ok());
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(t0.addSecs(30))).ok());
        QCOMPARE(m_ledger->appendPageEvent(pageEvent(s, 2, t0.addSecs(30), 30)).code,
                 StatusCode::InvalidArgument); // At.
        QCOMPARE(m_ledger->appendPageEvent(pageEvent(s, 2, t0.addSecs(10), 20)).code,
                 StatusCode::InvalidArgument); // Below.
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 2, t0.addSecs(31), 29)).ok()); // Above.

        // Outside a Finished session's [start, end]: refused.
        s.endUtc = t0.addSecs(120);
        s.activeSeconds = 120;
        QVERIFY(m_ledger->finishSession(s).ok());
        QCOMPARE(m_ledger->appendPageEvent(pageEvent(s, 3, t0.addSecs(121), 5)).code,
                 StatusCode::InvalidArgument); // Starts after the end.
        QCOMPARE(m_ledger->appendPageEvent(pageEvent(s, 3, t0.addSecs(100), 60)).code,
                 StatusCode::InvalidArgument); // Runs past the end.
        // The last page, written at close after the finish: fine.
        QVERIFY(m_ledger->appendPageEvent(pageEvent(s, 3, t0.addSecs(60), 60)).ok());
        QCOMPARE(m_ledger->pageEventsAfter(kBook, 0).size(), 3);

        // A Finished session starting after its book's watermark: an event
        // before the session's start is refused too.
        SessionRecord other;
        other.contentId = kOther;
        other.startUtc = t0.addSecs(600);
        QVERIFY(m_ledger->beginSession(&other).ok());
        other.endUtc = other.startUtc.addSecs(60);
        other.activeSeconds = 60;
        QVERIFY(m_ledger->finishSession(other).ok());
        QCOMPARE(m_ledger->appendPageEvent(pageEvent(other, 1, t0.addSecs(599), 1)).code,
                 StatusCode::InvalidArgument);
        QVERIFY(m_ledger->appendPageEvent(pageEvent(other, 1, other.startUtc, 60)).ok());
    }

    void pageEventsInRangeAcrossBooks()
    {
        const QDateTime day = utc(2026, 11, 5, 0);
        // kOther before midnight, running past it; kBook after; a dropped
        // one in between; an unmatched book's reading too.
        const SessionRecord late = readFor(kOther, day.addSecs(-120), 3, 60, 1);
        const SessionRecord a = readFor(kBook, day.addSecs(3600), 2, 60, 1);
        const SessionRecord u = readFor(kUnmatched, day.addSecs(7200), 1, 60, 1);
        SessionRecord dropped;
        dropped.contentId = kBook;
        dropped.startUtc = day.addSecs(5000);
        QVERIFY(m_ledger->beginSession(&dropped).ok());
        QVERIFY(m_ledger->appendPageEvent(pageEvent(dropped, 9, dropped.startUtc, 4)).ok());
        dropped.endUtc = dropped.startUtc.addSecs(4);
        dropped.activeSeconds = 4;
        QVERIFY(m_ledger->finishSession(dropped).ok());
        QVERIFY(!late.uuid.isEmpty() && !a.uuid.isEmpty() && !u.uuid.isEmpty());

        // The kOther event at +0 s (page 3) is the one after midnight.
        QVERIFY(m_ledger->setStatsWatermark(kBook, unixSecs(day.addSecs(3600))).ok());
        QVector<bool> confirmed;
        const QVector<PageEventRecord> events =
                m_ledger->pageEventsInRange(day, day.addDays(1), &confirmed);
        QCOMPARE(events.size(), 4);
        QCOMPARE(confirmed.size(), 4);
        QCOMPARE(events[0].contentId, kOther);
        QCOMPARE(events[0].startUtc, day);
        QVERIFY(!confirmed[0]);
        QCOMPARE(events[1].contentId, kBook);
        QCOMPARE(events[1].startUtc, day.addSecs(3600));
        QVERIFY(confirmed[1]); // At the watermark.
        QCOMPARE(events[2].startUtc, day.addSecs(3660));
        QVERIFY(!confirmed[2]);
        QCOMPARE(events[3].contentId, kUnmatched);
        QVERIFY(!confirmed[3]); // Never sent.

        // Half-open range, no flags wanted, bad ranges.
        QCOMPARE(m_ledger->pageEventsInRange(day.addSecs(-120), day).size(), 2);
        QCOMPARE(m_ledger->pageEventsInRange(day.addSecs(-119), day).size(), 1);
        QVERIFY(m_ledger->pageEventsInRange(day, day).isEmpty());
        QVERIFY(m_ledger->pageEventsInRange(QDateTime(), day).isEmpty());
        QVector<bool> none;
        none << true;
        QVERIFY(m_ledger->pageEventsInRange(day.addDays(1), day, &none).isEmpty());
        QVERIFY(none.isEmpty());
    }

    // --- Stats queries ---

    void sessionsInRangeAndRecentOrderingAndLimits()
    {
        const QDateTime day = utc(2026, 11, 5, 0);
        SessionRecord s1 = makeSession(kBook, day.addSecs(9 * 3600), 600);
        SessionRecord s2 = makeSession(kOther, day.addSecs(7 * 3600), 600);
        SessionRecord s3 = makeSession(kBook, day.addSecs(21 * 3600), 600);
        SessionRecord before = makeSession(kBook, day.addSecs(-1), 600);
        SessionRecord atEnd = makeSession(kBook, day.addDays(1), 600); // Excluded: [from, to).
        for (SessionRecord *r : { &s1, &s2, &s3, &before, &atEnd })
            QVERIFY(m_ledger->appendSession(r).ok());
        // A dropped one inside the range.
        SessionRecord dropped;
        dropped.contentId = kBook;
        dropped.startUtc = day.addSecs(10 * 3600);
        QVERIFY(m_ledger->beginSession(&dropped).ok());
        dropped.endUtc = dropped.startUtc.addSecs(5);
        dropped.activeSeconds = 5;
        QVERIFY(m_ledger->finishSession(dropped).ok());
        // An open one inside the range (today's reading counts).
        SessionRecord open;
        open.contentId = kOther;
        open.startUtc = day.addSecs(22 * 3600);
        QVERIFY(m_ledger->beginSession(&open).ok());

        QCOMPARE(uuidsOf(m_ledger->sessionsInRange(day, day.addDays(1))),
                 QStringList() << s2.uuid << s1.uuid << s3.uuid << open.uuid);
        QCOMPARE(uuidsOf(m_ledger->sessionsInRange(day.addSecs(-1), day.addSecs(8 * 3600))),
                 QStringList() << before.uuid << s2.uuid);
        QVERIFY(m_ledger->sessionsInRange(day, day).isEmpty());
        QVERIFY(m_ledger->sessionsInRange(day.addDays(1), day).isEmpty());
        QVERIFY(m_ledger->sessionsInRange(QDateTime(), day).isEmpty());

        // recentSessionsForBook() leaves out Dropped sessions, like sessionsInRange().
        QCOMPARE(uuidsOf(m_ledger->recentSessionsForBook(kBook, 10)),
                 QStringList() << atEnd.uuid << s3.uuid << s1.uuid << before.uuid);
        QCOMPARE(uuidsOf(m_ledger->recentSessionsForBook(kBook, 2)),
                 QStringList() << atEnd.uuid << s3.uuid);
        QVERIFY(m_ledger->recentSessionsForBook(kBook, 0).isEmpty());
        QCOMPARE(uuidsOf(m_ledger->recentSessionsForBook(kOther, 1)), QStringList() << open.uuid);
        // sessionsForBook keeps everything, the dropped one too.
        QCOMPARE(m_ledger->sessionsForBook(kBook).size(), 5);
    }

    void closedLedger()
    {
        m_ledger->close();
        SessionRecord s;
        s.contentId = kBook;
        s.startUtc = utc(2026, 11, 3);
        QCOMPARE(m_ledger->beginSession(&s).code, StatusCode::Busy);
        QCOMPARE(m_ledger->openSession(&s).code, StatusCode::Busy);
        QCOMPARE(m_ledger->appendPageEvent(pageEvent(s, 1, s.startUtc, 5)).code, StatusCode::Busy);
        QCOMPARE(m_ledger->setStatsWatermark(kBook, 1).code, StatusCode::Busy);
        QVERIFY(m_ledger->booksWithPendingPageEvents().isEmpty());
        QVERIFY(m_ledger->unconfirmedSessions().isEmpty());
        QVERIFY(m_ledger->sessionsInRange(utc(2026, 1, 1), utc(2027, 1, 1)).isEmpty());
    }

    // --- crash safety ---

    void crashMidAppendPageEventAndFinishLosesAndDuplicatesNothing()
    {
        // A child process records a session, then dies twice over: (1) right
        // after a page event and an update were committed, before the caller
        // could note it; (2) in the middle of a write transaction holding more
        // page events and the finish, with pages already spilled to the
        // journal. The parent then opens the ledger like a reboot would.
        const Paths paths = Paths::forRoot(m_dir->path() + "/crash");
        const QDateTime t0 = utc(2026, 11, 3, 21);
        constexpr int kCommitted = 3;
        constexpr int kInFlight = 400;

        const pid_t pid = fork();
        QVERIFY(pid >= 0);
        if (pid == 0) {
            int code = 1;
            {
                auto ledger = createLedger(paths);
                if (ledger->open().ok()) {
                    SessionRecord s;
                    s.uuid = QStringLiteral("11111111-2222-3333-4444-555555555555");
                    s.contentId = kBook;
                    s.startUtc = t0;
                    bool ok = ledger->beginSession(&s).ok();
                    for (int i = 0; i < kCommitted && ok; ++i)
                        ok = ledger->appendPageEvent(pageEvent(s, i, t0.addSecs(i * 30), 30)).ok();
                    s.endUtc = t0.addSecs(kCommitted * 30);
                    s.activeSeconds = kCommitted * 30;
                    ok = ok && ledger->updateOpenSession(s).ok();

                    QSqlDatabase db = ledgerdb::database(ledger.get());
                    QSqlQuery q(db);
                    ok = ok && q.exec("PRAGMA cache_size = 5") && db.transaction();
                    for (int i = kCommitted; i < kCommitted + kInFlight && ok; ++i)
                        ok = ledger->appendPageEvent(pageEvent(s, i, t0.addSecs(i * 30), 30)).ok();
                    s.endUtc = t0.addSecs((kCommitted + kInFlight) * 30);
                    s.activeSeconds = (kCommitted + kInFlight) * 30;
                    ok = ok && ledger->finishSession(s).ok();
                    if (ok)
                        _exit(QFile::exists(paths.ledger() + "-journal") ? 0 : 3);
                    code = 2;
                }
            }
            _exit(code);
        }
        int status = 0;
        QCOMPARE(waitpid(pid, &status, 0), pid);
        QVERIFY(WIFEXITED(status));
        QCOMPARE(WEXITSTATUS(status), 0); // 0: it died with a hot journal on disk.

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QVERIFY(!QFile::exists(paths.ledger() + "-journal"));
        QDir adds(paths.addsDir());
        QVERIFY(adds.entryList(QStringList() << "ledger.sqlite.corrupt-*", QDir::Files).isEmpty());

        // Everything committed is there, nothing of the half-done transaction.
        SessionRecord open;
        QVERIFY(ledger->openSession(&open).ok());
        QCOMPARE(open.uuid, QStringLiteral("11111111-2222-3333-4444-555555555555"));
        QCOMPARE(open.activeSeconds, qint64(kCommitted * 30));
        QCOMPARE(ledger->pageEventsAfter(kBook, 0).size(), kCommitted);

        // The recorder replays: the committed events are refused as
        // duplicates, the lost ones go in once, the finish applies once.
        for (int i = 0; i < kCommitted + kInFlight; ++i) {
            const Status st = ledger->appendPageEvent(pageEvent(open, i, t0.addSecs(i * 30), 30));
            QCOMPARE(st.ok(), i >= kCommitted);
        }
        open.endUtc = t0.addSecs((kCommitted + kInFlight) * 30);
        open.activeSeconds = (kCommitted + kInFlight) * 30;
        QVERIFY(ledger->finishSession(open).ok());
        QCOMPARE(ledger->finishSession(open).code, StatusCode::InvalidArgument);

        const QVector<PageEventRecord> events = ledger->pageEventsAfter(kBook, 0);
        QCOMPARE(events.size(), kCommitted + kInFlight);
        for (int i = 0; i < events.size(); ++i)
            QCOMPARE(events[i].page, i);
        QCOMPARE(ledger->sessionsForBook(kBook).size(), 1);
        SessionRecord done;
        QVERIFY(ledger->session(open.uuid, &done).ok());
        QCOMPARE(done.state, SessionState::Finished);
        QCOMPARE(done.activeSeconds, qint64((kCommitted + kInFlight) * 30));
        QVERIFY(!done.confirmed);
        QVERIFY(ledger->unconfirmedSessions().isEmpty()); // Book not linked in this ledger.
        QVERIFY(ledger->putBookMap(mapEntry(kBook, 1)).ok());
        QCOMPARE(ledger->unconfirmedSessions().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstLedgerSessions)
#include "tst_ledger_sessions.moc"
