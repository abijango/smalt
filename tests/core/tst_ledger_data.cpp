// Task 10: the ledger's tables and their rules. Acceptance: "Sessions under
// 10 seconds are rejected at insert". Also: append-only sessions, UUIDs made
// on the device, tombstones, per-field newest-wins for book state.
// SPEC: On-device data model ("Tables"); Sync engine ("Conflict rules").
#include "core_test_util.h"

#include "smalt/ledger.h"
#include "smalt/outbox.h"
#include "smalt/paths.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace smalt;
using namespace coretest;

namespace {
const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/A/B.epub");
const QRegularExpression
        kUuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
} // namespace

class TstLedgerData : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_dir = nullptr;
    std::unique_ptr<Ledger> m_ledger;

private slots:

    void init()
    {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
        m_ledger = createLedger(Paths::forRoot(m_dir->path()));
        Status s = m_ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
    }

    void cleanup()
    {
        m_ledger.reset();
        delete m_dir;
        m_dir = nullptr;
    }

    // --- session ---

    void sessionsUnderTenSecondsAreRejected_data()
    {
        QTest::addColumn<int>("seconds");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("0") << 0 << false;
        QTest::newRow("9") << 9 << false;
        QTest::newRow("10") << 10 << true;
        QTest::newRow("3600") << 3600 << true;
    }

    void sessionsUnderTenSecondsAreRejected()
    {
        QFETCH(int, seconds);
        QFETCH(bool, accepted);
        SessionRecord r = makeSession(kBook, utc(2026, 11, 1), seconds);
        Status s = m_ledger->appendSession(&r);
        QCOMPARE(s.ok(), accepted);
        if (!accepted) {
            QCOMPARE(s.code, StatusCode::InvalidArgument);
            QVERIFY(r.uuid.isEmpty());
        }
        QCOMPARE(m_ledger->sessionsForBook(kBook).size(), accepted ? 1 : 0);
    }

    void sessionRejectsBadInput()
    {
        SessionRecord noBook = makeSession(QString(), utc(2026, 11, 1), 60);
        QCOMPARE(m_ledger->appendSession(&noBook).code, StatusCode::InvalidArgument);
        SessionRecord backwards = makeSession(kBook, utc(2026, 11, 1), 60);
        backwards.endUtc = backwards.startUtc.addSecs(-1);
        QCOMPARE(m_ledger->appendSession(&backwards).code, StatusCode::InvalidArgument);
        SessionRecord noTime = makeSession(kBook, QDateTime(), 60);
        QCOMPARE(m_ledger->appendSession(&noTime).code, StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->appendSession(nullptr).code, StatusCode::InvalidArgument);
    }

    void sessionRoundTripAndUuid()
    {
        SessionRecord r = makeSession(kBook, utc(2026, 11, 1, 20, 15, 3), 754);
        r.estimated = true;
        r.approximate = true;
        r.startPage = 40;
        r.endPage = 44;
        r.totalPages = 310;
        r.state = SessionState::Open; // Ignored: an appended session is complete.
        QVERIFY(m_ledger->appendSession(&r).ok());
        QVERIFY(kUuid.match(r.uuid).hasMatch());
        QCOMPARE(r.state, SessionState::Finished);

        SessionRecord second = makeSession(kBook, utc(2026, 11, 1, 18), 60);
        QVERIFY(m_ledger->appendSession(&second).ok());
        QVERIFY(second.uuid != r.uuid);

        const QVector<SessionRecord> all = m_ledger->sessionsForBook(kBook);
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].uuid, second.uuid); // Ordered by start time.
        const SessionRecord &got = all[1];
        QCOMPARE(got.uuid, r.uuid);
        QCOMPARE(got.contentId, kBook);
        QCOMPARE(got.startUtc, r.startUtc);
        QCOMPARE(got.endUtc, r.endUtc);
        QCOMPARE(got.startUtc.timeSpec(), Qt::UTC);
        QCOMPARE(got.activeSeconds, qint64(754));
        QCOMPARE(got.start.locator, r.start.locator);
        QCOMPARE(got.start.percent, r.start.percent);
        QCOMPARE(got.end.locator, r.end.locator);
        QCOMPARE(got.end.percent, r.end.percent);
        QCOMPARE(got.pagesTurned, 4);
        QCOMPARE(got.startPage, 40);
        QCOMPARE(got.endPage, 44);
        QCOMPARE(got.totalPages, 310);
        QCOMPARE(got.state, SessionState::Finished);
        QVERIFY(got.estimated);
        QVERIFY(got.approximate);
        QVERIFY(got.confirmed); // No page events, so nothing waits for the server.
        QVERIFY(m_ledger->sessionsForBook(QStringLiteral("other")).isEmpty());

        SessionRecord one;
        QVERIFY(m_ledger->session(r.uuid, &one).ok());
        QCOMPARE(one.uuid, r.uuid);
        QCOMPARE(one.activeSeconds, qint64(754));
        QCOMPARE(m_ledger->session(QStringLiteral("nope"), &one).code, StatusCode::NotFound);
        QCOMPARE(m_ledger->session(r.uuid, nullptr).code, StatusCode::InvalidArgument);
    }

    void callerUuidIsKeptAndDuplicatesRefused()
    {
        SessionRecord r = makeSession(kBook, utc(2026, 11, 1), 60);
        r.uuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
        QVERIFY(m_ledger->appendSession(&r).ok());
        QCOMPARE(r.uuid, QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e"));

        SessionRecord again = makeSession(kBook, utc(2026, 11, 2), 300);
        again.uuid = r.uuid;
        QCOMPARE(m_ledger->appendSession(&again).code, StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->sessionsForBook(kBook).size(), 1);
        QCOMPARE(m_ledger->sessionsForBook(kBook).first().activeSeconds, qint64(60));
    }

    void sessionsAreAppendOnly()
    {
        SessionRecord r = makeSession(kBook, utc(2026, 11, 1), 60);
        QVERIFY(m_ledger->appendSession(&r).ok());
        QSqlDatabase db = ledgerdb::database(m_ledger.get());

        QSqlQuery del(db);
        QVERIFY(!del.exec(QStringLiteral("DELETE FROM session")));
        QCOMPARE(m_ledger->sessionsForBook(kBook).size(), 1);

        QSqlQuery identity(db);
        QVERIFY(!identity.exec(QStringLiteral("UPDATE session SET content_id = 'x'")));
    }

    void sessionsAreNeverDeleted()
    {
        // Sessions of every state stay; the file refuses a delete whatever
        // the caller does.
        SessionRecord done = makeSession(kBook, utc(2026, 11, 1), 60);
        QVERIFY(m_ledger->appendSession(&done).ok());
        SessionRecord open = makeSession(kBook, utc(2026, 11, 2), 0);
        QVERIFY(m_ledger->beginSession(&open).ok());
        QSqlQuery q(ledgerdb::database(m_ledger.get()));
        QVERIFY(!q.exec(QStringLiteral("DELETE FROM session WHERE state = 0")));
        QVERIFY(!q.exec(QStringLiteral("DELETE FROM session WHERE state = 1")));
        QCOMPARE(m_ledger->sessionsForBook(kBook).size(), 2);
    }

    // --- server row ---

    void serverRow()
    {
        QVERIFY(m_ledger->pullCursor().isEmpty());
        QVERIFY(m_ledger->lastPullUtc().isNull());
        QVERIFY(m_ledger->lastSyncUtc().isNull());
        QVERIFY(!m_ledger->sweepOwed());

        QVERIFY(m_ledger->setPullCursor(QStringLiteral("c-17"), utc(2026, 11, 1, 9)).ok());
        QVERIFY(m_ledger->setLastSyncUtc(utc(2026, 11, 1, 9, 1)).ok());
        QVERIFY(m_ledger->setSweepOwed(true).ok());
        QCOMPARE(m_ledger->pullCursor(), QStringLiteral("c-17"));
        QCOMPARE(m_ledger->lastPullUtc(), utc(2026, 11, 1, 9));
        QCOMPARE(m_ledger->lastSyncUtc(), utc(2026, 11, 1, 9, 1));
        QVERIFY(m_ledger->sweepOwed());
        const QString device = m_ledger->deviceId();

        // Survives a close and reopen (USB).
        m_ledger->close();
        QVERIFY(m_ledger->pullCursor().isEmpty()); // Closed: nothing to read.
        QCOMPARE(m_ledger->setSweepOwed(false).code, StatusCode::Busy);
        QVERIFY(m_ledger->open().ok());
        QCOMPARE(m_ledger->pullCursor(), QStringLiteral("c-17"));
        QVERIFY(m_ledger->sweepOwed());
        QCOMPARE(m_ledger->deviceId(), device);

        QVERIFY(m_ledger->setSweepOwed(false).ok());
        QVERIFY(!m_ledger->sweepOwed());
        QCOMPARE(m_ledger->setPullCursor(QStringLiteral("x"), QDateTime()).code,
                 StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->setLastSyncUtc(QDateTime()).code, StatusCode::InvalidArgument);
    }

    // --- progress ---

    void progressLocalAndRemote()
    {
        ProgressRecord p;
        QCOMPARE(m_ledger->progress(kBook, &p).code, StatusCode::NotFound);

        Position local{ QStringLiteral("OEBPS/ch5.xhtml#a"), 41.0 };
        QVERIFY(m_ledger->setLocalProgress(kBook, local, utc(2026, 11, 1, 10)).ok());
        QVERIFY(m_ledger->progress(kBook, &p).ok());
        QCOMPARE(p.contentId, kBook);
        QCOMPARE(p.local.locator, local.locator);
        QCOMPARE(p.local.percent, 41.0);
        QCOMPARE(p.localUpdatedUtc, utc(2026, 11, 1, 10));
        QVERIFY(!p.hasRemote);
        QVERIFY(kUuid.match(p.uuid).hasMatch());
        const QString progressUuid = p.uuid;

        Position remote{ QStringLiteral("/body/DocFragment[12]/body/p[3]/text().0"), 54.0 };
        QVERIFY(m_ledger->setRemoteProgress(kBook, remote, utc(2026, 11, 1, 9), "Phone").ok());
        QVERIFY(m_ledger->progress(kBook, &p).ok());
        QVERIFY(p.hasRemote);
        QCOMPARE(p.remote.locator, remote.locator);
        QCOMPARE(p.remote.percent, 54.0);
        QCOMPARE(p.remoteDevice, QStringLiteral("Phone"));
        QCOMPARE(p.local.percent, 41.0); // Remote never overwrites local.
        QCOMPARE(p.uuid, progressUuid); // Stable per book.

        // An older remote report doesn't replace a newer one.
        Position stale{ QStringLiteral("x"), 20.0 };
        QVERIFY(m_ledger->setRemoteProgress(kBook, stale, utc(2026, 11, 1, 8), "Web").ok());
        QVERIFY(m_ledger->progress(kBook, &p).ok());
        QCOMPARE(p.remote.percent, 54.0);

        QCOMPARE(m_ledger->setLocalProgress(QString(), local, utc(2026, 11, 1)).code,
                 StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->setLocalProgress(kBook, local, QDateTime()).code,
                 StatusCode::InvalidArgument);
    }

    // --- annotation ---

    void annotationUpsertAndTombstone()
    {
        AnnotationRecord a;
        a.contentId = kBook;
        a.anchorStart = QStringLiteral("/body/p[1]/text().0");
        a.anchorEnd = QStringLiteral("/body/p[1]/text().20");
        a.text = QStringLiteral("highlighted");
        a.context = QStringLiteral("some highlighted text");
        a.colour = QStringLiteral("yellow");
        a.createdUtc = utc(2026, 11, 1, 10);
        a.updatedUtc = utc(2026, 11, 1, 10);
        QVERIFY(m_ledger->upsertAnnotation(&a).ok());
        QVERIFY(kUuid.match(a.uuid).hasMatch());

        a.note = QStringLiteral("a note");
        a.remoteId = QStringLiteral("r-17");
        a.updatedUtc = utc(2026, 11, 1, 11);
        QVERIFY(m_ledger->upsertAnnotation(&a).ok());

        QVector<AnnotationRecord> all = m_ledger->annotationsForBook(kBook);
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].note, QStringLiteral("a note"));
        QCOMPARE(all[0].remoteId, QStringLiteral("r-17"));
        QCOMPARE(all[0].createdUtc, utc(2026, 11, 1, 10));
        QVERIFY(all[0].deletedUtc.isNull());

        // Older edit loses (newest edit wins).
        AnnotationRecord old = all[0];
        old.note = QStringLiteral("older");
        old.remoteId.clear(); // An empty remote ID never clears the known one.
        old.updatedUtc = utc(2026, 11, 1, 10, 30);
        QVERIFY(m_ledger->upsertAnnotation(&old).ok());
        QCOMPARE(m_ledger->annotationsForBook(kBook)[0].note, QStringLiteral("a note"));
        QCOMPARE(m_ledger->annotationsForBook(kBook)[0].remoteId, QStringLiteral("r-17"));

        // Deleted: kept as a tombstone.
        QVERIFY(m_ledger->markAnnotationDeleted(a.uuid, utc(2026, 11, 1, 12)).ok());
        all = m_ledger->annotationsForBook(kBook);
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].deletedUtc, utc(2026, 11, 1, 12));

        // Deletion wins over an edit made before it...
        AnnotationRecord before = all[0];
        before.deletedUtc = QDateTime();
        before.note = QStringLiteral("edited before delete");
        before.updatedUtc = utc(2026, 11, 1, 11, 30);
        QVERIFY(m_ledger->upsertAnnotation(&before).ok());
        QCOMPARE(m_ledger->annotationsForBook(kBook)[0].deletedUtc, utc(2026, 11, 1, 12));
        QCOMPARE(m_ledger->annotationsForBook(kBook)[0].note, QStringLiteral("a note"));

        // ...but not over one made after it.
        AnnotationRecord after = before;
        after.note = QStringLiteral("edited after delete");
        after.updatedUtc = utc(2026, 11, 1, 13);
        QVERIFY(m_ledger->upsertAnnotation(&after).ok());
        QVERIFY(m_ledger->annotationsForBook(kBook)[0].deletedUtc.isNull());
        QCOMPARE(m_ledger->annotationsForBook(kBook)[0].note,
                 QStringLiteral("edited after delete"));

        QCOMPARE(m_ledger->markAnnotationDeleted(QStringLiteral("missing"), utc(2026, 11, 2)).code,
                 StatusCode::NotFound);
        AnnotationRecord noBook;
        QCOMPARE(m_ledger->upsertAnnotation(&noBook).code, StatusCode::InvalidArgument);
    }

    // --- book_state ---

    void bookStateNewestWinsPerField()
    {
        BookStateRecord st;
        QCOMPARE(m_ledger->bookState(kBook, &st).code, StatusCode::NotFound);
        QCOMPARE(st.status, ReadStatus::Unknown);

        QVERIFY(m_ledger->setLocalStatus(kBook, ReadStatus::Reading, utc(2026, 11, 1, 10)).ok());
        QVERIFY(m_ledger->setLocalRating(kBook, 3, utc(2026, 11, 1, 14)).ok());

        // Remote: status newer (applies), rating older (doesn't).
        BookStateRecord remote;
        remote.contentId = kBook;
        remote.status = ReadStatus::Read;
        remote.statusRemoteUpdatedUtc = utc(2026, 11, 1, 12);
        remote.rating = 5;
        remote.ratingRemoteUpdatedUtc = utc(2026, 11, 1, 13);
        QVERIFY(m_ledger->mergeRemoteState(remote).ok());

        QVERIFY(m_ledger->bookState(kBook, &st).ok());
        QCOMPARE(st.status, ReadStatus::Read);
        QCOMPARE(st.statusLocalUpdatedUtc, utc(2026, 11, 1, 10));
        QCOMPARE(st.statusRemoteUpdatedUtc, utc(2026, 11, 1, 12));
        QCOMPARE(st.rating, 3);
        QCOMPARE(st.ratingLocalUpdatedUtc, utc(2026, 11, 1, 14));
        QVERIFY(st.ratingRemoteUpdatedUtc.isNull());

        // A remote status older than the one already applied loses too.
        remote.status = ReadStatus::Abandoned;
        remote.statusRemoteUpdatedUtc = utc(2026, 11, 1, 11);
        QVERIFY(m_ledger->mergeRemoteState(remote).ok());
        QVERIFY(m_ledger->bookState(kBook, &st).ok());
        QCOMPARE(st.status, ReadStatus::Read);

        // Equal times: the local value stays.
        remote.status = ReadStatus::Abandoned;
        remote.statusRemoteUpdatedUtc = utc(2026, 11, 1, 12);
        remote.rating = 1;
        remote.ratingRemoteUpdatedUtc = utc(2026, 11, 1, 14);
        QVERIFY(m_ledger->mergeRemoteState(remote).ok());
        QVERIFY(m_ledger->bookState(kBook, &st).ok());
        QCOMPARE(st.status, ReadStatus::Read);
        QCOMPARE(st.rating, 3);

        // A newer local change wins again.
        QVERIFY(m_ledger->setLocalStatus(kBook, ReadStatus::Abandoned, utc(2026, 11, 2)).ok());
        QVERIFY(m_ledger->bookState(kBook, &st).ok());
        QCOMPARE(st.status, ReadStatus::Abandoned);

        // Remote state for a book with no row yet makes one.
        BookStateRecord fresh;
        fresh.contentId = QStringLiteral("book-new");
        fresh.status = ReadStatus::Reading;
        fresh.statusRemoteUpdatedUtc = utc(2026, 11, 3);
        QVERIFY(m_ledger->mergeRemoteState(fresh).ok());
        QVERIFY(m_ledger->bookState(QStringLiteral("book-new"), &st).ok());
        QCOMPARE(st.status, ReadStatus::Reading);

        QCOMPARE(m_ledger->setLocalRating(kBook, 6, utc(2026, 11, 2)).code,
                 StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->setLocalRating(kBook, -1, utc(2026, 11, 2)).code,
                 StatusCode::InvalidArgument);
    }

    void allSixStatusesRoundTripWithStableUuid()
    {
        const QVector<ReadStatus> all = QVector<ReadStatus>()
                << ReadStatus::Unread << ReadStatus::WantToRead << ReadStatus::Reading
                << ReadStatus::OnHold << ReadStatus::Read << ReadStatus::Abandoned
                << ReadStatus::Unknown;
        QString uuid;
        QDateTime at = utc(2026, 11, 1);
        for (ReadStatus status : all) {
            at = at.addSecs(60);
            QVERIFY(m_ledger->setLocalStatus(kBook, status, at).ok());
            BookStateRecord st;
            QVERIFY(m_ledger->bookState(kBook, &st).ok());
            QCOMPARE(st.status, status);
            QVERIFY(kUuid.match(st.uuid).hasMatch());
            if (uuid.isEmpty())
                uuid = st.uuid;
            QCOMPARE(st.uuid, uuid); // The outbox entityUuid for Status items.
        }
        // The file refuses a status outside BookOrbit's six (plus unknown).
        QSqlQuery q(ledgerdb::database(m_ledger.get()));
        QVERIFY(!q.exec(QStringLiteral("UPDATE book_state SET status = 7")));
        QCOMPARE(m_ledger->setLocalStatus(kBook, ReadStatus(42), at).code,
                 StatusCode::InvalidArgument);
    }

    // --- book_map ---

    void bookMapAndUnlink()
    {
        BookMapEntry e;
        e.contentId = kBook;
        e.bookId = 42;
        e.bookFileId = 420;
        e.partialMd5 = QStringLiteral("0123456789abcdef0123456789abcdef");
        e.origin = BookOrigin::Matched;
        e.createdUtc = utc(2026, 11, 1);
        QVERIFY(m_ledger->putBookMap(e).ok());

        BookMapEntry got;
        QVERIFY(m_ledger->bookMapByContentId(kBook, &got).ok());
        QCOMPARE(got.bookId, qint64(42));
        QCOMPARE(got.bookFileId, qint64(420));
        QCOMPARE(got.partialMd5, e.partialMd5);
        QCOMPARE(got.origin, BookOrigin::Matched);
        QCOMPARE(got.createdUtc, e.createdUtc);
        QVERIFY(m_ledger->bookMapByBookId(42, &got).ok());
        QCOMPARE(got.contentId, kBook);
        QCOMPARE(m_ledger->bookMapByBookId(43, &got).code, StatusCode::NotFound);

        QVERIFY(m_ledger->unlinkBook(kBook).ok());
        QCOMPARE(m_ledger->bookMapByContentId(kBook, &got).code, StatusCode::NotFound);
        QCOMPARE(m_ledger->bookMapByBookId(42, &got).code, StatusCode::NotFound);
        QCOMPARE(m_ledger->unlinkBook(kBook).code, StatusCode::NotFound);

        // Linking again works.
        e.origin = BookOrigin::Downloaded;
        QVERIFY(m_ledger->putBookMap(e).ok());
        QVERIFY(m_ledger->bookMapByContentId(kBook, &got).ok());
        QCOMPARE(got.origin, BookOrigin::Downloaded);

        BookMapEntry bad;
        QCOMPARE(m_ledger->putBookMap(bad).code, StatusCode::InvalidArgument);
    }

    // --- pending_decision ---

    void pendingDecisions()
    {
        PendingDecision d;
        d.contentId = kBook;
        d.reason = PendingReason::LeftCollection;
        d.createdUtc = utc(2026, 11, 1);
        QVERIFY(m_ledger->addPendingDecision(d).ok());
        QVERIFY(m_ledger->addPendingDecision(d).ok()); // No duplicate.
        PendingDecision e;
        e.contentId = QStringLiteral("book-2");
        e.reason = PendingReason::DeletedUpstream;
        e.createdUtc = utc(2026, 11, 2);
        QVERIFY(m_ledger->addPendingDecision(e).ok());

        QVector<PendingDecision> all = m_ledger->pendingDecisions();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].contentId, kBook);
        QCOMPARE(all[0].reason, PendingReason::LeftCollection);
        QCOMPARE(all[0].choice, PendingChoice::Undecided);

        QVERIFY(m_ledger->resolvePendingDecision(kBook, PendingChoice::DecideLater).ok());
        all = m_ledger->pendingDecisions();
        QCOMPARE(all.size(), 2); // "Decide later" stays waiting.
        QCOMPARE(all[0].choice, PendingChoice::DecideLater);

        QVERIFY(m_ledger->resolvePendingDecision(kBook, PendingChoice::KeepUnlinked).ok());
        all = m_ledger->pendingDecisions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].contentId, QStringLiteral("book-2"));

        QCOMPARE(m_ledger->resolvePendingDecision(QStringLiteral("none"),
                                                  PendingChoice::RemoveFromKobo)
                         .code,
                 StatusCode::NotFound);
    }

    void closedLedgerSaysBusy()
    {
        m_ledger->close();
        ProgressRecord p;
        QCOMPARE(m_ledger->progress(kBook, &p).code, StatusCode::Busy);
        QCOMPARE(m_ledger->setLocalStatus(kBook, ReadStatus::Reading, utc(2026, 11, 1)).code,
                 StatusCode::Busy);
        QVERIFY(m_ledger->pendingDecisions().isEmpty());
        QVERIFY(m_ledger->annotationsForBook(kBook).isEmpty());
        QVERIFY(!m_ledger->deviceId().isEmpty()); // Known from the last open.
    }
};

QTEST_GUILESS_MAIN(TstLedgerData)
#include "tst_ledger_data.moc"
