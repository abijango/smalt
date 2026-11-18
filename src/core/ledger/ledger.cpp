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

// book_state.status, schema version 2 (schema.cpp).
int statusToDb(ReadStatus s)
{
    switch (s) {
    case ReadStatus::Unknown:
        return 0;
    case ReadStatus::Unread:
        return 1;
    case ReadStatus::WantToRead:
        return 2;
    case ReadStatus::Reading:
        return 3;
    case ReadStatus::OnHold:
        return 4;
    case ReadStatus::Read:
        return 5;
    case ReadStatus::Abandoned:
        return 6;
    }
    return -1;
}

ReadStatus statusFromDb(int v)
{
    switch (v) {
    case 1:
        return ReadStatus::Unread;
    case 2:
        return ReadStatus::WantToRead;
    case 3:
        return ReadStatus::Reading;
    case 4:
        return ReadStatus::OnHold;
    case 5:
        return ReadStatus::Read;
    case 6:
        return ReadStatus::Abandoned;
    default:
        return ReadStatus::Unknown;
    }
}

// session.state.
int stateToDb(SessionState s)
{
    switch (s) {
    case SessionState::Open:
        return 0;
    case SessionState::Finished:
        return 1;
    case SessionState::Dropped:
        return 2;
    }
    return -1;
}

SessionState stateFromDb(int v)
{
    switch (v) {
    case 0:
        return SessionState::Open;
    case 2:
        return SessionState::Dropped;
    default:
        return SessionState::Finished;
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

    QString pullCursor() override;
    QDateTime lastPullUtc() override;
    Status setPullCursor(const QString &cursor, const QDateTime &pulledUtc) override;
    QDateTime lastSyncUtc() override;
    Status setLastSyncUtc(const QDateTime &atUtc) override;
    bool sweepOwed() override;
    Status setSweepOwed(bool owed) override;

    Status beginSession(SessionRecord *record) override;
    Status updateOpenSession(const SessionRecord &record) override;
    Status finishSession(const SessionRecord &record) override;
    Status appendSession(SessionRecord *record) override;
    Status correctSessionEnd(const QString &uuid, const Position &end, bool approximate) override;
    Status session(const QString &uuid, SessionRecord *out) override;
    Status openSession(SessionRecord *out) override;
    QVector<SessionRecord> sessionsForBook(const QString &contentId) override;
    QVector<SessionRecord> recentSessionsForBook(const QString &contentId, int limit) override;
    QVector<SessionRecord> sessionsInRange(const QDateTime &fromUtc,
                                           const QDateTime &toUtc) override;
    QVector<SessionRecord> unconfirmedSessions() override;

    Status appendPageEvent(const PageEventRecord &event) override;
    QVector<PageEventRecord> pageEventsInRange(const QDateTime &fromUtc, const QDateTime &toUtc,
                                               QVector<bool> *confirmed) override;
    QVector<PageEventRecord> pageEventsAfter(const QString &contentId, qint64 afterUnixSeconds,
                                             int limit) override;
    QStringList booksWithPendingPageEvents() override;

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
    Status setStatsWatermark(const QString &contentId, qint64 unixSeconds) override;

    Status cacheGet(const QString &key, QByteArray *value, QDateTime *updatedUtc) override;
    Status cachePut(const QString &key, const QString &kind, const QByteArray &value,
                    const QDateTime &updatedUtc) override;
    Status cacheClear(const QString &kind) override;

    Status addPendingDecision(const PendingDecision &decision) override;
    QVector<PendingDecision> pendingDecisions() override;
    Status resolvePendingDecision(const QString &contentId, PendingChoice choice) override;

private:
    Status openChecked();
    void removeConnection();
    Status ensureServerRow(QSqlDatabase db);
    QString deviceIdFromBackups() const;
    Status ensureRow(QSqlDatabase db, const char *table, const QString &contentId);
    Status bookMapWhere(const QString &where, const QVariant &value, BookMapEntry *out);
    QVariant serverValue(const char *column);
    Status setServerValues(const QString &assignments, const QVariantList &values);
    Status insertSession(const SessionRecord &r, SessionState state);
    // Checks the values a caller may set on a session (all but state).
    Status checkSessionValues(const SessionRecord &r) const;
    // Writes the running or final values of an open session; `state` is the
    // new state. One UPDATE, so it is atomic.
    Status writeOpenSession(const SessionRecord &r, SessionState state, const char *what);
    QVector<SessionRecord> querySessions(const QString &whereAndOrder, const QVariantList &binds);

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
// server row
// ---------------------------------------------------------------------------

QVariant SqlLedger::serverValue(const char *column)
{
    if (!m_open)
        return QVariant();
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT %1 FROM server WHERE id = 1").arg(QLatin1String(column)))
        || !q.next()) {
        sqlFailure(q.lastError(), "read server row");
        return QVariant();
    }
    return q.value(0);
}

Status SqlLedger::setServerValues(const QString &assignments, const QVariantList &values)
{
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE server SET %1 WHERE id = 1").arg(assignments));
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec())
        return sqlFailure(q.lastError(), "write server row");
    if (q.numRowsAffected() < 1)
        return Status::error(StatusCode::IoError, QStringLiteral("server row missing"));
    return Status::success();
}

