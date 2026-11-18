// The SQLite ledger (include/smalt/ledger.h).
//
// SPEC: On-device data model (all); Crash safety ("Ledger protection");
// Sync engine ("Conflict rules"). One QSQLITE connection per Ledger, with a
// unique name, opened by open() and fully removed by close() so the user
// partition can be unmounted on USB connect.
#include "ledger_files.h"
#include "ledger_internal.h"

#include "smalt/log.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <qnumeric.h>

#include <atomic>

namespace smalt {
namespace {

const char kLog[] = "ledger";

// --- Enum <-> stored integer. The numbers are part of the file format. ---

int originToDb(BookOrigin o)
{
    switch (o) {
    case BookOrigin::Downloaded:
        return 0;
    case BookOrigin::Matched:
        return 1;
    case BookOrigin::Migrated:
        return 2;
    }
    return -1;
}

BookOrigin originFromDb(int v)
{
    switch (v) {
    case 1:
        return BookOrigin::Matched;
    case 2:
        return BookOrigin::Migrated;
    default:
        return BookOrigin::Downloaded;
    }
}

int statusToDb(ReadStatus s)
{
    switch (s) {
    case ReadStatus::Unknown:
        return 0;
    case ReadStatus::Reading:
        return 1;
    case ReadStatus::Finished:
        return 2;
    case ReadStatus::Abandoned:
        return 3;
    }
    return -1;
}

ReadStatus statusFromDb(int v)
{
    switch (v) {
    case 1:
        return ReadStatus::Reading;
    case 2:
        return ReadStatus::Finished;
    case 3:
        return ReadStatus::Abandoned;
    default:
        return ReadStatus::Unknown;
    }
}

int reasonToDb(PendingReason r)
{
    switch (r) {
    case PendingReason::DeletedUpstream:
        return 0;
    case PendingReason::LeftCollection:
        return 1;
    }
    return -1;
}

PendingReason reasonFromDb(int v)
{
    return v == 1 ? PendingReason::LeftCollection : PendingReason::DeletedUpstream;
}

int choiceToDb(PendingChoice c)
{
    switch (c) {
    case PendingChoice::Undecided:
        return 0;
    case PendingChoice::RemoveFromKobo:
        return 1;
    case PendingChoice::KeepUnlinked:
        return 2;
    case PendingChoice::DecideLater:
        return 3;
    }
    return -1;
}

PendingChoice choiceFromDb(int v)
{
    switch (v) {
    case 1:
        return PendingChoice::RemoveFromKobo;
    case 2:
        return PendingChoice::KeepUnlinked;
    case 3:
        return PendingChoice::DecideLater;
    default:
        return PendingChoice::Undecided;
    }
}

// --- Errors ---

bool looksBusy(const QSqlError &e)
{
    const int code = ledgerdb::sqliteErrorCode(e);
    if (code == ledgerdb::kSqliteBusy || code == ledgerdb::kSqliteLocked)
        return true;
    const QString t = e.text().toLower();
    return t.contains(QLatin1String("locked")) || t.contains(QLatin1String("busy"));
}

Status invalid(const QString &message)
{
    return Status::error(StatusCode::InvalidArgument, message);
}

Status closedStatus()
{
    return Status::error(StatusCode::Busy, QStringLiteral("ledger is closed"));
}

Status sqlFailure(const QSqlError &e, const char *what)
{
    const StatusCode code = looksBusy(e) ? StatusCode::Busy : StatusCode::IoError;
    const QString message = QStringLiteral("%1: %2").arg(QLatin1String(what), e.text());
    log::warn(kLog, message);
    return Status::error(code, message);
}

bool percentOk(double p)
{
    return !qIsNaN(p) && !qIsInf(p);
}

double clampPercent(double p)
{
    return qBound(0.0, p, 100.0);
}

QDateTime later(const QDateTime &a, const QDateTime &b)
{
    if (!a.isValid())
        return b;
    if (!b.isValid())
        return a;
    return a < b ? b : a;
}

QDateTime nowUtc()
{
    return QDateTime::currentDateTimeUtc();
}

// Rolls back on scope exit unless committed. Short transactions only.
class Transaction
{
public:
    explicit Transaction(QSqlDatabase db) : m_db(db), m_active(db.transaction()) { }

    ~Transaction()
    {
        if (m_active)
            m_db.rollback();
    }

    bool active() const { return m_active; }

    Status begin(const char *what) const
    {
        return m_active ? Status::success() : sqlFailure(m_db.lastError(), what);
    }

    Status commit(const char *what)
    {
        if (!m_active)
            return sqlFailure(m_db.lastError(), what);
        if (!m_db.commit())
            return sqlFailure(m_db.lastError(), what);
        m_active = false;
        return Status::success();
    }

private:
    QSqlDatabase m_db;
    bool m_active;
};

std::atomic<int> g_connectionCounter{ 0 };

} // namespace

class SqlLedger : public Ledger
{
public:
    explicit SqlLedger(const Paths &paths)
        : m_paths(paths), m_connection(QStringLiteral("smalt-ledger-%1").arg(++g_connectionCounter))
    {
    }

    ~SqlLedger() override { close(); }

