// File handling for the ledger: header check, renaming a corrupt file aside,
// daily backups and their rotation. Internal to src/core/ledger/.
//
// SPEC: Crash safety ("Ledger protection"). CLAUDE.md hard rule 3: a ledger
// file is never deleted, only renamed. The one exception is backup rotation,
// which removes the oldest *copies* beyond kBackupsKept, as the brief asks.
#pragma once

#include "smalt/status.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace smalt {
namespace ledgerfiles {

enum class FileState {
    Missing, // No file: a fresh ledger.
    Empty, // Zero bytes: SQLite treats it as an empty database.
    LooksLikeSqlite, // Has the SQLite header; the integrity check decides.
    NotSqlite, // Readable, but not a SQLite database: corrupt.
    Unreadable, // Can't be read at all (I/O error, permissions): not corrupt.
};

FileState inspect(const QString &path);

// Renames `path` (and its -journal, if any) to
// <path>.corrupt-<yyyyMMddTHHmmssZ>[-<n>]. Returns the new name in *renamedTo.
Status quarantine(const QString &path, const QDateTime &nowUtc, QString *renamedTo);

// <backupDir>/ledger-yyyyMMdd.sqlite
QString backupPath(const QString &backupDir, const QDate &day);

// Backup files in `backupDir`, oldest first (by the date in the name).
QStringList backups(const QString &backupDir);

// Copies `from` to `to` through `to`.tmp, fsyncs, then renames into place.
Status copyDurably(const QString &from, const QString &to);

// Removes the oldest backups beyond `keep`. Failures are logged, not returned.
void rotateBackups(const QString &backupDir, int keep);

} // namespace ledgerfiles
} // namespace smalt
