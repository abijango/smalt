// Task 10: the outbox. Acceptance: "Retried uploads never produce duplicate
// sessions (same UUID)" (reading now goes by page events and the watermark),
// "Outbox survives a simulated crash mid-batch with nothing lost". SPEC:
// On-device data model ("Tables": outbox); Sync engine ("Order of a full
// sync", "Conflict rules", "Networking": backoff).
#include "core_test_util.h"

#include "smalt/ledger.h"
#include "smalt/outbox.h"
#include "smalt/paths.h"

#include <QFile>
#include <QSet>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace smalt;
using namespace coretest;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 17), QTime(9, 0), Qt::UTC);

QVector<qint64> idsOf(const QVector<OutboxItem> &items)
{
    QVector<qint64> ids;
    for (const OutboxItem &i : items)
        ids << i.id;
    return ids;
}

QStringList uuidsOf(const QVector<OutboxItem> &items)
{
    QStringList out;
    for (const OutboxItem &i : items)
        out << i.entityUuid;
    return out;
}

} // namespace

class TstOutbox : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_dir = nullptr;
    std::unique_ptr<Ledger> m_ledger;
    std::unique_ptr<Outbox> m_outbox;

    // A finished session of `pages` page events of 30 s on `book`.
    QString readBook(const QString &book, const QDateTime &start, int pages)
    {
        SessionRecord r;
        r.contentId = book;
        r.startUtc = start;
        if (!m_ledger->beginSession(&r).ok())
            return QString();
        for (int i = 0; i < pages; ++i) {
            PageEventRecord e;
            e.sessionUuid = r.uuid;
            e.contentId = book;
            e.page = i;
            e.totalPages = 100;
            e.startUtc = start.addSecs(i * 30);
            e.durationSeconds = 30;
            if (!m_ledger->appendPageEvent(e).ok())
                return QString();
        }
        r.endUtc = start.addSecs(pages * 30);
        r.activeSeconds = pages * 30;
        if (!m_ledger->finishSession(r).ok())
            return QString();
        return r.uuid;
    }

    void mapBook(const QString &book, qint64 bookId)
    {
        BookMapEntry e;
        e.contentId = book;
        e.bookId = bookId;
        QVERIFY(m_ledger->putBookMap(e).ok());
    }