    QSqlDatabase db() const
    {
        if (!m_open)
            return QSqlDatabase();
        return QSqlDatabase::database(m_connection, false);
    }

    Status open() override;
    void close() override;

    bool isOpen() const override { return m_open; }

    Status backupIfDue(const QDateTime &nowUtc) override;

    QString deviceId() override { return m_deviceId; }

    Status appendSession(SessionRecord *record) override;
    QVector<SessionRecord> sessionsForBook(const QString &contentId) override;

    Status setLocalProgress(const QString &contentId, const Position &position,
                            const QDateTime &updatedUtc) override;
    Status setRemoteProgress(const QString &contentId, const Position &position,
                             const QDateTime &updatedUtc, const QString &remoteDevice) override;
    Status progress(const QString &contentId, ProgressRecord *out) override;

    Status upsertAnnotation(AnnotationRecord *record) override;
    Status markAnnotationDeleted(const QString &uuid, const QDateTime &deletedUtc) override;
    QVector<AnnotationRecord> annotationsForBook(const QString &contentId) override;

    Status bookState(const QString &contentId, BookStateRecord *out) override;
    Status setLocalStatus(const QString &contentId, ReadStatus status,
                          const QDateTime &atUtc) override;
    Status setLocalRating(const QString &contentId, int rating, const QDateTime &atUtc) override;
    Status mergeRemoteState(const BookStateRecord &remote) override;

    Status putBookMap(const BookMapEntry &entry) override;
    Status bookMapByContentId(const QString &contentId, BookMapEntry *out) override;
    Status bookMapByBookId(qint64 bookId, BookMapEntry *out) override;
    Status unlinkBook(const QString &contentId) override;

    Status addPendingDecision(const PendingDecision &decision) override;
    QVector<PendingDecision> pendingDecisions() override;
    Status resolvePendingDecision(const QString &contentId, PendingChoice choice) override;

    Status correctSessionEnd(const QString &uuid, const Position &end, bool approximate);

private:
    Status openChecked();
    void removeConnection();
    Status ensureServerRow(QSqlDatabase db);
    QString deviceIdFromBackups() const;
    Status ensureRow(QSqlDatabase db, const char *table, const QString &contentId);
    Status bookMapWhere(const QString &where, const QVariant &value, BookMapEntry *out);

    Paths m_paths;
    QString m_connection;
    QString m_deviceId;
    bool m_open = false;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void SqlLedger::removeConnection()
{
    m_open = false;
    if (!QSqlDatabase::contains(m_connection))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

void SqlLedger::close()
{
    if (!m_open && !QSqlDatabase::contains(m_connection))
        return;
    removeConnection();
    log::info(kLog, QStringLiteral("closed"));
}

Status SqlLedger::open()
{
    if (m_open)
        return Status::success();
    if (!QDir().mkpath(m_paths.addsDir()))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("can't create %1").arg(m_paths.addsDir()));

    Status s = openChecked();
    if (s.code == StatusCode::Corrupt) {
        removeConnection();
        QString renamed;
        const Status q = ledgerfiles::quarantine(m_paths.ledger(), nowUtc(), &renamed);
        if (!q.ok()) {
            log::error(kLog,
                       QStringLiteral("ledger is corrupt (%1) and can't be renamed: %2")
                               .arg(s.message, q.message));
            return Status::error(StatusCode::Corrupt, s.message + QStringLiteral("; ") + q.message);
        }
        log::error(kLog,
                   QStringLiteral("ledger failed its integrity check (%1); kept as %2, "
                                  "starting a fresh ledger")
                           .arg(s.message, renamed));
        s = openChecked();
    }
    if (!s.ok()) {
        removeConnection();
        log::error(kLog, QStringLiteral("can't open: %1").arg(s.message));
        return s;
    }
    m_open = true;
    log::info(kLog, QStringLiteral("opened %1").arg(m_paths.ledger()));
    return Status::success();
}

Status SqlLedger::openChecked()
{
    const QString path = m_paths.ledger();
    const ledgerfiles::FileState state = ledgerfiles::inspect(path);
    if (state == ledgerfiles::FileState::NotSqlite)
        return Status::error(StatusCode::Corrupt, QStringLiteral("not a SQLite file"));
    if (state == ledgerfiles::FileState::Unreadable)
        return Status::error(StatusCode::IoError, QStringLiteral("can't read %1").arg(path));

    removeConnection();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
    if (!db.open())
        return Status::error(StatusCode::IoError, db.lastError().text());

    // Classifies a failure while reading the file. Only SQLite's own verdict
    // (SQLITE_CORRUPT, SQLITE_NOTADB) on a file with a SQLite header counts as
    // corruption. Anything else (an I/O error just after a remount, a lock, a
    // journal that can't be opened) leaves the file untouched: renaming a
    // healthy ledger would strand its unsent sessions and outbox.
    auto classify = [&](const QSqlError &e, const char *what) {
        const QString message = QStringLiteral("%1: %2 (SQLite code %3)")
                                        .arg(QLatin1String(what), e.text())
                                        .arg(ledgerdb::sqliteErrorCode(e));
        if (looksBusy(e))
            return Status::error(StatusCode::Busy, message);
        if (state == ledgerfiles::FileState::LooksLikeSqlite && ledgerdb::isCorruptionError(e))
            return Status::error(StatusCode::Corrupt, message);
        return Status::error(StatusCode::IoError, message);
    };

    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT count(*) FROM sqlite_master")) || !q.next())
            return classify(q.lastError(), "read schema");
        // FAT partition: rollback journal, full sync, no WAL.
        if (!q.exec(QStringLiteral("PRAGMA journal_mode = DELETE")))
            return classify(q.lastError(), "journal_mode");
        if (!q.next() || q.value(0).toString().toLower() != QLatin1String("delete"))
            return Status::error(StatusCode::IoError, QStringLiteral("journal_mode not DELETE"));
        if (!q.exec(QStringLiteral("PRAGMA synchronous = FULL")))
            return classify(q.lastError(), "synchronous");

        if (state == ledgerfiles::FileState::LooksLikeSqlite) {
            if (!q.exec(QStringLiteral("PRAGMA integrity_check")))
                return classify(q.lastError(), "integrity_check");
            QStringList problems;
            while (q.next() && problems.size() < 5)
                problems << q.value(0).toString();
            if (problems != QStringList() << QStringLiteral("ok"))
                return Status::error(StatusCode::Corrupt,
                                     QStringLiteral("integrity_check: ")
                                             + problems.join(QStringLiteral("; ")));
        }
    }

