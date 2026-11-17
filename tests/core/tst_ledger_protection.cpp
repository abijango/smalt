// Task 10 acceptance: "Corrupt file is renamed and a fresh ledger opens",
// "Backup rotation keeps 7 files", plus close/reopen around USB connect.
// SPEC: Crash safety ("Ledger protection"); On-device data model ("Where it lives").
#include "core_test_util.h"

#include "smalt/ledger.h"
#include "smalt/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace smalt;
using namespace coretest;

namespace {

QStringList corruptCopies(const Paths &paths)
{
    QStringList out;
    const QStringList all =
            QDir(paths.addsDir())
                    .entryList(QStringList() << QStringLiteral("ledger.sqlite.corrupt-*"),
                               QDir::Files, QDir::Name);
    for (const QString &name : all) {
        if (!name.endsWith(QStringLiteral("-journal")))
            out << name;
    }
    return out;
}

QStringList backups(const Paths &paths)
{
    return QDir(paths.backupDir())
            .entryList(QStringList() << QStringLiteral("ledger-*.sqlite"), QDir::Files, QDir::Name);
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

// Makes a real ledger with one session in it, closed.
QString makeLedgerWithSession(const Paths &paths)
{
    auto ledger = createLedger(paths);
    if (!ledger->open().ok())
        return QString();
    SessionRecord r = makeSession(QStringLiteral("book-1"), utc(2026, 11, 1), 120);
    if (!ledger->appendSession(&r).ok())
        return QString();
    const QString id = ledger->deviceId();
    ledger->close();
    return id;
}

} // namespace

class TstLedgerProtection : public QObject
{
    Q_OBJECT

private slots:

    void garbageFileIsRenamedAndFreshLedgerOpens()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        const QByteArray garbage = QByteArray("this is not a database ").repeated(200);
        QVERIFY(writeFile(paths.ledger(), garbage));

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QVERIFY(ledger->isOpen());

        // The bad file is kept, byte for byte, under a timestamped name.
        const QStringList kept = corruptCopies(paths);
        QCOMPARE(kept.size(), 1);
        QVERIFY(QRegularExpression(QStringLiteral("^ledger\\.sqlite\\.corrupt-\\d{8}T\\d{6}Z$"))
                        .match(kept.first())
                        .hasMatch());
        QCOMPARE(readAll(paths.addsDir() + QLatin1Char('/') + kept.first()), garbage);

        // The fresh ledger works.
        SessionRecord r = makeSession(QStringLiteral("book-2"), utc(2026, 11, 2), 60);
        QVERIFY(ledger->appendSession(&r).ok());
        QCOMPARE(ledger->sessionsForBook(QStringLiteral("book-2")).size(), 1);
        QVERIFY(!ledger->deviceId().isEmpty());
    }

    void damagedPagesAreRenamed()
    {
        // A real ledger with its pages overwritten after the header: the
        // header looks fine, the integrity check catches it.
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(!makeLedgerWithSession(paths).isEmpty());

        QByteArray bytes = readAll(paths.ledger());
        QVERIFY(bytes.size() > 4096);
        for (int i = 100; i < bytes.size(); ++i)
            bytes[i] = char(0xA5 ^ (i & 0xff));
        QVERIFY(writeFile(paths.ledger(), bytes));

        auto ledger = createLedger(paths);
        Status s = ledger->open();
        QVERIFY2(s.ok(), qPrintable(s.message));
        QCOMPARE(corruptCopies(paths).size(), 1);
        QCOMPARE(readAll(paths.addsDir() + QLatin1Char('/') + corruptCopies(paths).first()), bytes);
        QCOMPARE(ledger->sessionsForBook(QStringLiteral("book-1")).size(), 0);
    }

