// Helpers shared by the task 10 tests (tests/core/).
#pragma once

#include "../../src/core/ledger/ledger_internal.h"
#include "smalt/ledger.h"
#include "smalt/paths.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

namespace coretest {

inline QDateTime utc(int y, int mo, int d, int h = 12, int mi = 0, int s = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s), Qt::UTC);
}

inline smalt::SessionRecord makeSession(const QString &contentId, const QDateTime &start,
                                        int activeSeconds)
{
    smalt::SessionRecord r;
    r.contentId = contentId;
    r.startUtc = start;
    r.endUtc = start.addSecs(activeSeconds + 5);
    r.activeSeconds = activeSeconds;
    r.start.locator = QStringLiteral("OEBPS/ch1.xhtml#p1");
    r.start.percent = 10.0;
    r.end.locator = QStringLiteral("OEBPS/ch2.xhtml#p9");
    r.end.percent = 12.5;
    r.pagesTurned = 4;
    return r;
}

// One string per line: the CREATE statements of every table, index and
// trigger, sorted, for comparing schemas.
inline QStringList schemaOf(QSqlDatabase db)
{
    QStringList out;
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT type, name, sql FROM sqlite_master "
                          "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name"));
    while (q.next())
        out << q.value(0).toString() + QLatin1Char(' ') + q.value(1).toString() + QLatin1Char(' ')
                        + q.value(2).toString().simplified();
    return out;
}

inline int countRows(QSqlDatabase db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next())
        return -1;
    return q.value(0).toInt();
}

} // namespace coretest