    const Status migrated = ledgerdb::migrate(db, ledgerdb::kSchemaVersion);
    if (!migrated.ok())
        return migrated;
    return ensureServerRow(db);
}

Status SqlLedger::ensureServerRow(QSqlDatabase db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT device_id FROM server WHERE id = 1")))
        return sqlFailure(q.lastError(), "read server row");
    if (q.next() && !q.value(0).toString().isEmpty()) {
        m_deviceId = q.value(0).toString();
        return Status::success();
    }

    // A fresh ledger. Keep the Kobo's device ID if a backup knows it (after a
    // corrupt ledger was renamed aside), so the server still sees one device.
    QString id = deviceIdFromBackups();
    if (id.isEmpty())
        id = ledgerdb::newUuid();
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO server (id, device_id, created_utc) "
                             "VALUES (1, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(ledgerdb::toDb(nowUtc()));
    if (!q.exec())
        return sqlFailure(q.lastError(), "write server row");
    m_deviceId = id;
    log::info(kLog, QStringLiteral("device ID %1").arg(id));
    return Status::success();
}

QString SqlLedger::deviceIdFromBackups() const
{
    const QStringList files = ledgerfiles::backups(m_paths.backupDir());
    for (int i = files.size() - 1; i >= 0; --i) {
        const QString name = m_connection + QStringLiteral("-backup");
        QString id;
        {
            QSqlDatabase b = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            b.setDatabaseName(files[i]);
            b.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (b.open()) {
                QSqlQuery q(b);
                if (q.exec(QStringLiteral("SELECT device_id FROM server WHERE id = 1")) && q.next())
                    id = q.value(0).toString();
            }
            b.close();
        }
        QSqlDatabase::removeDatabase(name);
        if (!id.isEmpty()) {
            log::info(kLog, QStringLiteral("device ID recovered from %1").arg(files[i]));
            return id;
        }
    }
    return QString();
}

Status SqlLedger::backupIfDue(const QDateTime &now)
{
    if (!m_open)
        return closedStatus();
    if (!now.isValid())
        return invalid(QStringLiteral("backupIfDue: invalid time"));
    const QString dir = m_paths.backupDir();
    const QString target = ledgerfiles::backupPath(dir, now.toUTC().date());
    if (QFileInfo(target).exists())
        return Status::success();
    if (!QDir().mkpath(dir))
        return Status::error(StatusCode::IoError, QStringLiteral("can't create %1").arg(dir));

    // Don't let a damaged file push good copies out of the rotation.
    {
        QSqlQuery q(db());
        if (!q.exec(QStringLiteral("PRAGMA quick_check")) || !q.next())
            return sqlFailure(q.lastError(), "quick_check before backup");
        if (q.value(0).toString() != QLatin1String("ok")) {
            log::error(kLog,
                       QStringLiteral("not backing up: quick_check says %1")
                               .arg(q.value(0).toString()));
            return Status::error(StatusCode::Corrupt, QStringLiteral("quick_check failed"));
        }
    }

    // Every write is a short committed transaction, so between calls the file
    // on disk is complete and no lock is held: a plain copy is consistent.
    const Status copied = ledgerfiles::copyDurably(m_paths.ledger(), target);
    if (!copied.ok()) {
        log::warn(kLog, QStringLiteral("backup failed: %1").arg(copied.message));
        return copied;
    }
    log::info(kLog, QStringLiteral("backup written to %1").arg(target));
    ledgerfiles::rotateBackups(dir, ledgerdb::kBackupsKept);
    return Status::success();
}

// ---------------------------------------------------------------------------
// session
// ---------------------------------------------------------------------------

Status SqlLedger::appendSession(SessionRecord *record)
{
    if (!record)
        return invalid(QStringLiteral("appendSession: no record"));
    if (!m_open)
        return closedStatus();
    const SessionRecord &r = *record;
    if (r.contentId.isEmpty())
        return invalid(QStringLiteral("session without a book"));
    if (!r.startUtc.isValid() || !r.endUtc.isValid() || r.endUtc < r.startUtc)
        return invalid(QStringLiteral("session times are invalid"));
    if (r.activeSeconds < kMinSessionSeconds)
        return invalid(QStringLiteral("session shorter than %1 s").arg(kMinSessionSeconds));
    if (r.pagesTurned < 0 || !percentOk(r.start.percent) || !percentOk(r.end.percent))
        return invalid(QStringLiteral("session positions are invalid"));

    const QString uuid = r.uuid.isEmpty() ? ledgerdb::newUuid() : r.uuid;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT 1 FROM session WHERE uuid = ?"));
    q.addBindValue(uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "check session");
    if (q.next())
        return invalid(QStringLiteral("session %1 already recorded").arg(uuid));

    q.prepare(QStringLiteral(
            "INSERT INTO session (uuid, content_id, start_utc, end_utc, active_seconds,"
            " start_locator, start_percent, end_locator, end_percent, pages_turned, estimated,"
            " approximate, created_utc) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(uuid);
    q.addBindValue(r.contentId);
    q.addBindValue(ledgerdb::toDb(r.startUtc));
    q.addBindValue(ledgerdb::toDb(r.endUtc));
    q.addBindValue(qlonglong(r.activeSeconds));
    q.addBindValue(r.start.locator);
    q.addBindValue(clampPercent(r.start.percent));
    q.addBindValue(r.end.locator);
    q.addBindValue(clampPercent(r.end.percent));
    q.addBindValue(r.pagesTurned);
    q.addBindValue(r.estimated ? 1 : 0);
    q.addBindValue(r.approximate ? 1 : 0);
    q.addBindValue(ledgerdb::toDb(nowUtc()));
    if (!q.exec())
        return sqlFailure(q.lastError(), "append session");
    record->uuid = uuid;
    return Status::success();
}

QVector<SessionRecord> SqlLedger::sessionsForBook(const QString &contentId)
{
    QVector<SessionRecord> out;
    if (!m_open)
        return out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
            "SELECT uuid, content_id, start_utc, end_utc, active_seconds, start_locator,"
            " start_percent, end_locator, end_percent, pages_turned, estimated, approximate"
            " FROM session WHERE content_id = ? ORDER BY start_utc, created_utc"));
    q.addBindValue(contentId);
    if (!q.exec()) {
        sqlFailure(q.lastError(), "read sessions");
        return out;
    }
    while (q.next()) {
        SessionRecord r;
        r.uuid = q.value(0).toString();
        r.contentId = q.value(1).toString();
        r.startUtc = ledgerdb::fromDb(q.value(2));
        r.endUtc = ledgerdb::fromDb(q.value(3));
        r.activeSeconds = q.value(4).toLongLong();
        r.start.locator = q.value(5).toString();
        r.start.percent = q.value(6).toDouble();
        r.end.locator = q.value(7).toString();
        r.end.percent = q.value(8).toDouble();
        r.pagesTurned = q.value(9).toInt();
        r.estimated = q.value(10).toInt() != 0;
        r.approximate = q.value(11).toInt() != 0;
        out << r;
    }
    return out;
}

Status SqlLedger::correctSessionEnd(const QString &uuid, const Position &end, bool approximate)
{
    if (!m_open)
        return closedStatus();
    if (!percentOk(end.percent))
        return invalid(QStringLiteral("invalid percent"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT sent_utc FROM session WHERE uuid = ?"));
    q.addBindValue(uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "check session");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("no session %1").arg(uuid));
    if (!q.value(0).isNull())
        return invalid(QStringLiteral("session %1 was already sent").arg(uuid));
    q.prepare(QStringLiteral("UPDATE session SET end_locator = ?, end_percent = ?, approximate = ?"
                             " WHERE uuid = ?"));
    q.addBindValue(end.locator);
    q.addBindValue(clampPercent(end.percent));
    q.addBindValue(approximate ? 1 : 0);
    q.addBindValue(uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "correct session");
    return Status::success();
}

// ---------------------------------------------------------------------------
// progress, book_state rows
// ---------------------------------------------------------------------------

Status SqlLedger::ensureRow(QSqlDatabase d, const char *table, const QString &contentId)
{
    QSqlQuery q(d);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO %1 (content_id, uuid) VALUES (?, ?)")
                      .arg(QLatin1String(table)));
    q.addBindValue(contentId);
    q.addBindValue(ledgerdb::newUuid());
    if (!q.exec())
        return sqlFailure(q.lastError(), "create row");
    return Status::success();
}

Status SqlLedger::setLocalProgress(const QString &contentId, const Position &position,
                                   const QDateTime &updatedUtc)
{
    if (!m_open)
        return closedStatus();
    if (contentId.isEmpty() || !updatedUtc.isValid() || !percentOk(position.percent))
        return invalid(QStringLiteral("setLocalProgress: invalid argument"));
    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok())
        return s;
    s = ensureRow(d, "progress", contentId);
    if (!s.ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral("UPDATE progress SET local_locator = ?, local_percent = ?,"
                             " local_updated_utc = ? WHERE content_id = ?"
                             " AND (local_updated_utc IS NULL OR local_updated_utc <= ?)"));
    q.addBindValue(position.locator);
    q.addBindValue(clampPercent(position.percent));
    q.addBindValue(ledgerdb::toDb(updatedUtc));
    q.addBindValue(contentId);
    q.addBindValue(ledgerdb::toDb(updatedUtc));
    if (!q.exec())
        return sqlFailure(q.lastError(), "set local progress");
    return t.commit("commit");
}

