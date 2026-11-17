// The ledger schema and its versioned migrations.
//
// SPEC: On-device data model ("Tables"). The version lives in PRAGMA
// user_version. Each migration runs in one transaction together with the
// version bump, so a crash leaves the file at the old version or the new one,
// never between. Migrations are append-only: never edit a released one, add
// the next.
//
// SQL stays within what the SQLite bundled with Qt 5.2 understands (no
// UPSERT, no WITHOUT ROWID, no partial indexes, no RETURNING).
#include "ledger_internal.h"

#include "smalt/log.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace smalt {
namespace ledgerdb {
namespace {

// Version 1: every table in the spec.
//
// Times are UTC milliseconds since the epoch. Enums are stored as the
// integers documented next to each column; ledger.cpp maps them explicitly.
const char *const kVersion1[] = {
    // One row: server URL, this Kobo's device ID, sync cursors. Credentials
    // stay in config.ini.
    "CREATE TABLE server ("
    " id INTEGER PRIMARY KEY CHECK (id = 1),"
    " device_id TEXT NOT NULL,"
    " server_url TEXT,"
    " pull_cursor TEXT,"
    " last_pull_utc INTEGER,"
    " last_sync_utc INTEGER,"
    " created_utc INTEGER NOT NULL)",

    // Nickel ContentID <-> BookOrbit IDs. origin: 0 downloaded, 1 matched,
    // 2 migrated. Unlinked rows are kept with unlinked_utc set.
    "CREATE TABLE book_map ("
    " content_id TEXT PRIMARY KEY NOT NULL,"
    " book_id INTEGER NOT NULL,"
    " book_file_id INTEGER NOT NULL DEFAULT 0,"
    " partial_md5 TEXT,"
    " origin INTEGER NOT NULL DEFAULT 0,"
    " created_utc INTEGER,"
    " unlinked_utc INTEGER)",
    "CREATE INDEX book_map_book_id ON book_map (book_id)",

    // Reading sessions. Append-only: no deletes ever; once sent_utc is set
    // (handed out for upload) the reading data is frozen. confirmed_utc is
    // set when the server confirms it.
    "CREATE TABLE session ("
    " uuid TEXT PRIMARY KEY NOT NULL,"
    " content_id TEXT NOT NULL,"
    " start_utc INTEGER NOT NULL,"
    " end_utc INTEGER NOT NULL,"
    " active_seconds INTEGER NOT NULL,"
    " start_locator TEXT,"
    " start_percent REAL,"
    " end_locator TEXT,"
    " end_percent REAL,"
    " pages_turned INTEGER NOT NULL DEFAULT 0,"
    " estimated INTEGER NOT NULL DEFAULT 0,"
    " approximate INTEGER NOT NULL DEFAULT 0,"
    " created_utc INTEGER NOT NULL,"
    " sent_utc INTEGER,"
    " confirmed_utc INTEGER)",
    "CREATE INDEX session_book ON session (content_id, start_utc)",
    "CREATE TRIGGER session_no_delete BEFORE DELETE ON session"
    " BEGIN SELECT RAISE(ABORT, 'session rows are append-only'); END",
    "CREATE TRIGGER session_fixed_identity"
    " BEFORE UPDATE OF uuid, content_id, start_utc, created_utc ON session"
    " BEGIN SELECT RAISE(ABORT, 'a session''s identity never changes'); END",
    "CREATE TRIGGER session_frozen_once_sent"
    " BEFORE UPDATE OF end_utc, active_seconds, start_locator, start_percent, end_locator,"
    " end_percent, pages_turned, estimated, approximate ON session"
    " WHEN OLD.sent_utc IS NOT NULL"
    " BEGIN SELECT RAISE(ABORT, 'session already sent'); END",
    "CREATE TRIGGER session_sent_is_final BEFORE UPDATE OF sent_utc ON session"
    " WHEN OLD.sent_utc IS NOT NULL AND (NEW.sent_utc IS NULL OR NEW.sent_utc <> OLD.sent_utc)"
    " BEGIN SELECT RAISE(ABORT, 'session already sent'); END",

    // Latest local position per book and the latest known remote one.
    "CREATE TABLE progress ("
    " content_id TEXT PRIMARY KEY NOT NULL,"
    " uuid TEXT NOT NULL UNIQUE,"
    " local_locator TEXT,"
    " local_percent REAL,"
    " local_updated_utc INTEGER,"
    " remote_locator TEXT,"
    " remote_percent REAL,"
    " remote_updated_utc INTEGER,"
    " remote_device TEXT)",

    // Highlights and notes; deleted_utc set = tombstone until confirmed.
    "CREATE TABLE annotation ("
    " uuid TEXT PRIMARY KEY NOT NULL,"
    " remote_id TEXT,"
    " content_id TEXT NOT NULL,"
    " anchor_start TEXT,"
    " anchor_end TEXT,"
    " text TEXT,"
    " context TEXT,"
    " note TEXT,"
    " colour TEXT,"
    " created_utc INTEGER,"
    " updated_utc INTEGER,"
    " deleted_utc INTEGER)",
    "CREATE INDEX annotation_book ON annotation (content_id)",

    // Status (0 unknown, 1 reading, 2 finished, 3 abandoned) and rating
    // (0 none, 1..5), each with local and remote update times.
    "CREATE TABLE book_state ("
    " content_id TEXT PRIMARY KEY NOT NULL,"
    " uuid TEXT NOT NULL UNIQUE,"
    " status INTEGER NOT NULL DEFAULT 0,"
    " status_local_utc INTEGER,"
    " status_remote_utc INTEGER,"
    " rating INTEGER NOT NULL DEFAULT 0 CHECK (rating BETWEEN 0 AND 5),"
    " rating_local_utc INTEGER,"
    " rating_remote_utc INTEGER)",

    // Pending uploads (kind: 0 session, 1 position, 2 annotation, 3 status).
    // One row per kind + entity. payload_rev counts payload replacements;
    // sent_rev is the revision last handed out, so a confirm only removes
    // the row if nothing changed while it was in flight.
    "CREATE TABLE outbox ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " kind INTEGER NOT NULL,"
    " entity_uuid TEXT NOT NULL,"
    " content_id TEXT,"
    " payload BLOB,"
    " attempts INTEGER NOT NULL DEFAULT 0,"
    " created_utc INTEGER NOT NULL,"
    " next_retry_utc INTEGER,"
    " payload_rev INTEGER NOT NULL DEFAULT 1,"
    " sent_rev INTEGER NOT NULL DEFAULT 0,"
    " UNIQUE (kind, entity_uuid))",
    "CREATE INDEX outbox_ready ON outbox (kind, next_retry_utc)",

    // Browser cache: collections, catalogue pages, cover thumbnails.
    // Disposable; safe to wipe.
    "CREATE TABLE remote_cache ("
    " cache_key TEXT PRIMARY KEY NOT NULL,"
    " kind TEXT NOT NULL,"
    " value BLOB,"
    " updated_utc INTEGER)",

    // Books removed upstream. reason: 0 deleted upstream, 1 left collection.
    // choice: 0 undecided, 1 remove from Kobo, 2 keep unlinked, 3 decide later.
    "CREATE TABLE pending_decision ("
    " content_id TEXT PRIMARY KEY NOT NULL,"
    " reason INTEGER NOT NULL,"
    " choice INTEGER NOT NULL DEFAULT 0,"
    " created_utc INTEGER,"
    " resolved_utc INTEGER)",
};

struct Migration
{
    int toVersion;
    const char *const *statements;
    int count;
};

const Migration kMigrations[] = {
    { 1, kVersion1, int(sizeof(kVersion1) / sizeof(kVersion1[0])) },
};

static_assert(sizeof(kMigrations) / sizeof(kMigrations[0]) == kSchemaVersion,
              "one migration per schema version");

} // namespace

QString newUuid()
{
    // Qt 5.2 has no QUuid::WithoutBraces.
    QString s = QUuid::createUuid().toString().toLower();
    if (s.startsWith(QLatin1Char('{')) && s.endsWith(QLatin1Char('}')))
        s = s.mid(1, s.size() - 2);
    return s;
}

QVariant toDb(const QDateTime &utc)
{
    if (!utc.isValid())
        return QVariant();
    return QVariant(qlonglong(utc.toMSecsSinceEpoch()));
}

QDateTime fromDb(const QVariant &value)
{
    if (value.isNull() || !value.isValid())
        return QDateTime();
    return QDateTime::fromMSecsSinceEpoch(value.toLongLong()).toUTC();
}

int schemaVersion(QSqlDatabase db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return -1;
    return q.value(0).toInt();
}

Status migrate(QSqlDatabase db, int targetVersion)
{
    if (targetVersion < 0 || targetVersion > kSchemaVersion)
        return Status::error(StatusCode::InvalidArgument,
                             QStringLiteral("unknown schema version %1").arg(targetVersion));
    const int current = schemaVersion(db);
    if (current < 0)
        return Status::error(
                StatusCode::IoError,
                QStringLiteral("can't read schema version: %1").arg(db.lastError().text()));
    if (current > kSchemaVersion)
        return Status::error(StatusCode::Unavailable,
                             QStringLiteral("ledger schema %1 is newer than this Smalt (%2)")
                                     .arg(current)
                                     .arg(kSchemaVersion));

    for (const Migration &m : kMigrations) {
        if (m.toVersion <= current || m.toVersion > targetVersion)
            continue;
        if (!db.transaction())
            return Status::error(StatusCode::IoError,
                                 QStringLiteral("migration %1: can't begin: %2")
                                         .arg(m.toVersion)
                                         .arg(db.lastError().text()));
        QString failure;
        {
            QSqlQuery q(db);
            for (int i = 0; i < m.count && failure.isEmpty(); ++i) {
                if (!q.exec(QString::fromLatin1(m.statements[i])))
                    failure = q.lastError().text();
            }
            if (failure.isEmpty()
                && !q.exec(QStringLiteral("PRAGMA user_version = %1").arg(m.toVersion)))
                failure = q.lastError().text();
        }
        if (failure.isEmpty() && !db.commit())
            failure = db.lastError().text();
        if (!failure.isEmpty()) {
            db.rollback();
            return Status::error(
                    StatusCode::IoError,
                    QStringLiteral("migration to %1 failed: %2").arg(m.toVersion).arg(failure));
        }
        log::info("ledger", QStringLiteral("schema migrated to version %1").arg(m.toVersion));
    }
    return Status::success();
}

} // namespace ledgerdb
} // namespace smalt