QString SqlLedger::pullCursor()
{
    return serverValue("pull_cursor").toString();
}

QDateTime SqlLedger::lastPullUtc()
{
    return ledgerdb::fromDb(serverValue("last_pull_utc"));
}

Status SqlLedger::setPullCursor(const QString &cursor, const QDateTime &pulledUtc)
{
    if (!pulledUtc.isValid())
        return invalid(QStringLiteral("setPullCursor: invalid time"));
    return setServerValues(QStringLiteral("pull_cursor = ?, last_pull_utc = ?"),
                           QVariantList() << (cursor.isEmpty() ? QVariant() : QVariant(cursor))
                                          << ledgerdb::toDb(pulledUtc));
}

QDateTime SqlLedger::lastSyncUtc()
{
    return ledgerdb::fromDb(serverValue("last_sync_utc"));
}

Status SqlLedger::setLastSyncUtc(const QDateTime &atUtc)
{
    if (!atUtc.isValid())
        return invalid(QStringLiteral("setLastSyncUtc: invalid time"));
    return setServerValues(QStringLiteral("last_sync_utc = ?"),
                           QVariantList() << ledgerdb::toDb(atUtc));
}

bool SqlLedger::sweepOwed()
{
    return serverValue("sweep_owed").toInt() != 0;
}

Status SqlLedger::setSweepOwed(bool owed)
{
    return setServerValues(QStringLiteral("sweep_owed = ?"), QVariantList() << (owed ? 1 : 0));
}

// ---------------------------------------------------------------------------
// session
//
// Open -> Finished or Dropped, never back. While open, updateOpenSession()
// and finishSession() rewrite the running values; afterwards only the end
// position and the approximate mark change (correctSessionEnd()). Triggers in
// schema.cpp enforce the same rules on the file. Every write is one
// statement, so a crash leaves the old row or the new one.
// ---------------------------------------------------------------------------

namespace {

// "Some page event of this session is above its book's watermark", for the
// `confirmed` flag: the watermark of any book_map row, linked or not, and 0
// without one. So a session of an unmatched book reads as not confirmed (the
// server doesn't have it), though it isn't pending (unconfirmedSessions() and
// the pending counts take only linked books, ledgerdb::kSessionHasPendingEvents).
// A session with no page events reads as confirmed: nothing is left to send.
const char kHasEventsAboveWatermark[] =
        " EXISTS (SELECT 1 FROM page_event pe WHERE pe.session_uuid = session.uuid"
        " AND pe.start_unix > COALESCE((SELECT bm.stats_watermark FROM book_map bm"
        " WHERE bm.content_id = session.content_id), 0)) ";

} // namespace