    void damagedTablePageIsCaughtByIntegrityCheck()
    {
        // Page 1 (the schema) intact, a later page scrambled: opening and
        // reading the schema work, only the integrity check notices.
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(!makeLedgerWithSession(paths).isEmpty());
        QByteArray bytes = readAll(paths.ledger());
        const int pageSize = 4096;
        QVERIFY(bytes.size() >= 3 * pageSize);
        for (int i = bytes.size() - pageSize; i < bytes.size(); ++i)
            bytes[i] = char(0x5A ^ (i & 0xff));
        QVERIFY(writeFile(paths.ledger(), bytes));

        auto ledger = createLedger(paths);
        QVERIFY(ledger->open().ok());
        QCOMPARE(corruptCopies(paths).size(), 1);
        QVERIFY(ledger->sessionsForBook(QStringLiteral("book-1")).isEmpty());
    }

    void journalTravelsWithTheCorruptFile()
    {
        // A leftover journal belongs to the bad file; it must not be replayed
        // into the fresh ledger.
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        QVERIFY(writeFile(paths.ledger(), QByteArray(2048, 'x')));
        const QByteArray journal(1024, 'j');
        QVERIFY(writeFile(paths.ledger() + QStringLiteral("-journal"), journal));

        auto ledger = createLedger(paths);
        QVERIFY(ledger->open().ok());
        const QStringList kept = corruptCopies(paths);
        QCOMPARE(kept.size(), 1);
        const QString base = paths.addsDir() + QLatin1Char('/') + kept.first();
        QVERIFY(!base.endsWith(QStringLiteral("-journal")));
        QCOMPARE(readAll(base + QStringLiteral("-journal")), journal);
    }