private slots:

    void init()
    {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
        m_ledger = createLedger(Paths::forRoot(m_dir->path()));
        QVERIFY(m_ledger->open().ok());
        m_outbox = createOutbox(m_ledger.get());
        QVERIFY(m_outbox);
    }

    void cleanup()
    {
        m_outbox.reset();
        m_ledger.reset();
        delete m_dir;
        m_dir = nullptr;
    }

    void specValues()
    {
        QCOMPARE(kMinSessionSeconds, 10);
        QCOMPARE(kOutboxBatchSize, 20);
        QCOMPARE(kOutboxMaxBackoffSeconds, 6 * 3600);
        QCOMPARE(int(OutboxKind::Position), 1); // 0 was Session in schema version 1.
        QVERIFY(int(OutboxKind::Position) < int(OutboxKind::Annotation));
        QVERIFY(int(OutboxKind::Annotation) < int(OutboxKind::Status));
    }

    void kindOrderThenOldestFirstInBatchesOf20()
    {
        // Queued in the "wrong" order on purpose.
        QVERIFY(m_outbox->enqueue(OutboxKind::Status, "st-1", "b1", "{\"s\":1}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, "an-1", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-1", "b1", "{}").ok());
        QStringList positions;
        positions << "pos-1";
        for (int i = 0; i < 24; ++i) {
            positions << QStringLiteral("pos-x%1").arg(i, 2, 10, QLatin1Char('0'));
            QVERIFY(m_outbox->enqueue(OutboxKind::Position, positions.last(), "b1", "{}").ok());
        }
        QCOMPARE(m_outbox->pendingCount(), 27);

        QVector<OutboxItem> batch = m_outbox->nextBatch(kNow);
        QCOMPARE(batch.size(), kOutboxBatchSize);
        QCOMPARE(uuidsOf(batch), positions.mid(0, 20));
        for (const OutboxItem &item : batch) {
            QCOMPARE(item.kind, OutboxKind::Position);
            QCOMPARE(item.contentId, QStringLiteral("b1"));
            QCOMPARE(item.payload, QByteArray("{}"));
            QVERIFY(item.createdUtc.isValid());
            QVERIFY(item.nextRetryUtc.isNull());
        }
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());

        batch = m_outbox->nextBatch(kNow);
        QCOMPARE(uuidsOf(batch), positions.mid(20));
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());
        batch = m_outbox->nextBatch(kNow);
        QCOMPARE(uuidsOf(batch), QStringList() << "an-1");
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());
        batch = m_outbox->nextBatch(kNow);
        QCOMPARE(uuidsOf(batch), QStringList() << "st-1");
        QCOMPARE(batch[0].payload, QByteArray("{\"s\":1}"));
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());

        QVERIFY(m_outbox->nextBatch(kNow).isEmpty());
        QCOMPARE(m_outbox->pendingCount(), 0);
    }

    void maxAndBookFilter()
    {
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "a", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "b", "b2", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, "c", "b2", "{}").ok());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow, 1)), QStringList() << "a");
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow, kOutboxBatchSize, "b2")), QStringList() << "b");
        QVERIFY(m_outbox->nextBatch(kNow, 0).isEmpty());
        QVERIFY(m_outbox->nextBatch(kNow, kOutboxBatchSize, "b3").isEmpty());
    }

    void reEnqueueReplacesPayload()
    {
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{\"p\":10}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, "x", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{\"p\":20}").ok());
        QCOMPARE(m_outbox->pendingCount(), 2);
        const QVector<OutboxItem> batch = m_outbox->nextBatch(kNow);
        QCOMPARE(batch.size(), 1);
        QCOMPARE(batch[0].payload, QByteArray("{\"p\":20}"));
    }

    void changeWhileInFlightIsNotLost()
    {
        // Batch handed out, then the item changes before the server answers:
        // confirming the old payload must not drop the new one.
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{\"p\":10}").ok());
        const QVector<OutboxItem> sent = m_outbox->nextBatch(kNow);
        QCOMPARE(sent.size(), 1);
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{\"p\":30}").ok());
        QVERIFY(m_outbox->markConfirmed(idsOf(sent)).ok());
        QCOMPARE(m_outbox->pendingCount(), 1);
        const QVector<OutboxItem> again = m_outbox->nextBatch(kNow);
        QCOMPARE(again.size(), 1);
        QCOMPARE(again[0].payload, QByteArray("{\"p\":30}"));
        QVERIFY(m_outbox->markConfirmed(idsOf(again)).ok());
        QCOMPARE(m_outbox->pendingCount(), 0);
    }

    void confirmingSomethingNeverSentKeepsIt()
    {
        QVERIFY(m_outbox->enqueue(OutboxKind::Status, "st", "b1", "{}").ok());
        QSqlQuery q(ledgerdb::database(m_ledger.get()));
        QVERIFY(q.exec("SELECT id FROM outbox") && q.next());
        QVERIFY(m_outbox->markConfirmed(QVector<qint64>() << q.value(0).toLongLong()).ok());
        QCOMPARE(m_outbox->pendingCount(), 1);
        QVERIFY(m_outbox->markConfirmed(QVector<qint64>() << 9999).ok()); // Unknown: no-op.
    }

    void exponentialBackoffCappedAtSixHours()
    {
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "s", "b1", "{}").ok());
        QDateTime now = kNow;
        qint64 previousDelay = 0;
        for (int attempt = 1; attempt <= 16; ++attempt) {
            QVector<OutboxItem> batch = m_outbox->nextBatch(now);
            QCOMPARE(batch.size(), 1);
            QCOMPARE(batch[0].attempts, attempt - 1);
            QVERIFY(m_outbox->markFailed(idsOf(batch), now).ok());

            // Not ready until the backoff has passed.
            QVERIFY(m_outbox->nextBatch(now).isEmpty());
            QVERIFY(m_outbox->nextBatch(now.addSecs(1)).isEmpty() || attempt == 0);
            QSqlQuery q(ledgerdb::database(m_ledger.get()));
            QVERIFY(q.exec("SELECT next_retry_utc, attempts FROM outbox") && q.next());
            const QDateTime retry = ledgerdb::fromDb(q.value(0));
            QCOMPARE(q.value(1).toInt(), attempt);
            const qint64 delay = now.secsTo(retry);
            QVERIFY(delay > 0);
            QVERIFY(delay <= kOutboxMaxBackoffSeconds);
            if (attempt == 1)
                QCOMPARE(delay, qint64(ledgerdb::kOutboxFirstBackoffSeconds));
            else if (previousDelay * 2 <= kOutboxMaxBackoffSeconds)
                QCOMPARE(delay, previousDelay * 2);
            else
                QCOMPARE(delay, qint64(kOutboxMaxBackoffSeconds));
            QVERIFY(m_outbox->nextBatch(retry.addSecs(-1)).isEmpty());
            previousDelay = delay;
            now = retry;
        }
        QCOMPARE(previousDelay, qint64(kOutboxMaxBackoffSeconds));
        QCOMPARE(m_outbox->pendingCount(), 1); // Never dropped, however often it fails.
    }

    void backingOffItemDoesNotHoldBackLaterKinds()
    {
        // Literal interface rule: the lowest kind that has *ready* items.
        // (See the spec question in the task 10 report.)
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "s", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, "p", "b2", "{}").ok());
        QVERIFY(m_outbox->markFailed(idsOf(m_outbox->nextBatch(kNow)), kNow).ok());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "p");
    }

    void retriedUploadsNeverDuplicateReading()
    {
        // Reading goes by page events and the book's watermark: a retried or
        // partial upload sends the same events again (BookOrbit ignores the
        // ones it has), and nothing already acknowledged goes twice.
        mapBook("b1", 1);
        const QDateTime start = kNow.addSecs(-3600);
        const QString uuid = readBook("b1", start, 4);
        QVERIFY(!uuid.isEmpty());
        QCOMPARE(m_outbox->pendingCount(), 1); // One unconfirmed session.

        // First upload times out: the next try reads the same events.
        BookMapEntry map;
        QVERIFY(m_ledger->bookMapByContentId("b1", &map).ok());
        const QVector<PageEventRecord> first = m_ledger->pageEventsAfter("b1", map.statsWatermark);
        QCOMPARE(first.size(), 4);
        QCOMPARE(m_ledger->pageEventsAfter("b1", map.statsWatermark).size(), 4);

        // The recorder replays the same events (crash-replayed hook): refused.
        for (const PageEventRecord &e : first)
            QCOMPARE(m_ledger->appendPageEvent(e).code, StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->pageEventsAfter("b1", 0).size(), 4);

        // A partial acknowledgement (the server's watermark is the newest
        // start time in the batch it took): only the rest goes next.
        const qint64 secondStart = first[1].startUtc.toMSecsSinceEpoch() / 1000;
        QVERIFY(m_ledger->setStatsWatermark("b1", secondStart).ok());
        QCOMPARE(m_ledger->pageEventsAfter("b1", secondStart).size(), 2);
        QCOMPARE(m_outbox->pendingCount(), 1);

        // Everything acknowledged: nothing left, the session confirmed, one row.
        const qint64 last = first.last().startUtc.toMSecsSinceEpoch() / 1000;
        QVERIFY(m_ledger->setStatsWatermark("b1", last).ok());
        QVERIFY(m_ledger->pageEventsAfter("b1", last).isEmpty());
        QVERIFY(m_ledger->booksWithPendingPageEvents().isEmpty());
        QCOMPARE(m_outbox->pendingCount(), 0);
        QCOMPARE(m_ledger->sessionsForBook("b1").size(), 1);
        SessionRecord got;
        QVERIFY(m_ledger->session(uuid, &got).ok());
        QVERIFY(got.confirmed);
    }

    void positionHeldWhileItsBookHasPendingPageEvents()
    {
        mapBook("b1", 1);
        mapBook("b2", 2);
        const QString read = readBook("b1", kNow.addSecs(-3600), 3);
        QVERIFY(!read.isEmpty());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b2", "b2", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, "an-b1", "b1", "{}").ok());
        QCOMPARE(m_outbox->pendingCount(), 4); // Three items and one unconfirmed session.

        // b1 has unsent reading: its position waits; b2's goes.
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "pos-b2");
        // For b1 alone, the next kind comes out instead (highlights don't wait).
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow, kOutboxBatchSize, "b1")),
                 QStringList() << "an-b1");

        // Part of b1's events acknowledged: still held.
        const QVector<PageEventRecord> events = m_ledger->pageEventsAfter("b1", 0);
        QCOMPARE(events.size(), 3);
        QVERIFY(m_ledger->setStatsWatermark("b1", events[1].startUtc.toMSecsSinceEpoch() / 1000)
                        .ok());
        QVERIFY(!uuidsOf(m_outbox->nextBatch(kNow)).contains("pos-b1"));

        // The watermark passes them all: the position is released, oldest first.
        QVERIFY(m_ledger->setStatsWatermark("b1", events[2].startUtc.toMSecsSinceEpoch() / 1000)
                        .ok());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "pos-b1" << "pos-b2");
        QCOMPARE(m_outbox->pendingCount(), 3);

        // New reading in b1 holds it again; a dropped session never does.
        const QString more = readBook("b1", kNow.addSecs(-600), 1);
        QVERIFY(!more.isEmpty());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "pos-b2");
        QVERIFY(m_ledger->setStatsWatermark("b1", (kNow.toMSecsSinceEpoch() / 1000) - 600).ok());
        SessionRecord shortOne;
        shortOne.contentId = "b1";
        shortOne.startUtc = kNow.addSecs(-60);
        QVERIFY(m_ledger->beginSession(&shortOne).ok());
        PageEventRecord e;
        e.sessionUuid = shortOne.uuid;
        e.contentId = "b1";
        e.page = 50;
        e.startUtc = shortOne.startUtc;
        e.durationSeconds = 4;
        QVERIFY(m_ledger->appendPageEvent(e).ok());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "pos-b2"); // Open: held.
        shortOne.endUtc = shortOne.startUtc.addSecs(4);
        shortOne.activeSeconds = 4;
        QVERIFY(m_ledger->finishSession(shortOne).ok()); // Dropped.
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "pos-b1" << "pos-b2");
        QCOMPARE(m_outbox->pendingCount(), 3);
    }

    void confirmedTombstoneIsRemoved()
    {
        AnnotationRecord a;
        a.contentId = "b1";
        a.text = "x";
        a.updatedUtc = kNow;
        QVERIFY(m_ledger->upsertAnnotation(&a).ok());
        AnnotationRecord live;
        live.contentId = "b1";
        live.text = "y";
        live.updatedUtc = kNow;
        QVERIFY(m_ledger->upsertAnnotation(&live).ok());
        QVERIFY(m_ledger->markAnnotationDeleted(a.uuid, kNow.addSecs(60)).ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, a.uuid, "b1", "{\"deleted\":1}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, live.uuid, "b1", "{}").ok());

        QVector<OutboxItem> batch = m_outbox->nextBatch(kNow);
        QCOMPARE(batch.size(), 2);
        QVERIFY(m_outbox->markFailed(idsOf(batch), kNow).ok());
        QCOMPARE(m_ledger->annotationsForBook("b1").size(), 2); // Not confirmed: kept.

        batch = m_outbox->nextBatch(kNow.addDays(1));
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());
        const QVector<AnnotationRecord> left = m_ledger->annotationsForBook("b1");
        QCOMPARE(left.size(), 1);
        QCOMPARE(left[0].uuid, live.uuid);
    }

    void confirmedEarlierEditKeepsTombstone()
    {
        // An edit is in flight; the user deletes the highlight; the edit is
        // confirmed before the deletion is queued. The tombstone must stay so
        // the deletion still reaches the server.
        AnnotationRecord a;
        a.contentId = "b1";
        a.text = "x";
        a.updatedUtc = kNow;
        QVERIFY(m_ledger->upsertAnnotation(&a).ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, a.uuid, "b1", "{\"edit\":1}").ok());
        const QVector<OutboxItem> edit = m_outbox->nextBatch(kNow);
        QCOMPARE(edit.size(), 1);

        QVERIFY(m_ledger->markAnnotationDeleted(a.uuid, kNow.addSecs(30)).ok());
        QVERIFY(m_outbox->markConfirmed(idsOf(edit)).ok());
        QCOMPARE(m_outbox->pendingCount(), 0);
        QVector<AnnotationRecord> left = m_ledger->annotationsForBook("b1");
        QCOMPARE(left.size(), 1);
        QCOMPARE(left[0].deletedUtc, kNow.addSecs(30));

        // Now the deletion goes out and is confirmed: tombstone removed.
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, a.uuid, "b1", "{\"deleted\":1}").ok());
        const QVector<OutboxItem> deletion = m_outbox->nextBatch(kNow);
        QCOMPARE(deletion.size(), 1);
        QCOMPARE(deletion[0].payload, QByteArray("{\"deleted\":1}"));
        QVERIFY(m_outbox->markConfirmed(idsOf(deletion)).ok());
        QVERIFY(m_ledger->annotationsForBook("b1").isEmpty());
    }

    void deletionThenEditBackToLifeKeepsAnnotation()
    {
        // The deletion is in flight, then an edit made after it brings the
        // highlight back before anything is re-queued. Confirming the
        // deletion must not remove the live row.
        AnnotationRecord a;
        a.contentId = "b1";
        a.text = "x";
        a.updatedUtc = kNow;
        QVERIFY(m_ledger->upsertAnnotation(&a).ok());
        QVERIFY(m_ledger->markAnnotationDeleted(a.uuid, kNow.addSecs(10)).ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, a.uuid, "b1", "{\"deleted\":1}").ok());
        const QVector<OutboxItem> deletion = m_outbox->nextBatch(kNow);
        QCOMPARE(deletion.size(), 1);

        AnnotationRecord revived = m_ledger->annotationsForBook("b1")[0];
        revived.deletedUtc = QDateTime();
        revived.note = "back";
        revived.updatedUtc = kNow.addSecs(20);
        QVERIFY(m_ledger->upsertAnnotation(&revived).ok());
        QVERIFY(m_outbox->markConfirmed(idsOf(deletion)).ok());
        QCOMPARE(m_ledger->annotationsForBook("b1").size(), 1);
    }

    void rejectsBadInputAndClosedLedger()
    {
        QCOMPARE(m_outbox->enqueue(OutboxKind::Position, QString(), "b1", "{}").code,
                 StatusCode::InvalidArgument);
        QCOMPARE(m_outbox->enqueue(OutboxKind(0), "x", "b1", "{}").code,
                 StatusCode::InvalidArgument); // The v1 Session kind.
        QCOMPARE(m_outbox->enqueue(OutboxKind(9), "x", "b1", "{}").code,
                 StatusCode::InvalidArgument);
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "x", "b1", "{}").ok());

        m_ledger->close();
        QCOMPARE(m_outbox->enqueue(OutboxKind::Position, "y", "b1", "{}").code, StatusCode::Busy);
        QVERIFY(m_outbox->nextBatch(kNow).isEmpty());
        QCOMPARE(m_outbox->markConfirmed(QVector<qint64>() << 1).code, StatusCode::Busy);
        QCOMPARE(m_outbox->markFailed(QVector<qint64>() << 1, kNow).code, StatusCode::Busy);
        QCOMPARE(m_outbox->pendingCount(), 0);

        // Same outbox object works again after the ledger reopens (USB).
        QVERIFY(m_ledger->open().ok());
        QCOMPARE(m_outbox->pendingCount(), 1);

        QVERIFY(createOutbox(nullptr));
        QVERIFY(createOutbox(nullptr)->nextBatch(kNow).isEmpty());
    }

    void survivesCrashMidBatch()
    {
        // A child process does a sync and dies in the middle of confirming a
        // batch, inside the transaction, with pages already spilled to disk.
        // The parent then opens the ledger like a reboot would.
        const Paths paths = Paths::forRoot(m_dir->path() + "/crash");
        constexpr int kItems = 150;
        const QByteArray bigPayload(3000, 'p');

        const pid_t pid = fork();
        QVERIFY(pid >= 0);
        if (pid == 0) {
            int code = 1;
            {
                auto ledger = createLedger(paths);
                auto outbox = createOutbox(ledger.get());
                if (ledger->open().ok()) {
                    bool ok = true;
                    for (int i = 0; i < kItems && ok; ++i) {
                        SessionRecord r = makeSession("b1", kNow.addSecs(-86400 + i * 120), 60);
                        ok = ledger->appendSession(&r).ok()
                                && outbox->enqueue(OutboxKind::Status, r.uuid, "b1", bigPayload)
                                           .ok();
                    }
                    // First batch confirmed, second handed out, then the
                    // confirm for the second dies half way.
                    QVector<OutboxItem> first = outbox->nextBatch(kNow);
                    ok = ok && first.size() == 20 && outbox->markConfirmed(idsOf(first)).ok();
                    QVector<OutboxItem> second = outbox->nextBatch(kNow);
                    ok = ok && second.size() == 20;
                    QSqlDatabase db = ledgerdb::database(ledger.get());
                    QSqlQuery q(db);
                    ok = ok && q.exec("PRAGMA cache_size = 10") && db.transaction();
                    ok = ok && q.exec("DELETE FROM outbox"); // Far more than the cache holds.
                    if (ok)
                        _exit(QFile::exists(paths.ledger() + "-journal") ? 0 : 3);
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
        // No corruption: the journal rolled the half-done confirm back.
        QDir adds(paths.addsDir());
        QVERIFY(adds.entryList(QStringList() << "ledger.sqlite.corrupt-*", QDir::Files).isEmpty());

        auto outbox = createOutbox(ledger.get());
        QCOMPARE(outbox->pendingCount(), kItems - 20);
        QCOMPARE(ledger->sessionsForBook("b1").size(), kItems);

        // Everything still goes out, in order, exactly once.
        QSet<QString> seen;
        QDateTime now = kNow;
        QVector<OutboxItem> batch;
        while (!(batch = outbox->nextBatch(now)).isEmpty()) {
            for (const OutboxItem &item : batch) {
                QVERIFY(!seen.contains(item.entityUuid));
                seen.insert(item.entityUuid);
                QCOMPARE(item.payload, bigPayload);
            }
            QVERIFY(outbox->markConfirmed(idsOf(batch)).ok());
        }
        QCOMPARE(seen.size(), kItems - 20);
        QCOMPARE(outbox->pendingCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TstOutbox)
#include "tst_outbox.moc"