QVector<SessionRecord> SqlLedger::querySessions(const QString &whereAndOrder,
                                                const QVariantList &binds)
{
    QVector<SessionRecord> out;
    if (!m_open)
        return out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT uuid, content_id, state, start_utc, end_utc, active_seconds,"
                             " start_locator, start_percent, end_locator, end_percent,"
                             " start_page, end_page, total_pages, pages_turned, estimated,"
                             " approximate, NOT")
              + QLatin1String(kHasEventsAboveWatermark) + QStringLiteral("FROM session ")
              + whereAndOrder);
    for (const QVariant &v : binds)
        q.addBindValue(v);
    if (!q.exec()) {
        sqlFailure(q.lastError(), "read sessions");
        return out;
    }
    while (q.next()) {
        SessionRecord r;
        r.uuid = q.value(0).toString();
        r.contentId = q.value(1).toString();
        r.state = stateFromDb(q.value(2).toInt());
        r.startUtc = ledgerdb::fromDb(q.value(3));
        r.endUtc = ledgerdb::fromDb(q.value(4));
        r.activeSeconds = q.value(5).toLongLong();
        r.start.locator = q.value(6).toString();
        r.start.percent = q.value(7).toDouble();
        r.end.locator = q.value(8).toString();
        r.end.percent = q.value(9).toDouble();
        r.startPage = q.value(10).toInt();
        r.endPage = q.value(11).toInt();
        r.totalPages = q.value(12).toInt();
        r.pagesTurned = q.value(13).toInt();
        r.estimated = q.value(14).toInt() != 0;
        r.approximate = q.value(15).toInt() != 0;
        r.confirmed = q.value(16).toInt() != 0;
        out << r;
    }
    return out;
}

Status SqlLedger::checkSessionValues(const SessionRecord &r) const
{
    if (r.activeSeconds < 0 || r.pagesTurned < 0 || r.startPage < -1 || r.endPage < -1
        || r.totalPages < 0 || !percentOk(r.start.percent) || !percentOk(r.end.percent))
        return invalid(QStringLiteral("session values are invalid"));
    return Status::success();
}

Status SqlLedger::insertSession(const SessionRecord &r, SessionState state)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
            "INSERT INTO session (uuid, content_id, state, start_utc, end_utc, active_seconds,"
            " start_locator, start_percent, end_locator, end_percent, start_page, end_page,"
            " total_pages, pages_turned, estimated, approximate, created_utc)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(r.uuid);
    q.addBindValue(r.contentId);
    q.addBindValue(stateToDb(state));
    q.addBindValue(ledgerdb::toDb(r.startUtc));
    q.addBindValue(ledgerdb::toDb(r.endUtc));
    q.addBindValue(qlonglong(r.activeSeconds));
    q.addBindValue(r.start.locator);
    q.addBindValue(clampPercent(r.start.percent));
    q.addBindValue(r.end.locator);
    q.addBindValue(clampPercent(r.end.percent));
    q.addBindValue(r.startPage);
    q.addBindValue(r.endPage);
    q.addBindValue(r.totalPages);
    q.addBindValue(r.pagesTurned);
    q.addBindValue(r.estimated ? 1 : 0);
    q.addBindValue(r.approximate ? 1 : 0);
    q.addBindValue(ledgerdb::toDb(nowUtc()));
    if (!q.exec())
        return sqlFailure(q.lastError(), "insert session");
    return Status::success();
}

Status SqlLedger::beginSession(SessionRecord *record)
{
    if (!record)
        return invalid(QStringLiteral("beginSession: no record"));
    if (!m_open)
        return closedStatus();
    SessionRecord r = *record;
    if (r.contentId.isEmpty())
        return invalid(QStringLiteral("session without a book"));
    if (!r.startUtc.isValid())
        return invalid(QStringLiteral("session without a start time"));
    if (!r.endUtc.isValid())
        r.endUtc = r.startUtc;
    if (r.endUtc < r.startUtc)
        return invalid(QStringLiteral("session ends before it starts"));
    Status s = checkSessionValues(r);
    if (!s.ok())
        return s;
    if (r.uuid.isEmpty())
        r.uuid = ledgerdb::newUuid();

    // At most one open session per device; the recorder finishes the old one
    // (after a crash, from openSession()) before beginning another.
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT uuid FROM session WHERE uuid = ? OR state = 0"));
    q.addBindValue(r.uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "check session");
    if (q.next()) {
        if (q.value(0).toString() == r.uuid)
            return invalid(QStringLiteral("session %1 already recorded").arg(r.uuid));
        return invalid(QStringLiteral("session %1 is still open").arg(q.value(0).toString()));
    }
    s = insertSession(r, SessionState::Open);
    if (!s.ok())
        return s;
    r.state = SessionState::Open;
    r.confirmed = true; // No page events yet.
    *record = r;
    return Status::success();
}

