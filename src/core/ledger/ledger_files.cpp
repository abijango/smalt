#include "ledger_files.h"

#include "smalt/log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>

#include <unistd.h>

namespace smalt {
namespace ledgerfiles {
namespace {

const char kHeader[] = "SQLite format 3"; // Followed by a NUL: 16 bytes.
constexpr int kHeaderSize = 16;
constexpr int kMinDatabaseSize = 100; // The database header alone is 100 bytes.

QString journalOf(const QString &path)
{
    return path + QStringLiteral("-journal");
}

} // namespace

FileState inspect(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists())
        return FileState::Missing;
    if (!info.isFile())
        return FileState::Unreadable;
    if (info.size() == 0)
        return FileState::Empty;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return FileState::Unreadable;
    const QByteArray head = f.read(kHeaderSize);
    if (head.size() < kHeaderSize && f.error() != QFile::NoError)
        return FileState::Unreadable;
    if (info.size() < kMinDatabaseSize || head != QByteArray(kHeader, kHeaderSize))
        return FileState::NotSqlite;
    return FileState::LooksLikeSqlite;
}

Status quarantine(const QString &path, const QDateTime &nowUtc, QString *renamedTo)
{
    const QString stamp = nowUtc.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    const QString base = path + QStringLiteral(".corrupt-") + stamp;
    QString target = base;
    for (int n = 2; QFile::exists(target) || QFile::exists(journalOf(target)); ++n) {
        if (n > 1000)
            return Status::error(StatusCode::IoError,
                                 QStringLiteral("no free name to rename %1").arg(path));
        target = base + QStringLiteral("-%1").arg(n);
    }

    // The journal first: if it stayed behind, SQLite would replay it into the
    // fresh ledger.
    const QString journal = journalOf(path);
    if (QFile::exists(journal) && !QFile::rename(journal, journalOf(target)))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("can't rename %1 aside").arg(journal));
    if (QFile::exists(path) && !QFile::rename(path, target))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("can't rename %1 aside").arg(path));
    if (renamedTo)
        *renamedTo = target;
    return Status::success();
}

QString backupPath(const QString &backupDir, const QDate &day)
{
    return backupDir + QStringLiteral("/ledger-") + day.toString(QStringLiteral("yyyyMMdd"))
            + QStringLiteral(".sqlite");
}

QStringList backups(const QString &backupDir)
{
    const QRegExp pattern(QStringLiteral("ledger-\\d{8}\\.sqlite"));
    QStringList out;
    const QStringList names = QDir(backupDir).entryList(
            QStringList() << QStringLiteral("ledger-*.sqlite"), QDir::Files, QDir::Name);
    for (const QString &name : names) {
        if (pattern.exactMatch(name))
            out << backupDir + QLatin1Char('/') + name;
    }
    return out; // yyyyMMdd sorts by name.
}

Status copyDurably(const QString &from, const QString &to)
{
    const QString tmp = to + QStringLiteral(".tmp");
    // A .tmp is a half-written copy from an interrupted backup, never a ledger.
    if (QFile::exists(tmp) && !QFile::remove(tmp))
        return Status::error(StatusCode::IoError, QStringLiteral("can't clear %1").arg(tmp));

    QFile in(from);
    if (!in.open(QIODevice::ReadOnly))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("can't read %1: %2").arg(from, in.errorString()));
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("can't write %1: %2").arg(tmp, out.errorString()));
    bool ok = true;
    while (ok && !in.atEnd()) {
        const QByteArray chunk = in.read(64 * 1024);
        if (chunk.isEmpty() && in.error() != QFile::NoError)
            ok = false;
        else
            ok = out.write(chunk) == chunk.size();
    }
    ok = ok && out.flush() && ::fsync(out.handle()) == 0;
    out.close();
    in.close();
    if (!ok || out.error() != QFile::NoError) {
        QFile::remove(tmp);
        return Status::error(StatusCode::IoError, QStringLiteral("copy to %1 failed").arg(tmp));
    }
    if (QFile::exists(to) && !QFile::remove(to)) {
        QFile::remove(tmp);
        return Status::error(StatusCode::IoError, QStringLiteral("can't replace %1").arg(to));
    }
    if (!QFile::rename(tmp, to)) {
        QFile::remove(tmp);
        return Status::error(StatusCode::IoError, QStringLiteral("can't rename to %1").arg(to));
    }
    return Status::success();
}

void rotateBackups(const QString &backupDir, int keep)
{
    QStringList all = backups(backupDir);
    while (all.size() > keep) {
        const QString oldest = all.takeFirst();
        if (QFile::remove(oldest))
            log::info("ledger", QStringLiteral("removed old backup %1").arg(oldest));
        else
            log::warn("ledger", QStringLiteral("can't remove old backup %1").arg(oldest));
    }
}

} // namespace ledgerfiles
} // namespace smalt
