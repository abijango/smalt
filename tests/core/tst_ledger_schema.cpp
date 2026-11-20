// Task 10 acceptance: "Migrations from empty and from every earlier version",
// plus the FAT-partition settings (SPEC, On-device data model, "Where it lives").
#include "core_test_util.h"

#include "smalt/ledger.h"
#include "smalt/outbox.h"
#include "smalt/paths.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace smalt;
using namespace coretest;

namespace {

const QStringList kTables = {
    QStringLiteral("server"),           QStringLiteral("book_map"),
    QStringLiteral("session"),          QStringLiteral("progress"),
    QStringLiteral("annotation"),       QStringLiteral("book_state"),
    QStringLiteral("outbox"),           QStringLiteral("remote_cache"),
    QStringLiteral("pending_decision"), QStringLiteral("page_event"),
};

bool exec(QSqlDatabase db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        qWarning("%s: %s", qPrintable(sql), qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

// Builds a database at `version` with the migrations alone, as an older Smalt
// would have left it.
bool buildAtVersion(const QString &path, int version)
{
    const QString name = QStringLiteral("build-%1").arg(version);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            ok = ledgerdb::migrate(db, version).ok() && ledgerdb::schemaVersion(db) == version;
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QStringList schemaOfLedger(Ledger *ledger)
{
    return schemaOf(ledgerdb::database(ledger));
}

} // namespace

class TstLedgerSchema : public QObject
{
    Q_OBJECT

private slots:

    void freshLedgerHasEveryTable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(!QFile::exists(paths.ledger()));

        auto ledger = createLedger(paths);
        QVERIFY(!ledger->isOpen());
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QVERIFY(ledger->isOpen());
        QVERIFY(QFile::exists(paths.ledger()));

        QSqlDatabase db = ledgerdb::database(ledger.get());
        QCOMPARE(ledgerdb::schemaVersion(db), ledgerdb::kSchemaVersion);
        for (const QString &table : kTables) {
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("SELECT count(*) FROM ") + table));
        }
    }

    void fatSettings()
    {
        QTemporaryDir dir;
        auto ledger = createLedger(Paths::forRoot(dir.path()));
        QVERIFY(ledger->open().ok());
        QSqlDatabase db = ledgerdb::database(ledger.get());

        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")) && q.next());
        QCOMPARE(q.value(0).toString().toLower(),
                 QStringLiteral("delete")); // Rollback journal, no WAL.
        QVERIFY(q.exec(QStringLiteral("PRAGMA synchronous")) && q.next());
        QCOMPARE(q.value(0).toInt(), 2); // FULL
        QVERIFY(!QFile::exists(Paths::forRoot(dir.path()).ledger() + QStringLiteral("-wal")));
    }

    void migrationsFromEveryEarlierVersion_data()
    {
        QTest::addColumn<int>("from");
        for (int v = 0; v < ledgerdb::kSchemaVersion; ++v)
            QTest::newRow(qPrintable(QStringLiteral("v%1").arg(v))) << v;
    }

    void migrationsFromEveryEarlierVersion()
    {
        QFETCH(int, from);

        // Reference: a ledger made from nothing by the current code.
        QTemporaryDir freshDir;
        auto fresh = createLedger(Paths::forRoot(freshDir.path()));
        QVERIFY(fresh->open().ok());
        const QStringList expected = schemaOfLedger(fresh.get());
        QVERIFY(!expected.isEmpty());

        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        QVERIFY(buildAtVersion(paths.ledger(), from));

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QCOMPARE(ledgerdb::schemaVersion(ledgerdb::database(ledger.get())),
                 ledgerdb::kSchemaVersion);
        QCOMPARE(schemaOfLedger(ledger.get()), expected);
        QVERIFY(!ledger->deviceId().isEmpty());
    }