Status SqlLedger::appendSession(SessionRecord *record)
{
    if (!record)
        return invalid(QStringLiteral("appendSession: no record"));
    if (!m_open)
        return closedStatus();
    SessionRecord r = *record;
    if (r.contentId.isEmpty())
        return invalid(QStringLiteral("session without a book"));
    if (!r.startUtc.isValid() || !r.endUtc.isValid() || r.endUtc < r.startUtc)
        return invalid(QStringLiteral("session times are invalid"));
    if (r.activeSeconds < kMinSessionSeconds)
        return invalid(QStringLiteral("session shorter than %1 s").arg(kMinSessionSeconds));
    Status s = checkSessionValues(r);
    if (!s.ok())
        return s;
    if (r.uuid.isEmpty())
        r.uuid = ledgerdb::newUuid();

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT 1 FROM session WHERE uuid = ?"));
    q.addBindValue(r.uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "check session");
    if (q.next())
        return invalid(QStringLiteral("session %1 already recorded").arg(r.uuid));
    s = insertSession(r, SessionState::Finished);
    if (!s.ok())
        return s;
    r.state = SessionState::Finished;
    r.confirmed = true; // No page events yet.
    *record = r;
    return Status::success();
}

Status SqlLedger::writeOpenSession(const SessionRecord &r, SessionState state, const char *what)
{
    if (!m_open)
        return closedStatus();
    if (!r.endUtc.isValid())
        return invalid(QStringLiteral("%1: no end time").arg(QLatin1String(what)));
    Status s = checkSessionValues(r);
    if (!s.ok())
        return s;

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT content_id, start_utc, state FROM session WHERE uuid = ?"));
    q.addBindValue(r.uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read session");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("no session %1").arg(r.uuid));
    if (q.value(2).toInt() != stateToDb(SessionState::Open))
        return invalid(QStringLiteral("session %1 is not open").arg(r.uuid));
    if (!r.contentId.isEmpty() && r.contentId != q.value(0).toString())
        return invalid(QStringLiteral("session %1 is for another book").arg(r.uuid));
    if (r.endUtc < ledgerdb::fromDb(q.value(1)))
        return invalid(QStringLiteral("session %1 would end before it starts").arg(r.uuid));

    // The start time and the book are the session's identity and never change.
    q.prepare(QStringLiteral(
            "UPDATE session SET state = ?, end_utc = ?, active_seconds = ?, start_locator = ?,"
            " start_percent = ?, end_locator = ?, end_percent = ?, start_page = ?, end_page = ?,"
            " total_pages = ?, pages_turned = ?, estimated = ?, approximate = ?"
            " WHERE uuid = ? AND state = 0"));
    q.addBindValue(stateToDb(state));
    q.addBindValue(ledgerdb::toDb(r.endUtc));
    q.addBindValue(qlonglong(r.activeSeconds));
    q.addBindValue(r.start.locator);
    q.addBindValue(clampPercent(r.start.percent));
    q.addBindValue(r.end.locator);
    q.addBindValue(clampPercent(r.end.percent));
    q.addBindValue(r.startPage);
    q.addBindValue(r.endPage);
    q.addBindValue(r.totalPages);
    q.addBindValue(r.pagesTurned);
    q.addBindValue(r.estimated ? 1 : 0);
    q.addBindValue(r.approximate ? 1 : 0);
    q.addBindValue(r.uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), what);
    if (q.numRowsAffected() < 1)
        return invalid(QStringLiteral("session %1 is not open").arg(r.uuid));
    return Status::success();
}

Status SqlLedger::updateOpenSession(const SessionRecord &record)
{
    return writeOpenSession(record, SessionState::Open, "update open session");
}

Status SqlLedger::finishSession(const SessionRecord &record)
{
    const SessionState state = record.activeSeconds < kMinSessionSeconds ? SessionState::Dropped
                                                                         : SessionState::Finished;
    const Status s = writeOpenSession(record, state, "finish session");
    if (s.ok() && state == SessionState::Dropped)
        log::info(kLog,
                  QStringLiteral("session %1 dropped: %2 s of reading")
                          .arg(record.uuid)
                          .arg(record.activeSeconds));
    return s;
}

