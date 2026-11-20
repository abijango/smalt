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
    // the row if nothing changed while it was in flight. sent_tombstone is 1
    // when an annotation item was handed out while its annotation was already
    // deleted, i.e. the upload carried the deletion; only then does a confirm
    // remove the tombstone.
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
    " sent_tombstone INTEGER NOT NULL DEFAULT 0,"
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

// Version 2: page events, open sessions and the page-stats watermark (SPEC,
// "How sessions are captured", "Sleeping inside a book", "Correcting a
// session"; task 10 addendum "after wave 1").
//
// Every v1 row is kept:
// - session: v1 rows were complete sessions of 10 s or more, so they become
//   Finished (state 1). The v1 columns sent_utc and confirmed_utc stay in the
//   table, unused: whether a session is confirmed now comes from its page
//   events and the book's watermark.
// - outbox: rows of kind 0 (v1 "session" items) stay where they are and are
//   ignored by the outbox, which only reads kinds 1..3. Nothing is deleted.
// - book_state.status: v1 stored 0 unknown, 1 reading, 2 finished,
//   3 abandoned; v2 stores BookOrbit's six (see the comment on status below),
//   with Finished becoming Read.
const char *const kVersion2[] = {
    // server: whether a sweep is owed. pull_cursor, last_pull_utc and
    // last_sync_utc already exist.
    "ALTER TABLE server ADD COLUMN sweep_owed INTEGER NOT NULL DEFAULT 0",

    // book_map: the page-stats watermark, Unix seconds (start time of the
    // newest page event that needs no further upload). Only moves forward.
    "ALTER TABLE book_map ADD COLUMN stats_watermark INTEGER NOT NULL DEFAULT 0",
    "CREATE TRIGGER book_map_watermark_forward BEFORE UPDATE OF stats_watermark ON book_map"
    " WHEN NEW.stats_watermark < OLD.stats_watermark"
    " BEGIN SELECT RAISE(ABORT, 'the stats watermark only moves forward'); END",

    // session: state 0 open, 1 finished, 2 dropped (under 10 s; kept, never
    // counted). Pages as in the page events; -1 = unknown.
    "ALTER TABLE session ADD COLUMN state INTEGER NOT NULL DEFAULT 1",
    "ALTER TABLE session ADD COLUMN start_page INTEGER NOT NULL DEFAULT -1",
    "ALTER TABLE session ADD COLUMN end_page INTEGER NOT NULL DEFAULT -1",
    "ALTER TABLE session ADD COLUMN total_pages INTEGER NOT NULL DEFAULT 0",
    "CREATE INDEX session_state ON session (state)",
    // The v1 rule (frozen once handed to the outbox) is replaced by: times and
    // counts change only while the session is open; the end position and the
    // approximate mark can be corrected at any time.
    "DROP TRIGGER session_frozen_once_sent",
    "DROP TRIGGER session_sent_is_final",
    "CREATE TRIGGER session_valid_state BEFORE INSERT ON session"
    " WHEN NEW.state NOT IN (0, 1, 2)"
    " BEGIN SELECT RAISE(ABORT, 'unknown session state'); END",
    "CREATE TRIGGER session_one_open BEFORE INSERT ON session"
    " WHEN NEW.state = 0 AND EXISTS (SELECT 1 FROM session WHERE state = 0)"
    " BEGIN SELECT RAISE(ABORT, 'another session is open'); END",
    "CREATE TRIGGER session_state_final BEFORE UPDATE OF state ON session"
    " WHEN NEW.state <> OLD.state AND (OLD.state <> 0 OR NEW.state NOT IN (1, 2))"
    " BEGIN SELECT RAISE(ABORT, 'a finished or dropped session stays so'); END",
    "CREATE TRIGGER session_times_final"
    " BEFORE UPDATE OF end_utc, active_seconds, start_locator, start_percent, start_page,"
    " end_page, total_pages, pages_turned, estimated ON session"
    " WHEN OLD.state <> 0"
    " BEGIN SELECT RAISE(ABORT, 'a finished session''s times are final'); END",

    // page_event: one per page read. start_unix is Unix seconds, as uploaded
    // (BookOrbit keys page events on page + start second). Append-only.
    "CREATE TABLE page_event ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " session_uuid TEXT NOT NULL,"
    " content_id TEXT NOT NULL,"
    " page INTEGER NOT NULL CHECK (page >= 0),"
    " total_pages INTEGER NOT NULL CHECK (total_pages >= 0),"
    " start_unix INTEGER NOT NULL,"
    " duration_seconds INTEGER NOT NULL CHECK (duration_seconds >= 0),"
    " created_utc INTEGER NOT NULL,"
    " UNIQUE (content_id, page, start_unix))",
    "CREATE INDEX page_event_book ON page_event (content_id, start_unix)",
    "CREATE INDEX page_event_session ON page_event (session_uuid, start_unix)",
    "CREATE TRIGGER page_event_needs_session BEFORE INSERT ON page_event"
    " WHEN NOT EXISTS (SELECT 1 FROM session WHERE uuid = NEW.session_uuid"
    " AND content_id = NEW.content_id AND state <> 2)"
    " BEGIN SELECT RAISE(ABORT, 'page event without an open or finished session'); END",
    "CREATE TRIGGER page_event_no_update BEFORE UPDATE ON page_event"
    " BEGIN SELECT RAISE(ABORT, 'page events are never edited'); END",
    "CREATE TRIGGER page_event_no_delete BEFORE DELETE ON page_event"
    " BEGIN SELECT RAISE(ABORT, 'page events are append-only'); END",

    // book_state.status, v2: 0 unknown, 1 unread, 2 want to read, 3 reading,
    // 4 on hold, 5 read, 6 abandoned.
    "UPDATE book_state SET status = CASE status WHEN 1 THEN 3 WHEN 2 THEN 5 WHEN 3 THEN 6"
    " ELSE 0 END",
    "CREATE TRIGGER book_state_status_insert BEFORE INSERT ON book_state"
    " WHEN NEW.status NOT BETWEEN 0 AND 6"
    " BEGIN SELECT RAISE(ABORT, 'unknown status'); END",
    "CREATE TRIGGER book_state_status_update BEFORE UPDATE OF status ON book_state"
    " WHEN NEW.status NOT BETWEEN 0 AND 6"
    " BEGIN SELECT RAISE(ABORT, 'unknown status'); END",
};