    void twoCorruptionsKeepBothFiles()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        for (int round = 0; round < 2; ++round) {
            QVERIFY(writeFile(paths.ledger(), QByteArray(4096, char('a' + round))));
            auto ledger = createLedger(paths);
            QVERIFY(ledger->open().ok());
            ledger->close();
        }
        QCOMPARE(corruptCopies(paths).size(), 2); // Same second: the name gets a counter.
    }

    void deviceIdIsStableAcrossReopen()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        const QString id = makeLedgerWithSession(paths);
        QVERIFY(!id.isEmpty());
        auto again = createLedger(paths);
        QVERIFY(again->open().ok());
        QCOMPARE(again->deviceId(), id);
        QVERIFY(QRegularExpression(
                        QStringLiteral(
                                "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"))
                        .match(id)
                        .hasMatch());
    }

    void deviceIdSurvivesCorruptionThroughBackup()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        QString id;
        {
            auto ledger = createLedger(paths);
            QVERIFY(ledger->open().ok());
            id = ledger->deviceId();
            QVERIFY(ledger->backupIfDue(utc(2026, 11, 10)).ok());
            ledger->close();
        }
        QVERIFY(writeFile(paths.ledger(), QByteArray(4096, 'z')));
        auto ledger = createLedger(paths);
        QVERIFY(ledger->open().ok());
        QCOMPARE(corruptCopies(paths).size(), 1);
        QCOMPARE(ledger->deviceId(), id);
    }

    void closeAndReopenAroundUsb()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        auto ledger = createLedger(paths);
        ledger->close(); // Safe when closed.
        QVERIFY(ledger->open().ok());
        const QString id = ledger->deviceId();
        SessionRecord a = makeSession(QStringLiteral("book-1"), utc(2026, 11, 1), 30);
        QVERIFY(ledger->appendSession(&a).ok());

        ledger->close(); // USB connect.
        QVERIFY(!ledger->isOpen());
        ledger->close();
        QVERIFY(!ledgerdb::database(ledger.get()).isValid());
        SessionRecord b = makeSession(QStringLiteral("book-1"), utc(2026, 11, 2), 30);
        QCOMPARE(ledger->appendSession(&b).code, StatusCode::Busy);
        QVERIFY(ledger->sessionsForBook(QStringLiteral("book-1")).isEmpty());
        QCOMPARE(ledger->backupIfDue(utc(2026, 11, 2)).code, StatusCode::Busy);
        // No journal left behind while the partition is unmounted.
        QVERIFY(!QFile::exists(paths.ledger() + QStringLiteral("-journal")));

        QVERIFY(ledger->open().ok()); // USB disconnect.
        QCOMPARE(ledger->deviceId(), id);
        QVERIFY(ledger->appendSession(&b).ok());
        QCOMPARE(ledger->sessionsForBook(QStringLiteral("book-1")).size(), 2);
        QVERIFY(ledger->open().ok()); // Opening twice is harmless.
    }

    void twoLedgersUseSeparateConnections()
    {
        QTemporaryDir a, b;
        auto la = createLedger(Paths::forRoot(a.path()));
        auto lb = createLedger(Paths::forRoot(b.path()));
        QVERIFY(la->open().ok());
        QVERIFY(lb->open().ok());
        QVERIFY(ledgerdb::database(la.get()).connectionName()
                != ledgerdb::database(lb.get()).connectionName());
        QVERIFY(la->deviceId() != lb->deviceId());
    }

    void unwritableFolderIsAnIoErrorAndRenamesNothing()
    {
        QTemporaryDir dir;
        // The .adds folder is a file, so .adds/bookorbit can't be made.
        QVERIFY(writeFile(dir.filePath(QStringLiteral(".adds")), "x"));
        auto ledger = createLedger(Paths::forRoot(dir.path()));
        Status s = ledger->open();
        QCOMPARE(s.code, StatusCode::IoError);
        QVERIFY(!ledger->isOpen());
    }

    void backupOncePerDay()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        auto ledger = createLedger(paths);
        QVERIFY(ledger->open().ok());
        SessionRecord r = makeSession(QStringLiteral("book-1"), utc(2026, 11, 1), 120);
        QVERIFY(ledger->appendSession(&r).ok());

        QVERIFY(ledger->backupIfDue(utc(2026, 11, 10, 8)).ok());
        QCOMPARE(backups(paths), QStringList() << QStringLiteral("ledger-20261110.sqlite"));
        const QDateTime first =
                QFileInfo(paths.backupDir() + "/ledger-20261110.sqlite").lastModified();
        QVERIFY(ledger->backupIfDue(utc(2026, 11, 10, 23)).ok()); // Same day: not due.
        QCOMPARE(backups(paths).size(), 1);
        QCOMPARE(QFileInfo(paths.backupDir() + "/ledger-20261110.sqlite").lastModified(), first);

        // The copy is a working ledger with the data in it.
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), "backup");
            db.setDatabaseName(paths.backupDir() + "/ledger-20261110.sqlite");
            QVERIFY(db.open());
            QCOMPARE(countRows(db, QStringLiteral("SELECT count(*) FROM session")), 1);
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("PRAGMA integrity_check")) && q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("ok"));
            db.close();
        }
        QSqlDatabase::removeDatabase("backup");
        QVERIFY(QDir(paths.backupDir()).entryList(QStringList() << "*.tmp", QDir::Files).isEmpty());
    }

    void backupRotationKeepsSeven()
    {
        QTemporaryDir dir;
        Paths paths = Paths::forRoot(dir.path());
        auto ledger = createLedger(paths);
        QVERIFY(ledger->open().ok());
        // An unrelated file in the backup folder is left alone.
        QVERIFY(QDir().mkpath(paths.backupDir()));
        QVERIFY(writeFile(paths.backupDir() + "/notes.txt", "mine"));

        for (int day = 1; day <= 10; ++day)
            QVERIFY(ledger->backupIfDue(utc(2026, 11, day)).ok());

        QStringList expected;
        for (int day = 4; day <= 10; ++day)
            expected << QStringLiteral("ledger-202611%1.sqlite").arg(day, 2, 10, QLatin1Char('0'));
        QCOMPARE(backups(paths), expected);
        QCOMPARE(backups(paths).size(), ledgerdb::kBackupsKept);
        QVERIFY(QFile::exists(paths.backupDir() + "/notes.txt"));
        // The ledger itself is untouched.
        QVERIFY(QFile::exists(paths.ledger()));
        QVERIFY(ledger->isOpen());
    }
};

QTEST_GUILESS_MAIN(TstLedgerProtection)
#include "tst_ledger_protection.moc"