Status SqlLedger::correctSessionEnd(const QString &uuid, const Position &end, bool approximate)
{
    if (!m_open)
        return closedStatus();
    if (!percentOk(end.percent))
        return invalid(QStringLiteral("invalid percent"));
    // Only the end position and the mark: allowed in every state.
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE session SET end_locator = ?, end_percent = ?, approximate = ?"
                             " WHERE uuid = ?"));
    q.addBindValue(end.locator);
    q.addBindValue(clampPercent(end.percent));
    q.addBindValue(approximate ? 1 : 0);
    q.addBindValue(uuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "correct session");
    if (q.numRowsAffected() < 1)
        return Status::error(StatusCode::NotFound, QStringLiteral("no session %1").arg(uuid));
    return Status::success();
}

Status SqlLedger::session(const QString &uuid, SessionRecord *out)
{
    if (!out)
        return invalid(QStringLiteral("session: no output"));
    *out = SessionRecord();
    if (!m_open)
        return closedStatus();
    const QVector<SessionRecord> found =
            querySessions(QStringLiteral("WHERE uuid = ?"), QVariantList() << uuid);
    if (found.isEmpty())
        return Status::error(StatusCode::NotFound, QStringLiteral("no session %1").arg(uuid));
    *out = found.first();
    return Status::success();
}

Status SqlLedger::openSession(SessionRecord *out)
{
    if (!out)
        return invalid(QStringLiteral("openSession: no output"));
    *out = SessionRecord();
    if (!m_open)
        return closedStatus();
    const QVector<SessionRecord> found = querySessions(
            QStringLiteral("WHERE state = 0 ORDER BY start_utc DESC LIMIT 1"), QVariantList());
    if (found.isEmpty())
        return Status::error(StatusCode::NotFound, QStringLiteral("no open session"));
    *out = found.first();
    return Status::success();
}

QVector<SessionRecord> SqlLedger::sessionsForBook(const QString &contentId)
{
    return querySessions(QStringLiteral("WHERE content_id = ? ORDER BY start_utc, rowid"),
                         QVariantList() << contentId);
}

QVector<SessionRecord> SqlLedger::recentSessionsForBook(const QString &contentId, int limit)
{
    if (limit <= 0)
        return QVector<SessionRecord>();
    // Dropped sessions are never counted, so Stats never sees them.
    return querySessions(QStringLiteral("WHERE content_id = ? AND state <> 2"
                                        " ORDER BY start_utc DESC, rowid DESC LIMIT ?"),
                         QVariantList() << contentId << limit);
}

QVector<SessionRecord> SqlLedger::sessionsInRange(const QDateTime &fromUtc, const QDateTime &toUtc)
{
    if (!fromUtc.isValid() || !toUtc.isValid() || !(fromUtc < toUtc))
        return QVector<SessionRecord>();
    return querySessions(QStringLiteral("WHERE start_utc >= ? AND start_utc < ? AND state <> 2"
                                        " ORDER BY start_utc, rowid"),
                         QVariantList() << ledgerdb::toDb(fromUtc) << ledgerdb::toDb(toUtc));
}

QVector<SessionRecord> SqlLedger::unconfirmedSessions()
{
    // Linked books only; Dropped sessions never.
    return querySessions(QStringLiteral("WHERE") + QLatin1String(ledgerdb::kSessionHasPendingEvents)
                                 + QStringLiteral("ORDER BY start_utc, rowid"),
                         QVariantList());
}

// ---------------------------------------------------------------------------
// page_event (append-only; triggers in schema.cpp refuse updates and deletes)
// ---------------------------------------------------------------------------