Status SqlLedger::setRemoteProgress(const QString &contentId, const Position &position,
                                    const QDateTime &updatedUtc, const QString &remoteDevice)
{
    if (!m_open)
        return closedStatus();
    if (contentId.isEmpty() || !updatedUtc.isValid() || !percentOk(position.percent))
        return invalid(QStringLiteral("setRemoteProgress: invalid argument"));
    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok())
        return s;
    s = ensureRow(d, "progress", contentId);
    if (!s.ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral("UPDATE progress SET remote_locator = ?, remote_percent = ?,"
                             " remote_updated_utc = ?, remote_device = ? WHERE content_id = ?"
                             " AND (remote_updated_utc IS NULL OR remote_updated_utc <= ?)"));
    q.addBindValue(position.locator);
    q.addBindValue(clampPercent(position.percent));
    q.addBindValue(ledgerdb::toDb(updatedUtc));
    q.addBindValue(remoteDevice);
    q.addBindValue(contentId);
    q.addBindValue(ledgerdb::toDb(updatedUtc));
    if (!q.exec())
        return sqlFailure(q.lastError(), "set remote progress");
    return t.commit("commit");
}

Status SqlLedger::progress(const QString &contentId, ProgressRecord *out)
{
    if (!out)
        return invalid(QStringLiteral("progress: no output"));
    *out = ProgressRecord();
    out->contentId = contentId;
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT local_locator, local_percent, local_updated_utc,"
                             " remote_locator, remote_percent, remote_updated_utc, remote_device"
                             " FROM progress WHERE content_id = ?"));
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read progress");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("no progress"));
    out->local.locator = q.value(0).toString();
    out->local.percent = q.value(1).toDouble();
    out->localUpdatedUtc = ledgerdb::fromDb(q.value(2));
    out->hasRemote = !q.value(5).isNull();
    out->remote.locator = q.value(3).toString();
    out->remote.percent = q.value(4).toDouble();
    out->remoteUpdatedUtc = ledgerdb::fromDb(q.value(5));
    out->remoteDevice = q.value(6).toString();
    return Status::success();
}

