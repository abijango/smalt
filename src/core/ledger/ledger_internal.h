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
#include <QString>
#include <QVariant>

namespace smalt {
namespace ledgerdb {

// Current schema version, kept in PRAGMA user_version. Version 0 is an empty
// file. Every change to the schema adds a migration in schema.cpp and bumps
// this number; never edit an existing migration.
constexpr int kSchemaVersion = 1;

// Daily backups kept in Paths::backupDir() (SPEC, Crash safety, "Ledger protection").
constexpr int kBackupsKept = 7;

// First retry delay for a failed outbox item; doubles per attempt up to
// kOutboxMaxBackoffSeconds. The spec gives only the cap (Sync engine, "Networking").
constexpr int kOutboxFirstBackoffSeconds = 30;

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

// Corrects the end of a session that hasn't been sent yet (SPEC, "Sleeping
// inside a book": an approximate end position is corrected at the next sleep
// or close). InvalidArgument once the session has been handed to the outbox
// for upload; NotFound for an unknown UUID. Proposed for include/smalt/ledger.h.
Status correctSessionEnd(Ledger *ledger, const QString &uuid, const Position &end,
                         bool approximate);

} // namespace ledgerdb
} // namespace smalt
