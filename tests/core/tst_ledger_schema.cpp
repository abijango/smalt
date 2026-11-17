// Task 10 acceptance: "Migrations from empty and from every earlier version",
// plus the FAT-partition settings (SPEC, On-device data model, "Where it lives").
#include "core_test_util.h"

#include "smalt/ledger.h"
#include "smalt/paths.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace smalt;
using namespace coretest;

namespace {

const QStringList kTables = {
    QStringLiteral("server"),   QStringLiteral("book_map"),     QStringLiteral("session"),
    QStringLiteral("progress"), QStringLiteral("annotation"),   QStringLiteral("book_state"),
    QStringLiteral("outbox"),   QStringLiteral("remote_cache"), QStringLiteral("pending_decision"),
};

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