// ---------------------------------------------------------------------------
// annotation
// ---------------------------------------------------------------------------

Status SqlLedger::upsertAnnotation(AnnotationRecord *record)
{
    if (!record)
        return invalid(QStringLiteral("upsertAnnotation: no record"));
    if (!m_open)
        return closedStatus();
    AnnotationRecord r = *record;
    if (r.contentId.isEmpty())
        return invalid(QStringLiteral("annotation without a book"));
    if (!r.updatedUtc.isValid())
        r.updatedUtc = nowUtc();
    if (!r.createdUtc.isValid())
        r.createdUtc = r.updatedUtc;
    if (r.uuid.isEmpty())
        r.uuid = ledgerdb::newUuid();

    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT updated_utc, deleted_utc, remote_id FROM annotation"
                             " WHERE uuid = ?"));
    q.addBindValue(r.uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read annotation");

    if (!q.next()) {
        q.prepare(QStringLiteral(
                "INSERT INTO annotation (uuid, remote_id, content_id, anchor_start, anchor_end,"
                " text, context, note, colour, created_utc, updated_utc, deleted_utc)"
                " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        q.addBindValue(r.uuid);
        q.addBindValue(r.remoteId.isEmpty() ? QVariant() : QVariant(r.remoteId));
        q.addBindValue(r.contentId);
        q.addBindValue(r.anchorStart);
        q.addBindValue(r.anchorEnd);
        q.addBindValue(r.text);
        q.addBindValue(r.context);
        q.addBindValue(r.note);
        q.addBindValue(r.colour);
        q.addBindValue(ledgerdb::toDb(r.createdUtc));
        q.addBindValue(ledgerdb::toDb(r.updatedUtc));
        q.addBindValue(ledgerdb::toDb(r.deletedUtc));
        if (!q.exec())
            return sqlFailure(q.lastError(), "insert annotation");
    } else {
        const QDateTime existingUpdated = ledgerdb::fromDb(q.value(0));
        const QDateTime existingDeleted = ledgerdb::fromDb(q.value(1));
        const QString existingRemote = q.value(2).toString();
        // Conflict rules: newest edit wins; a deletion wins over edits made
        // before it. Losing edits are dropped quietly (not an error).
        const bool loses = existingDeleted.isValid()
                ? r.updatedUtc <= existingDeleted
                : (existingUpdated.isValid() && r.updatedUtc < existingUpdated);
        if (!loses) {
            q.prepare(QStringLiteral(
                    "UPDATE annotation SET remote_id = ?, anchor_start = ?, anchor_end = ?,"
                    " text = ?, context = ?, note = ?, colour = ?, updated_utc = ?,"
                    " deleted_utc = ? WHERE uuid = ?"));
            const QString remote = r.remoteId.isEmpty() ? existingRemote : r.remoteId;
            q.addBindValue(remote.isEmpty() ? QVariant() : QVariant(remote));
            q.addBindValue(r.anchorStart);
            q.addBindValue(r.anchorEnd);
            q.addBindValue(r.text);
            q.addBindValue(r.context);
            q.addBindValue(r.note);
            q.addBindValue(r.colour);
            q.addBindValue(ledgerdb::toDb(r.updatedUtc));
            q.addBindValue(ledgerdb::toDb(r.deletedUtc));
            q.addBindValue(r.uuid);
            if (!q.exec())
                return sqlFailure(q.lastError(), "update annotation");
        }
    }
    s = t.commit("commit");
    if (s.ok())
        record->uuid = r.uuid;
    return s;
}

Status SqlLedger::markAnnotationDeleted(const QString &uuid, const QDateTime &deletedUtc)
{
    if (!m_open)
        return closedStatus();
    if (uuid.isEmpty() || !deletedUtc.isValid())
        return invalid(QStringLiteral("markAnnotationDeleted: invalid argument"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT deleted_utc FROM annotation WHERE uuid = ?"));
    q.addBindValue(uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read annotation");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("no annotation %1").arg(uuid));
    if (!q.value(0).isNull())
        return Status::success(); // Already a tombstone; keep the first deletion time.
    q.prepare(QStringLiteral("UPDATE annotation SET deleted_utc = ? WHERE uuid = ?"));
    q.addBindValue(ledgerdb::toDb(deletedUtc));
    q.addBindValue(uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "delete annotation");
    return Status::success();
}

QVector<AnnotationRecord> SqlLedger::annotationsForBook(const QString &contentId)
{
    QVector<AnnotationRecord> out;
    if (!m_open)
        return out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT uuid, remote_id, content_id, anchor_start, anchor_end, text,"
                             " context, note, colour, created_utc, updated_utc, deleted_utc"
                             " FROM annotation WHERE content_id = ? ORDER BY created_utc, uuid"));
    q.addBindValue(contentId);
    if (!q.exec()) {
        sqlFailure(q.lastError(), "read annotations");
        return out;
    }
    while (q.next()) {
        AnnotationRecord r;
        r.uuid = q.value(0).toString();
        r.remoteId = q.value(1).toString();
        r.contentId = q.value(2).toString();
        r.anchorStart = q.value(3).toString();
        r.anchorEnd = q.value(4).toString();
        r.text = q.value(5).toString();
        r.context = q.value(6).toString();
        r.note = q.value(7).toString();
        r.colour = q.value(8).toString();
        r.createdUtc = ledgerdb::fromDb(q.value(9));
        r.updatedUtc = ledgerdb::fromDb(q.value(10));
        r.deletedUtc = ledgerdb::fromDb(q.value(11));
        out << r;
    }
    return out;
}

// ---------------------------------------------------------------------------
// book_state
// ---------------------------------------------------------------------------

Status SqlLedger::bookState(const QString &contentId, BookStateRecord *out)
{
    if (!out)
        return invalid(QStringLiteral("bookState: no output"));
    *out = BookStateRecord();
    out->contentId = contentId;
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT status, status_local_utc, status_remote_utc, rating,"
                             " rating_local_utc, rating_remote_utc FROM book_state"
                             " WHERE content_id = ?"));
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read book state");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("no book state"));
    out->status = statusFromDb(q.value(0).toInt());
    out->statusLocalUpdatedUtc = ledgerdb::fromDb(q.value(1));
    out->statusRemoteUpdatedUtc = ledgerdb::fromDb(q.value(2));
    out->rating = q.value(3).toInt();
    out->ratingLocalUpdatedUtc = ledgerdb::fromDb(q.value(4));
    out->ratingRemoteUpdatedUtc = ledgerdb::fromDb(q.value(5));
    return Status::success();
}

Status SqlLedger::setLocalStatus(const QString &contentId, ReadStatus status,
                                 const QDateTime &atUtc)
{
    if (!m_open)
        return closedStatus();
    if (contentId.isEmpty() || !atUtc.isValid() || statusToDb(status) < 0)
        return invalid(QStringLiteral("setLocalStatus: invalid argument"));
    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok() || !(s = ensureRow(d, "book_state", contentId)).ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral(
            "UPDATE book_state SET status = ?, status_local_utc = ? WHERE content_id = ?"));
    q.addBindValue(statusToDb(status));
    q.addBindValue(ledgerdb::toDb(atUtc));
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "set status");
    return t.commit("commit");
}

