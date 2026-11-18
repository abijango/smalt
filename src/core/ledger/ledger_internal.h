// Internal to task 10 (src/core/ledger/ and src/core/outbox/). Not a shared
// interface: other modules use include/smalt/ledger.h and outbox.h only.
//
// The outbox is a table in the ledger file, so the outbox module reaches the
// ledger's open connection through ledgerdb::database(). Tests also use these
// helpers to build databases at earlier schema versions and to look at the
// connection's settings.
#pragma once

#include "smalt/ledger.h"
#include "smalt/status.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>
#include <QVariant>

namespace smalt {
namespace ledgerdb {

// Current schema version, kept in PRAGMA user_version. Version 0 is an empty
// file. Every change to the schema adds a migration in schema.cpp and bumps
// this number; never edit an existing migration.
constexpr int kSchemaVersion = 2;

// Daily backups kept in Paths::backupDir() (SPEC, Crash safety, "Ledger protection").
constexpr int kBackupsKept = 7;

// First retry delay for a failed outbox item; doubles per attempt up to
// kOutboxMaxBackoffSeconds. The spec gives only the cap (Sync engine, "Networking").
constexpr int kOutboxFirstBackoffSeconds = 30;

// SQLite primary result codes the ledger cares about.
constexpr int kSqliteBusy = 5;
constexpr int kSqliteLocked = 6;
constexpr int kSqliteCorrupt = 11;
constexpr int kSqliteNotADatabase = 26;

// The SQLite primary result code behind a QSQLITE error, or -1 if there is
// none. Works with Qt 5.2 (QSqlError::number()) and later (nativeErrorCode()).
int sqliteErrorCode(const QSqlError &error);

// True only for SQLITE_CORRUPT and SQLITE_NOTADB. I/O errors, locks and
// everything else are not corruption and never get a ledger renamed.
bool isCorruptionError(const QSqlError &error);

// A lowercase UUID without braces, made on the device.
QString newUuid();

// Timestamps are stored as UTC milliseconds since the epoch; NULL for a null
// QDateTime.
QVariant toDb(const QDateTime &utc);
QDateTime fromDb(const QVariant &value);

// PRAGMA user_version, or -1 when it can't be read.
int schemaVersion(QSqlDatabase db);

// Applies migrations from the database's current version up to
// `targetVersion` (<= kSchemaVersion), each in its own transaction.
Status migrate(QSqlDatabase db, int targetVersion);

// The open connection of a ledger made by createLedger(), or an invalid
// QSqlDatabase when it is closed (USB connected) or not a SQLite ledger.
QSqlDatabase database(Ledger *ledger);

// Unix seconds, as page events and the watermark use them (no
// QDateTime::toSecsSinceEpoch in Qt 5.2).
qint64 toUnix(const QDateTime &utc);
QDateTime fromUnix(qint64 seconds);

// FROM ... WHERE clause (aliases pe for page_event, s for session) selecting
// page events above their book's watermark: what still has to be uploaded.
// Events of Dropped sessions never count. A book without a book_map row has
// watermark 0. Shared by the ledger and the outbox's Position hold.
extern const char kPendingPageEventsFromWhere[];

} // namespace ledgerdb
} // namespace smalt