Status SqlLedger::appendPageEvent(const PageEventRecord &e)
{
    if (!m_open)
        return closedStatus();
    if (e.sessionUuid.isEmpty() || e.contentId.isEmpty() || !e.startUtc.isValid() || e.page < 0
        || e.totalPages < 0 || e.durationSeconds < 0)
        return invalid(QStringLiteral("appendPageEvent: invalid argument"));
    const qint64 start = ledgerdb::toUnix(e.startUtc);

    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT content_id, state, start_utc, end_utc,"
                             " COALESCE((SELECT bm.stats_watermark FROM book_map bm"
                             " WHERE bm.content_id = session.content_id), 0)"
                             " FROM session WHERE uuid = ?"));
    q.addBindValue(e.sessionUuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read session");
    if (!q.next())
        return invalid(QStringLiteral("page event for unknown session %1").arg(e.sessionUuid));
    if (q.value(0).toString() != e.contentId)
        return invalid(
                QStringLiteral("page event for another book than session %1").arg(e.sessionUuid));
    const int state = q.value(1).toInt();
    if (state == stateToDb(SessionState::Dropped))
        return invalid(QStringLiteral("session %1 was dropped").arg(e.sessionUuid));
    const QDateTime sessionStart = ledgerdb::fromDb(q.value(2));
    const QDateTime sessionEnd = ledgerdb::fromDb(q.value(3));
    const qint64 watermark = q.value(4).toLongLong();

    // BookOrbit keys page events on page + start second; so does the ledger.
    q.prepare(QStringLiteral("SELECT 1 FROM page_event WHERE content_id = ? AND page = ?"
                             " AND start_unix = ?"));
    q.addBindValue(e.contentId);
    q.addBindValue(e.page);
    q.addBindValue(qlonglong(start));
    if (!q.exec())
        return sqlFailure(q.lastError(), "check page event");
    if (q.next())
        return invalid(
                QStringLiteral("page event %1 at %2 already recorded").arg(e.page).arg(start));

    // Backstops (ledger.h, "Same-second rule"). An event at or below the
    // watermark would be ignored by BookOrbit and never counted there; an
    // event outside a Finished session's span can't belong to it. Both point
    // at a recorder bug, so they are logged.
    if (start <= watermark) {
        const QString message = QStringLiteral("page event %1 at %2 of %3 is at or below the"
                                               " book's watermark %4; refused")
                                        .arg(e.page)
                                        .arg(start)
                                        .arg(e.contentId)
                                        .arg(watermark);
        log::warn(kLog, message);
        return invalid(message);
    }
    if (state == stateToDb(SessionState::Finished)) {
        // Whole seconds, as the event is stored: [start second, end rounded up].
        const qint64 spanStart = ledgerdb::toUnix(sessionStart);
        const qint64 spanEnd = ledgerdb::toUnix(sessionEnd.addMSecs(999));
        if (start < spanStart || start + e.durationSeconds > spanEnd) {
            const QString message = QStringLiteral("page event %1 at %2 (%3 s) is outside finished"
                                                   " session %4 (%5 to %6); refused")
                                            .arg(e.page)
                                            .arg(start)
                                            .arg(e.durationSeconds)
                                            .arg(e.sessionUuid)
                                            .arg(spanStart)
                                            .arg(spanEnd);
            log::warn(kLog, message);
            return invalid(message);
        }
    }

    // One statement: a crash leaves the event recorded or not, never half.
    q.prepare(QStringLiteral("INSERT INTO page_event (session_uuid, content_id, page, total_pages,"
                             " start_unix, duration_seconds, created_utc)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(e.sessionUuid);
    q.addBindValue(e.contentId);
    q.addBindValue(e.page);
    q.addBindValue(e.totalPages);
    q.addBindValue(qlonglong(start));
    q.addBindValue(e.durationSeconds);
    q.addBindValue(ledgerdb::toDb(nowUtc()));
    if (!q.exec())
        return sqlFailure(q.lastError(), "append page event");
    return Status::success();
}

QVector<PageEventRecord> SqlLedger::pageEventsInRange(const QDateTime &fromUtc,
                                                      const QDateTime &toUtc,
                                                      QVector<bool> *confirmed)
{
    QVector<PageEventRecord> out;
    if (confirmed)
        confirmed->clear();
    if (!m_open || !fromUtc.isValid() || !toUtc.isValid() || !(fromUtc < toUtc))
        return out;
    // Compared in milliseconds, so a range that doesn't start on a whole
    // second still means [fromUtc, toUtc). Confirmed: at or below the
    // watermark of the book's row, linked or not (0 without one).
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
            "SELECT pe.session_uuid, pe.content_id, pe.page, pe.total_pages, pe.start_unix,"
            " pe.duration_seconds, pe.start_unix <= COALESCE((SELECT bm.stats_watermark"
            " FROM book_map bm WHERE bm.content_id = pe.content_id), 0)"
            " FROM page_event pe JOIN session s ON s.uuid = pe.session_uuid"
            " WHERE s.state <> 2 AND pe.start_unix * 1000 >= ? AND pe.start_unix * 1000 < ?"
            " ORDER BY pe.start_unix, pe.content_id, pe.page, pe.id"));
    q.addBindValue(ledgerdb::toDb(fromUtc));
    q.addBindValue(ledgerdb::toDb(toUtc));
    if (!q.exec()) {
        sqlFailure(q.lastError(), "read page events in range");
        return out;
    }
    while (q.next()) {
        PageEventRecord e;
        e.sessionUuid = q.value(0).toString();
        e.contentId = q.value(1).toString();
        e.page = q.value(2).toInt();
        e.totalPages = q.value(3).toInt();
        e.startUtc = ledgerdb::fromUnix(q.value(4).toLongLong());
        e.durationSeconds = q.value(5).toInt();
        out << e;
        if (confirmed)
            *confirmed << (q.value(6).toInt() != 0);
    }
    return out;
}