Status SqlLedger::setLocalRating(const QString &contentId, int rating, const QDateTime &atUtc)
{
    if (!m_open)
        return closedStatus();
    if (contentId.isEmpty() || !atUtc.isValid() || rating < 0 || rating > 5)
        return invalid(QStringLiteral("setLocalRating: invalid argument"));
    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok() || !(s = ensureRow(d, "book_state", contentId)).ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral(
            "UPDATE book_state SET rating = ?, rating_local_utc = ? WHERE content_id = ?"));
    q.addBindValue(rating);
    q.addBindValue(ledgerdb::toDb(atUtc));
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "set rating");
    return t.commit("commit");
}

Status SqlLedger::mergeRemoteState(const BookStateRecord &remote)
{
    if (!m_open)
        return closedStatus();
    if (remote.contentId.isEmpty() || statusToDb(remote.status) < 0 || remote.rating < 0
        || remote.rating > 5)
        return invalid(QStringLiteral("mergeRemoteState: invalid argument"));
    // The time of each remote value: its remote update time, or the local
    // one if the caller filled only that.
    const QDateTime statusAt = remote.statusRemoteUpdatedUtc.isValid()
            ? remote.statusRemoteUpdatedUtc
            : remote.statusLocalUpdatedUtc;
    const QDateTime ratingAt = remote.ratingRemoteUpdatedUtc.isValid()
            ? remote.ratingRemoteUpdatedUtc
            : remote.ratingLocalUpdatedUtc;

    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok() || !(s = ensureRow(d, "book_state", remote.contentId)).ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT status_local_utc, status_remote_utc, rating_local_utc,"
                             " rating_remote_utc FROM book_state WHERE content_id = ?"));
    q.addBindValue(remote.contentId);
    if (!q.exec() || !q.next())
        return sqlFailure(q.lastError(), "read book state");
    const QDateTime statusCurrent =
            later(ledgerdb::fromDb(q.value(0)), ledgerdb::fromDb(q.value(1)));
    const QDateTime ratingCurrent =
            later(ledgerdb::fromDb(q.value(2)), ledgerdb::fromDb(q.value(3)));

    // Per field, newest change wins; on a tie the value already here stays.
    if (statusAt.isValid() && (!statusCurrent.isValid() || statusAt > statusCurrent)) {
        q.prepare(QStringLiteral("UPDATE book_state SET status = ?, status_remote_utc = ?"
                                 " WHERE content_id = ?"));
        q.addBindValue(statusToDb(remote.status));
        q.addBindValue(ledgerdb::toDb(statusAt));
        q.addBindValue(remote.contentId);
        if (!q.exec())
            return sqlFailure(q.lastError(), "merge status");
    }
    if (ratingAt.isValid() && (!ratingCurrent.isValid() || ratingAt > ratingCurrent)) {
        q.prepare(QStringLiteral("UPDATE book_state SET rating = ?, rating_remote_utc = ?"
                                 " WHERE content_id = ?"));
        q.addBindValue(remote.rating);
        q.addBindValue(ledgerdb::toDb(ratingAt));
        q.addBindValue(remote.contentId);
        if (!q.exec())
            return sqlFailure(q.lastError(), "merge rating");
    }
    return t.commit("commit");
}