struct Migration
{
    int toVersion;
    const char *const *statements;
    int count;
};

const Migration kMigrations[] = {
    { 1, kVersion1, int(sizeof(kVersion1) / sizeof(kVersion1[0])) },
    { 2, kVersion2, int(sizeof(kVersion2) / sizeof(kVersion2[0])) },
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

qint64 toUnix(const QDateTime &utc)
{
    const qint64 ms = utc.toMSecsSinceEpoch();
    return ms >= 0 ? ms / 1000 : -((-ms + 999) / 1000);
}

QDateTime fromUnix(qint64 seconds)
{
    return QDateTime::fromMSecsSinceEpoch(seconds * 1000).toUTC();
}

const char kPendingPageEventsFromWhere[] =
        " page_event pe JOIN session s ON s.uuid = pe.session_uuid"
        " JOIN book_map bm ON bm.content_id = pe.content_id AND bm.unlinked_utc IS NULL"
        " WHERE s.state <> 2 AND pe.start_unix > bm.stats_watermark ";

const char kSessionHasPendingEvents[] =
        " (session.state <> 2 AND EXISTS (SELECT 1 FROM page_event pe JOIN book_map bm"
        " ON bm.content_id = pe.content_id AND bm.unlinked_utc IS NULL"
        " WHERE pe.session_uuid = session.uuid AND pe.start_unix > bm.stats_watermark)) ";

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