QVector<PageEventRecord> SqlLedger::pageEventsAfter(const QString &contentId,
                                                    qint64 afterUnixSeconds, int limit)
{
    QVector<PageEventRecord> out;
    if (!m_open || limit < 0)
        return out;
    // Events of Dropped sessions are kept but never sent.
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
            "SELECT pe.session_uuid, pe.content_id, pe.page, pe.total_pages, pe.start_unix,"
            " pe.duration_seconds FROM page_event pe JOIN session s ON s.uuid = pe.session_uuid"
            " WHERE pe.content_id = ? AND pe.start_unix > ? AND s.state <> 2"
            " ORDER BY pe.start_unix, pe.page, pe.id LIMIT ?"));
    q.addBindValue(contentId);
    q.addBindValue(qlonglong(afterUnixSeconds));
    q.addBindValue(limit == 0 ? -1 : limit);
    if (!q.exec()) {
        sqlFailure(q.lastError(), "read page events");
        return out;
    }
    while (q.next()) {
        PageEventRecord e;
        e.sessionUuid = q.value(0).toString();
        e.contentId = q.value(1).toString();
        e.page = q.value(2).toInt();
        e.totalPages = q.value(3).toInt();
        e.startUtc = ledgerdb::fromUnix(q.value(4).toLongLong());
        e.durationSeconds = q.value(5).toInt();
        out << e;
    }
    return out;
}

QStringList SqlLedger::booksWithPendingPageEvents()
{
    QStringList out;
    if (!m_open)
        return out;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT pe.content_id FROM")
                + QLatin1String(ledgerdb::kPendingPageEventsFromWhere)
                + QStringLiteral("GROUP BY pe.content_id"
                                 " ORDER BY MIN(pe.start_unix), pe.content_id"))) {
        sqlFailure(q.lastError(), "read pending books");
        return out;
    }
    while (q.next())
        out << q.value(0).toString();
    return out;
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
                             " remote_locator, remote_percent, remote_updated_utc, remote_device,"
                             " uuid FROM progress WHERE content_id = ?"));
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
    out->uuid = q.value(7).toString();
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
                             " rating_local_utc, rating_remote_utc, uuid FROM book_state"
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
    out->uuid = q.value(6).toString();
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
    if (e.contentId.isEmpty() || e.bookId <= 0 || originToDb(e.origin) < 0 || e.statsWatermark < 0)
        return invalid(QStringLiteral("putBookMap: invalid argument"));
    QSqlDatabase d = db();
    Transaction t(d);
    Status s = t.begin("begin");
    if (!s.ok())
        return s;
    // The watermark survives a re-link: page events it covers were sent from
    // this device already and must not go again.
    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT stats_watermark FROM book_map WHERE content_id = ?"));
    q.addBindValue(e.contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read book map");
    const qint64 watermark =
            q.next() ? qMax(q.value(0).toLongLong(), e.statsWatermark) : e.statsWatermark;
    q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO book_map (content_id, book_id, book_file_id, partial_md5,"
            " origin, created_utc, unlinked_utc, stats_watermark)"
            " VALUES (?, ?, ?, ?, ?, ?, NULL, ?)"));
    q.addBindValue(e.contentId);
    q.addBindValue(qlonglong(e.bookId));
    q.addBindValue(qlonglong(e.bookFileId));
    q.addBindValue(e.partialMd5);
    q.addBindValue(originToDb(e.origin));
    q.addBindValue(ledgerdb::toDb(e.createdUtc.isValid() ? e.createdUtc : nowUtc()));
    q.addBindValue(qlonglong(watermark));
    if (!q.exec())
        return sqlFailure(q.lastError(), "put book map");
    return t.commit("commit");
}