// ---------------------------------------------------------------------------
// book_map
// ---------------------------------------------------------------------------

Status SqlLedger::putBookMap(const BookMapEntry &e)
{
    if (!m_open)
        return closedStatus();
    if (e.contentId.isEmpty() || e.bookId <= 0 || originToDb(e.origin) < 0)
        return invalid(QStringLiteral("putBookMap: invalid argument"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO book_map (content_id, book_id, book_file_id, partial_md5,"
            " origin, created_utc, unlinked_utc) VALUES (?, ?, ?, ?, ?, ?, NULL)"));
    q.addBindValue(e.contentId);
    q.addBindValue(qlonglong(e.bookId));
    q.addBindValue(qlonglong(e.bookFileId));
    q.addBindValue(e.partialMd5);
    q.addBindValue(originToDb(e.origin));
    q.addBindValue(ledgerdb::toDb(e.createdUtc.isValid() ? e.createdUtc : nowUtc()));
    if (!q.exec())
        return sqlFailure(q.lastError(), "put book map");
    return Status::success();
}

Status SqlLedger::bookMapWhere(const QString &where, const QVariant &value, BookMapEntry *out)
{
    if (!out)
        return invalid(QStringLiteral("book map: no output"));
    *out = BookMapEntry();
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT content_id, book_id, book_file_id, partial_md5, origin,"
                             " created_utc FROM book_map WHERE %1 AND unlinked_utc IS NULL"
                             " ORDER BY created_utc DESC LIMIT 1")
                      .arg(where));
    q.addBindValue(value);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read book map");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("book not mapped"));
    out->contentId = q.value(0).toString();
    out->bookId = q.value(1).toLongLong();
    out->bookFileId = q.value(2).toLongLong();
    out->partialMd5 = q.value(3).toString();
    out->origin = originFromDb(q.value(4).toInt());
    out->createdUtc = ledgerdb::fromDb(q.value(5));
    return Status::success();
}

Status SqlLedger::bookMapByContentId(const QString &contentId, BookMapEntry *out)
{
    return bookMapWhere(QStringLiteral("content_id = ?"), contentId, out);
}

Status SqlLedger::bookMapByBookId(qint64 bookId, BookMapEntry *out)
{
    return bookMapWhere(QStringLiteral("book_id = ?"), qlonglong(bookId), out);
}

Status SqlLedger::unlinkBook(const QString &contentId)
{
    if (!m_open)
        return closedStatus();
    // The row stays, marked unlinked, so the history of where a book came
    // from isn't lost.
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE book_map SET unlinked_utc = ? WHERE content_id = ?"
                             " AND unlinked_utc IS NULL"));
    q.addBindValue(ledgerdb::toDb(nowUtc()));
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "unlink book");
    if (q.numRowsAffected() < 1)
        return Status::error(StatusCode::NotFound, QStringLiteral("book not mapped"));
    return Status::success();
}