    void migrationV1ToV2KeepsEveryRow()
    {
        // A version 1 ledger as wave 1 code would have written it, with a row
        // in every table and outbox items of every v1 kind.
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        QVERIFY(buildAtVersion(paths.ledger(), 1));
        const qint64 t = utc(2026, 11, 1, 20).toMSecsSinceEpoch();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), "v1");
            db.setDatabaseName(paths.ledger());
            QVERIFY(db.open());
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO server (id, device_id, pull_cursor,"
                                        " last_pull_utc, last_sync_utc, created_utc)"
                                        " VALUES (1, 'dev-v1', 'cur-9', %1, %1, %1)")
                                 .arg(t)));
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO book_map (content_id, book_id, book_file_id,"
                                        " partial_md5, origin, created_utc)"
                                        " VALUES ('b1', 7, 70, 'md5', 1, %1)")
                                 .arg(t)));
            // Two sessions: one handed to the outbox and confirmed, one not sent.
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO session (uuid, content_id, start_utc, end_utc,"
                                        " active_seconds, start_locator, start_percent,"
                                        " end_locator, end_percent, pages_turned, estimated,"
                                        " approximate, created_utc, sent_utc, confirmed_utc)"
                                        " VALUES ('s-sent', 'b1', %1, %2, 600, 'a', 10, 'b', 12,"
                                        " 5, 0, 1, %1, %2, %2)")
                                 .arg(t)
                                 .arg(t + 700000)));
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO session (uuid, content_id, start_utc, end_utc,"
                                        " active_seconds, pages_turned, created_utc)"
                                        " VALUES ('s-new', 'b1', %1, %2, 120, 2, %1)")
                                 .arg(t + 3600000)
                                 .arg(t + 3800000)));
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO progress (content_id, uuid, local_locator,"
                                        " local_percent, local_updated_utc)"
                                        " VALUES ('b1', 'pr-1', 'x', 12, %1)")
                                 .arg(t)));
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO annotation (uuid, content_id, text,"
                                        " created_utc, updated_utc) VALUES ('an-1', 'b1', 'hi',"
                                        " %1, %1)")
                                 .arg(t)));
            // v1 statuses: 0 unknown, 1 reading, 2 finished, 3 abandoned.
            for (int v = 0; v <= 3; ++v)
                QVERIFY(exec(db,
                             QStringLiteral("INSERT INTO book_state (content_id, uuid, status,"
                                            " status_local_utc, rating) VALUES ('st%1', 'u%1',"
                                            " %1, %2, %3)")
                                     .arg(v)
                                     .arg(t)
                                     .arg(v)));
            // v1 outbox kinds: 0 session, 1 position, 2 annotation, 3 status.
            for (int k = 0; k <= 3; ++k)
                QVERIFY(exec(db,
                             QStringLiteral("INSERT INTO outbox (kind, entity_uuid, content_id,"
                                            " payload, created_utc, sent_rev)"
                                            " VALUES (%1, 'e%1', 'b1', 'p%1', %2, %3)")
                                     .arg(k)
                                     .arg(t + k)
                                     .arg(k == 0 ? 1 : 0)));
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO remote_cache (cache_key, kind, value)"
                                        " VALUES ('k', 'collections', 'v')")));
            QVERIFY(exec(db,
                         QStringLiteral("INSERT INTO pending_decision (content_id, reason,"
                                        " created_utc) VALUES ('b9', 1, %1)")
                                 .arg(t)));
            db.close();
        }
        QSqlDatabase::removeDatabase("v1");

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QSqlDatabase db = ledgerdb::database(ledger.get());
        QCOMPARE(ledgerdb::schemaVersion(db), 2);

        // server row: same device, cursor kept, no sweep owed.
        QCOMPARE(ledger->deviceId(), QStringLiteral("dev-v1"));
        QCOMPARE(ledger->pullCursor(), QStringLiteral("cur-9"));
        QCOMPARE(ledger->lastPullUtc().toMSecsSinceEpoch(), t);
        QCOMPARE(ledger->lastSyncUtc().toMSecsSinceEpoch(), t);
        QVERIFY(!ledger->sweepOwed());

        // book_map: kept, watermark 0.
        BookMapEntry map;
        QVERIFY(ledger->bookMapByContentId(QStringLiteral("b1"), &map).ok());
        QCOMPARE(map.bookId, qint64(7));
        QCOMPARE(map.bookFileId, qint64(70));
        QCOMPARE(map.statsWatermark, qint64(0));

        // Sessions: both kept as Finished, every value intact.
        const QVector<SessionRecord> sessions = ledger->sessionsForBook(QStringLiteral("b1"));
        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].uuid, QStringLiteral("s-sent"));
        QCOMPARE(sessions[0].state, SessionState::Finished);
        QCOMPARE(sessions[0].activeSeconds, qint64(600));
        QCOMPARE(sessions[0].startUtc.toMSecsSinceEpoch(), t);
        QCOMPARE(sessions[0].endUtc.toMSecsSinceEpoch(), t + 700000);
        QCOMPARE(sessions[0].start.locator, QStringLiteral("a"));
        QCOMPARE(sessions[0].end.percent, 12.0);
        QCOMPARE(sessions[0].pagesTurned, 5);
        QVERIFY(sessions[0].approximate);
        QCOMPARE(sessions[0].startPage, -1);
        QCOMPARE(sessions[0].endPage, -1);
        QCOMPARE(sessions[0].totalPages, 0);
        QCOMPARE(sessions[1].uuid, QStringLiteral("s-new"));
        QCOMPARE(sessions[1].state, SessionState::Finished);
        QCOMPARE(sessions[1].activeSeconds, qint64(120));
        QCOMPARE(ledger->sessionsInRange(utc(2026, 11, 1), utc(2026, 11, 2)).size(), 2);
        // Their end can still be corrected; their times can't change.
        QVERIFY(ledger->correctSessionEnd(QStringLiteral("s-sent"), Position{ "c", 13.0 }, false)
                        .ok());
        QVERIFY(!exec(db, QStringLiteral("UPDATE session SET end_utc = 0 WHERE uuid = 's-new'")));

        // Statuses mapped to BookOrbit's six (Finished -> Read).
        const ReadStatus expected[] = { ReadStatus::Unknown, ReadStatus::Reading, ReadStatus::Read,
                                        ReadStatus::Abandoned };
        for (int v = 0; v <= 3; ++v) {
            BookStateRecord st;
            QVERIFY(ledger->bookState(QStringLiteral("st%1").arg(v), &st).ok());
            QCOMPARE(st.status, expected[v]);
            QCOMPARE(st.rating, v);
            QCOMPARE(st.uuid, QStringLiteral("u%1").arg(v));
            QCOMPARE(st.statusLocalUpdatedUtc.toMSecsSinceEpoch(), t);
        }

        // Outbox: every row still there, the kind-0 one kept in place but
        // never handed out or counted.
        QCOMPARE(countRows(db, QStringLiteral("SELECT count(*) FROM outbox")), 4);
        QCOMPARE(countRows(db, QStringLiteral("SELECT count(*) FROM outbox WHERE kind = 0")), 1);
        auto outbox = createOutbox(ledger.get());
        QCOMPARE(outbox->pendingCount(), 3);
        const QDateTime later = utc(2026, 12, 1);
        QStringList seen;
        QVector<OutboxItem> batch;
        while (!(batch = outbox->nextBatch(later)).isEmpty()) {
            QVector<qint64> ids;
            for (const OutboxItem &item : batch) {
                seen << item.entityUuid;
                ids << item.id;
            }
            QVERIFY(outbox->markConfirmed(ids).ok());
        }
        QCOMPARE(seen, QStringList() << "e1" << "e2" << "e3");
        // Confirming or failing the kind-0 row by ID does nothing to it.
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT id FROM outbox WHERE kind = 0")) && q.next());
        const qint64 kindZero = q.value(0).toLongLong();
        QVERIFY(outbox->markConfirmed(QVector<qint64>() << kindZero).ok());
        QVERIFY(outbox->markFailed(QVector<qint64>() << kindZero, later).ok());
        QCOMPARE(countRows(db,
                           QStringLiteral("SELECT count(*) FROM outbox WHERE kind = 0"
                                          " AND attempts = 0 AND payload = 'p0'")),
                 1);

        // The rest.
        ProgressRecord p;
        QVERIFY(ledger->progress(QStringLiteral("b1"), &p).ok());
        QCOMPARE(p.uuid, QStringLiteral("pr-1"));
        QCOMPARE(ledger->annotationsForBook(QStringLiteral("b1")).size(), 1);
        QCOMPARE(countRows(db, QStringLiteral("SELECT count(*) FROM remote_cache")), 1);
        QCOMPARE(ledger->pendingDecisions().size(), 1);

        // And v2 works on top: page events for a migrated book.
        SessionRecord open;
        open.contentId = QStringLiteral("b1");
        open.startUtc = utc(2026, 11, 2, 21);
        QVERIFY(ledger->beginSession(&open).ok());
        PageEventRecord e;
        e.sessionUuid = open.uuid;
        e.contentId = QStringLiteral("b1");
        e.page = 3;
        e.totalPages = 100;
        e.startUtc = open.startUtc;
        e.durationSeconds = 40;
        QVERIFY(ledger->appendPageEvent(e).ok());
        QCOMPARE(ledger->booksWithPendingPageEvents(), QStringList() << "b1");
    }

    void migrationKeepsData()
    {
        // Data written at the current version survives a close and reopen
        // (which runs the migration step again as a no-op).
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        auto ledger = createLedger(paths);
        QVERIFY(ledger->open().ok());
        SessionRecord r = makeSession(QStringLiteral("book-1"), utc(2026, 11, 1), 600);
        QVERIFY(ledger->appendSession(&r).ok());
        ledger->close();

        auto again = createLedger(paths);
        QVERIFY(again->open().ok());
        QCOMPARE(again->sessionsForBook(QStringLiteral("book-1")).size(), 1);
    }

    void zeroByteFileIsEmpty()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        QFile f(paths.ledger());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QCOMPARE(ledgerdb::schemaVersion(ledgerdb::database(ledger.get())),
                 ledgerdb::kSchemaVersion);
        // Nothing was renamed: an empty file is a valid empty database.
        QCOMPARE(QDir(paths.addsDir())
                         .entryList(QStringList() << QStringLiteral("*.corrupt-*"), QDir::Files)
                         .size(),
                 0);
    }

    void newerVersionIsRefusedAndKept()
    {
        // A ledger from a newer Smalt (downgrade): don't touch it.
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        QVERIFY(buildAtVersion(paths.ledger(), ledgerdb::kSchemaVersion));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), "bump");
            db.setDatabaseName(paths.ledger());
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(
                    QStringLiteral("PRAGMA user_version = %1").arg(ledgerdb::kSchemaVersion + 1)));
            db.close();
        }
        QSqlDatabase::removeDatabase("bump");

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY(!s.ok());
        QCOMPARE(s.code, StatusCode::Unavailable);
        QVERIFY(!ledger->isOpen());
        QVERIFY(QFile::exists(paths.ledger()));
        QCOMPARE(QDir(paths.addsDir())
                         .entryList(QStringList() << QStringLiteral("*.corrupt-*"), QDir::Files)
                         .size(),
                 0);
    }

    void migrateRefusesUnknownTarget()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.sqlite"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), "target");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QCOMPARE(ledgerdb::migrate(db, ledgerdb::kSchemaVersion + 1).code,
                     StatusCode::InvalidArgument);
            QCOMPARE(ledgerdb::schemaVersion(db), 0);
            db.close();
        }
        QSqlDatabase::removeDatabase("target");
    }
};

QTEST_GUILESS_MAIN(TstLedgerSchema)
#include "tst_ledger_schema.moc"