Status SqlLedger::bookMapWhere(const QString &where, const QVariant &value, BookMapEntry *out)
{
    if (!out)
        return invalid(QStringLiteral("book map: no output"));
    *out = BookMapEntry();
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    q.prepare(
            QStringLiteral(
                    "SELECT content_id, book_id, book_file_id, partial_md5, origin,"
                    " created_utc, stats_watermark FROM book_map WHERE %1 AND unlinked_utc IS NULL"
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
    out->statsWatermark = q.value(6).toLongLong();
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

Status SqlLedger::setStatsWatermark(const QString &contentId, qint64 unixSeconds)
{
    if (!m_open)
        return closedStatus();
    if (contentId.isEmpty() || unixSeconds < 0)
        return invalid(QStringLiteral("setStatsWatermark: invalid argument"));
    // Forward only: a smaller value leaves the row as it is. Unlinked rows
    // keep their watermark too.
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE book_map SET stats_watermark = ? WHERE content_id = ?"
                             " AND stats_watermark < ?"));
    q.addBindValue(qlonglong(unixSeconds));
    q.addBindValue(contentId);
    q.addBindValue(qlonglong(unixSeconds));
    if (!q.exec())
        return sqlFailure(q.lastError(), "set watermark");
    if (q.numRowsAffected() > 0)
        return Status::success();
    q.prepare(QStringLiteral("SELECT 1 FROM book_map WHERE content_id = ?"));
    q.addBindValue(contentId);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read book map");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("book not mapped"));
    return Status::success();
}

// ---------------------------------------------------------------------------
// remote_cache (disposable; the v1 table's cache_key, kind, value, updated_utc)
// ---------------------------------------------------------------------------

Status SqlLedger::cacheGet(const QString &key, QByteArray *value, QDateTime *updatedUtc)
{
    if (value)
        value->clear();
    if (updatedUtc)
        *updatedUtc = QDateTime();
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT value, updated_utc FROM remote_cache WHERE cache_key = ?"));
    q.addBindValue(key);
    if (!q.exec())
        return sqlFailure(q.lastError(), "read cache");
    if (!q.next())
        return Status::error(StatusCode::NotFound, QStringLiteral("not cached: %1").arg(key));
    if (value)
        *value = q.value(0).toByteArray();
    if (updatedUtc)
        *updatedUtc = ledgerdb::fromDb(q.value(1));
    return Status::success();
}

Status SqlLedger::cachePut(const QString &key, const QString &kind, const QByteArray &value,
                           const QDateTime &updatedUtc)
{
    if (!m_open)
        return closedStatus();
    if (key.isEmpty() || kind.isEmpty())
        return invalid(QStringLiteral("cachePut: no key or kind"));
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO remote_cache (cache_key, kind, value,"
                             " updated_utc) VALUES (?, ?, ?, ?)"));
    q.addBindValue(key);
    q.addBindValue(kind);
    q.addBindValue(value);
    q.addBindValue(ledgerdb::toDb(updatedUtc.isValid() ? updatedUtc : nowUtc()));
    if (!q.exec())
        return sqlFailure(q.lastError(), "write cache");
    return Status::success();
}

Status SqlLedger::cacheClear(const QString &kind)
{
    if (!m_open)
        return closedStatus();
    QSqlQuery q(db());
    if (kind.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM remote_cache"));
    } else {
        q.prepare(QStringLiteral("DELETE FROM remote_cache WHERE kind = ?"));
        q.addBindValue(kind);
    }
    if (!q.exec())
        return sqlFailure(q.lastError(), "clear cache");
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

} // namespace ledgerdb
} // namespace smalt