// ---------------------------------------------------------------------------
// pending_decision
// ---------------------------------------------------------------------------

Status SqlLedger::addPendingDecision(const PendingDecision &decision)
{
    if (!m_open)
        return closedStatus();
    if (decision.contentId.isEmpty() || reasonToDb(decision.reason) < 0
        || choiceToDb(decision.choice) < 0)
        return invalid(QStringLiteral("addPendingDecision: invalid argument"));
    const QDateTime created = decision.createdUtc.isValid() ? decision.createdUtc : nowUtc();
    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok())
        return s;
    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT resolved_utc FROM pending_decision WHERE content_id = ?"));
    q.addBindValue(decision.contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read decision");
    if (!q.next()) {
        q.prepare(QStringLiteral("INSERT INTO pending_decision (content_id, reason, choice,"
                                 " created_utc) VALUES (?, ?, ?, ?)"));
        q.addBindValue(decision.contentId);
        q.addBindValue(reasonToDb(decision.reason));
        q.addBindValue(choiceToDb(decision.choice));
        q.addBindValue(ledgerdb::toDb(created));
    } else if (!q.value(0).isNull()) {
        // Decided before, and removed upstream again: ask again.
        q.prepare(QStringLiteral("UPDATE pending_decision SET reason = ?, choice = ?,"
                                 " created_utc = ?, resolved_utc = NULL WHERE content_id = ?"));
        q.addBindValue(reasonToDb(decision.reason));
        q.addBindValue(choiceToDb(decision.choice));
        q.addBindValue(ledgerdb::toDb(created));
        q.addBindValue(decision.contentId);
    } else {
        // Already waiting: keep its place and the choice so far.
        q.prepare(QStringLiteral("UPDATE pending_decision SET reason = ? WHERE content_id = ?"));
        q.addBindValue(reasonToDb(decision.reason));
        q.addBindValue(decision.contentId);
    }
    if (!q.exec())
        return sqlFailure(q.lastError(), "write decision");
    return t.commit("commit");
}

QVector<PendingDecision> SqlLedger::pendingDecisions()
{
    QVector<PendingDecision> out;
    if (!m_open)
        return out;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT content_id, reason, choice, created_utc"
                               " FROM pending_decision WHERE resolved_utc IS NULL"
                               " ORDER BY created_utc, content_id"))) {
        sqlFailure(q.lastError(), "read decisions");
        return out;
    }
    while (q.next()) {
        PendingDecision d;
        d.contentId = q.value(0).toString();
        d.reason = reasonFromDb(q.value(1).toInt());
        d.choice = choiceFromDb(q.value(2).toInt());
        d.createdUtc = ledgerdb::fromDb(q.value(3));
        out << d;
    }
    return out;
}

Status SqlLedger::resolvePendingDecision(const QString &contentId, PendingChoice choice)
{
    if (!m_open)
        return closedStatus();
    if (choiceToDb(choice) < 0)
        return invalid(QStringLiteral("resolvePendingDecision: invalid choice"));
    // "Decide later" (and undecided) keep the book waiting.
    const bool final =
            choice == PendingChoice::RemoveFromKobo || choice == PendingChoice::KeepUnlinked;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE pending_decision SET choice = ?, resolved_utc = ?"
                             " WHERE content_id = ?"));
    q.addBindValue(choiceToDb(choice));
    q.addBindValue(final ? ledgerdb::toDb(nowUtc()) : QVariant());
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "resolve decision");
    if (q.numRowsAffected() < 1)
        return Status::error(StatusCode::NotFound, QStringLiteral("no pending decision"));
    return Status::success();
}

// ---------------------------------------------------------------------------

std::unique_ptr<Ledger> createLedger(const Paths &paths)
{
    return std::unique_ptr<Ledger>(new SqlLedger(paths));
}

namespace ledgerdb {

int sqliteErrorCode(const QSqlError &error)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
    // Since 5.3 the QSQLITE driver gives the sqlite3 result code as a string.
    bool ok = false;
    const int code = error.nativeErrorCode().toInt(&ok);
    if (!ok)
        return -1;
#else
    // Qt 5.2: the QSQLITE driver passes the sqlite3 result code as the number.
    const int code = error.number();
#endif
    if (code < 0)
        return -1;
    return code & 0xff; // The primary code, also if extended codes are on.
}

bool isCorruptionError(const QSqlError &error)
{
    const int code = sqliteErrorCode(error);
    return code == kSqliteCorrupt || code == kSqliteNotADatabase;
}

QSqlDatabase database(Ledger *ledger)
{
    SqlLedger *sql = dynamic_cast<SqlLedger *>(ledger);
    return sql ? sql->db() : QSqlDatabase();
}

Status correctSessionEnd(Ledger *ledger, const QString &uuid, const Position &end, bool approximate)
{
    SqlLedger *sql = dynamic_cast<SqlLedger *>(ledger);
    if (!sql)
        return invalid(QStringLiteral("not a SQLite ledger"));
    return sql->correctSessionEnd(uuid, end, approximate);
}

} // namespace ledgerdb
} // namespace smalt
