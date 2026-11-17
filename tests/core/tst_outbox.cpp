// Task 10: the outbox. Acceptance: "Retried uploads never produce duplicate
// sessions (same UUID)", "Outbox survives a simulated crash mid-batch with
// nothing lost". SPEC: On-device data model ("Tables": outbox); Sync engine
// ("Order of a full sync", "Networking": backoff).
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

    QString addSession(const QString &book, int minute)
    {
        SessionRecord r = makeSession(book, kNow.addSecs(-86400 + minute * 60), 60);
        if (!m_ledger->appendSession(&r).ok())
            return QString();
        return r.uuid;
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
        QVERIFY(int(OutboxKind::Session) < int(OutboxKind::Position));
        QVERIFY(int(OutboxKind::Position) < int(OutboxKind::Annotation));
        QVERIFY(int(OutboxKind::Annotation) < int(OutboxKind::Status));
    }

    void kindOrderThenOldestFirstInBatchesOf20()
    {
        // Queued in the "wrong" order on purpose.
        QVERIFY(m_outbox->enqueue(OutboxKind::Status, "st-1", "b1", "{\"s\":1}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Annotation, "an-1", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-1", "b1", "{}").ok());
        QStringList sessions;
        for (int i = 0; i < 25; ++i) {
            sessions << QStringLiteral("se-%1").arg(i, 2, 10, QLatin1Char('0'));
            QVERIFY(m_outbox->enqueue(OutboxKind::Session, sessions.last(), "b1", "{}").ok());
        }
        QCOMPARE(m_outbox->pendingCount(), 28);

        QVector<OutboxItem> batch = m_outbox->nextBatch(kNow);
        QCOMPARE(batch.size(), kOutboxBatchSize);
        QCOMPARE(uuidsOf(batch), sessions.mid(0, 20));
        for (const OutboxItem &item : batch) {
            QCOMPARE(item.kind, OutboxKind::Session);
            QCOMPARE(item.contentId, QStringLiteral("b1"));
            QCOMPARE(item.payload, QByteArray("{}"));
            QVERIFY(item.createdUtc.isValid());
            QVERIFY(item.nextRetryUtc.isNull());
        }
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());

        batch = m_outbox->nextBatch(kNow);
        QCOMPARE(uuidsOf(batch), sessions.mid(20));
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());

        batch = m_outbox->nextBatch(kNow);
        QCOMPARE(uuidsOf(batch), QStringList() << "pos-1");
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
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, "a", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, "b", "b2", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "c", "b2", "{}").ok());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow, 1)), QStringList() << "a");
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow, kOutboxBatchSize, "b2")), QStringList() << "b");
        QVERIFY(m_outbox->nextBatch(kNow, 0).isEmpty());
        QVERIFY(m_outbox->nextBatch(kNow, kOutboxBatchSize, "b3").isEmpty());
    }

    void reEnqueueReplacesPayload()
    {
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{\"p\":10}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, "x", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "pos-b1", "b1", "{\"p\":20}").ok());
        QCOMPARE(m_outbox->pendingCount(), 2);
        QVERIFY(m_outbox->markConfirmed(idsOf(m_outbox->nextBatch(kNow))).ok());
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
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, "s", "b1", "{}").ok());
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
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, "s", "b1", "{}").ok());
        QVERIFY(m_outbox->enqueue(OutboxKind::Position, "p", "b2", "{}").ok());
        QVERIFY(m_outbox->markFailed(idsOf(m_outbox->nextBatch(kNow)), kNow).ok());
        QCOMPARE(uuidsOf(m_outbox->nextBatch(kNow)), QStringList() << "p");
    }

    void retriedUploadsNeverDuplicateSessions()
    {
        const QString uuid = addSession("b1", 0);
        QVERIFY(!uuid.isEmpty());
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, uuid, "b1", "{\"try\":1}").ok());

        // First upload times out.
        QVector<OutboxItem> batch = m_outbox->nextBatch(kNow);
        QCOMPARE(uuidsOf(batch), QStringList() << uuid);
        QVERIFY(m_outbox->markFailed(idsOf(batch), kNow).ok());

        // The recorder queues the same session again (book closed twice, a
        // crash-replayed hook...): still one item, one session.
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, uuid, "b1", "{\"try\":2}").ok());
        QCOMPARE(m_outbox->pendingCount(), 1);
        SessionRecord again = makeSession("b1", kNow.addSecs(-86400), 60);
        again.uuid = uuid;
        QCOMPARE(m_ledger->appendSession(&again).code, StatusCode::InvalidArgument);
        QCOMPARE(m_ledger->sessionsForBook("b1").size(), 1);

        // Retry after the backoff: same UUID, same row.
        batch = m_outbox->nextBatch(kNow.addSecs(kOutboxMaxBackoffSeconds));
        QCOMPARE(batch.size(), 1);
        QCOMPARE(batch[0].entityUuid, uuid);
        QCOMPARE(batch[0].attempts, 1);
        QVERIFY(m_outbox->markConfirmed(idsOf(batch)).ok());
        QCOMPARE(m_outbox->pendingCount(), 0);

        // Once the server has it, it can't be queued again.
        QCOMPARE(m_outbox->enqueue(OutboxKind::Session, uuid, "b1", "{}").code,
                 StatusCode::InvalidArgument);
        QCOMPARE(m_outbox->pendingCount(), 0);
        QCOMPARE(m_ledger->sessionsForBook("b1").size(), 1);
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
        QCOMPARE(m_outbox->enqueue(OutboxKind::Session, QString(), "b1", "{}").code,
                 StatusCode::InvalidArgument);
        QCOMPARE(m_outbox->enqueue(OutboxKind(9), "x", "b1", "{}").code,
                 StatusCode::InvalidArgument);
        QVERIFY(m_outbox->enqueue(OutboxKind::Session, "x", "b1", "{}").ok());

        m_ledger->close();
        QCOMPARE(m_outbox->enqueue(OutboxKind::Session, "y", "b1", "{}").code, StatusCode::Busy);
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
                                && outbox->enqueue(OutboxKind::Session, r.uuid, "b1", bigPayload)
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
