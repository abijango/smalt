// The outbox (include/smalt/outbox.h): a table in the ledger file.
//
// SPEC: On-device data model ("Tables": outbox, "Removed only after the
// server confirms"); Sync engine ("Order of a full sync", "Networking").
//
// One row per kind + entity UUID. Re-enqueueing replaces the payload and
// bumps payload_rev; nextBatch() records the revision it handed out in
// sent_rev. markConfirmed() removes a row only when the confirmed revision is
// still the current one, so a change made while a batch was in flight is
// never lost. Every change is one short transaction: a crash at any point
// leaves either the old state or the new one.
#include "../ledger/ledger_internal.h"

#include "smalt/log.h"
#include "smalt/outbox.h"

#include <QSqlError>
#include <QSqlQuery>

namespace smalt {
namespace {

const char kLog[] = "outbox";

bool kindOk(OutboxKind kind)
{
    switch (kind) {
    case OutboxKind::Session:
    case OutboxKind::Position:
    case OutboxKind::Annotation:
    case OutboxKind::Status:
        return true;
    }
    return false;
}

Status sqlFailure(const QSqlError &e, const char *what)
{
    const QString text = e.text();
    const bool busy = text.contains(QLatin1String("locked"), Qt::CaseInsensitive)
            || text.contains(QLatin1String("busy"), Qt::CaseInsensitive);
    const QString message = QStringLiteral("%1: %2").arg(QLatin1String(what), text);
    log::warn(kLog, message);
    return Status::error(busy ? StatusCode::Busy : StatusCode::IoError, message);
}

Status closedStatus()
{
    return Status::error(StatusCode::Busy, QStringLiteral("ledger is closed"));
}

// Delay before retry number `attempts` + 1: 30 s, 60 s, 120 s ... capped.
qint64 backoffSeconds(int attempts)
{
    qint64 delay = ledgerdb::kOutboxFirstBackoffSeconds;
    for (int i = 1; i < attempts && delay < kOutboxMaxBackoffSeconds; ++i)
        delay *= 2;
    return qMin<qint64>(delay, kOutboxMaxBackoffSeconds);
}

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

    bool commit()
    {
        if (!m_active || !m_db.commit())
            return false;
        m_active = false;
        return true;
    }

private:
    QSqlDatabase m_db;
    bool m_active;
};

class SqlOutbox : public Outbox
{
public:
    explicit SqlOutbox(Ledger *ledger) : m_ledger(ledger) { }

    Status enqueue(OutboxKind kind, const QString &entityUuid, const QString &contentId,
                   const QByteArray &payload) override;
    QVector<OutboxItem> nextBatch(const QDateTime &nowUtc, int max,
                                  const QString &contentId) override;
    Status markConfirmed(const QVector<qint64> &ids) override;
    Status markFailed(const QVector<qint64> &ids, const QDateTime &nowUtc) override;
    int pendingCount() override;

private:
    // The ledger's connection, or invalid while it's closed (USB).
    QSqlDatabase db() const { return m_ledger ? ledgerdb::database(m_ledger) : QSqlDatabase(); }

    Ledger *m_ledger;
};

Status SqlOutbox::enqueue(OutboxKind kind, const QString &entityUuid, const QString &contentId,
                          const QByteArray &payload)
{
    if (!kindOk(kind) || entityUuid.isEmpty())
        return Status::error(StatusCode::InvalidArgument,
                             QStringLiteral("enqueue: invalid kind or UUID"));
    QSqlDatabase d = db();
    if (!d.isValid())
        return closedStatus();

    Transaction t(d);
    if (!t.active())
        return sqlFailure(d.lastError(), "begin");
    QSqlQuery q(d);

    if (kind == OutboxKind::Session) {
        // A session the server has confirmed never goes out again.
        q.prepare(QStringLiteral("SELECT confirmed_utc FROM session WHERE uuid = ?"));
        q.addBindValue(entityUuid);
        if (!q.exec())
            return sqlFailure(q.lastError(), "check session");
        if (q.next() && !q.value(0).isNull())
            return Status::error(StatusCode::InvalidArgument,
                                 QStringLiteral("session %1 already confirmed").arg(entityUuid));
    }

    q.prepare(QStringLiteral("UPDATE outbox SET payload = ?, content_id = ?,"
                             " payload_rev = payload_rev + 1 WHERE kind = ? AND entity_uuid = ?"));
    q.addBindValue(payload);
    q.addBindValue(contentId);
    q.addBindValue(int(kind));
    q.addBindValue(entityUuid);
    if (!q.exec())
        return sqlFailure(q.lastError(), "replace item");
    if (q.numRowsAffected() < 1) {
        q.prepare(QStringLiteral("INSERT INTO outbox (kind, entity_uuid, content_id, payload,"
                                 " created_utc) VALUES (?, ?, ?, ?, ?)"));
        q.addBindValue(int(kind));
        q.addBindValue(entityUuid);
        q.addBindValue(contentId);
        q.addBindValue(payload);
        q.addBindValue(ledgerdb::toDb(QDateTime::currentDateTimeUtc()));
        if (!q.exec())
            return sqlFailure(q.lastError(), "add item");
    }
    if (!t.commit())
        return sqlFailure(d.lastError(), "commit");
    return Status::success();
}

QVector<OutboxItem> SqlOutbox::nextBatch(const QDateTime &nowUtc, int max, const QString &contentId)
{
    QVector<OutboxItem> out;
    QSqlDatabase d = db();
    if (!d.isValid() || max <= 0 || !nowUtc.isValid())
        return out;

    const QVariant now = ledgerdb::toDb(nowUtc);
    const QString ready = QStringLiteral("(next_retry_utc IS NULL OR next_retry_utc <= ?)")
            + (contentId.isEmpty() ? QString() : QStringLiteral(" AND content_id = ?"));

    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT MIN(kind) FROM outbox WHERE ") + ready);
    q.addBindValue(now);
    if (!contentId.isEmpty())
        q.addBindValue(contentId);
    if (!q.exec() || !q.next()) {
        sqlFailure(q.lastError(), "pick kind");
        return out;
    }
    if (q.value(0).isNull())
        return out; // Nothing ready.
    const int kind = q.value(0).toInt();

    q.prepare(QStringLiteral("SELECT id, kind, entity_uuid, content_id, payload, attempts,"
                             " created_utc, next_retry_utc FROM outbox WHERE kind = ? AND ")
              + ready + QStringLiteral(" ORDER BY id LIMIT ?"));
    q.addBindValue(kind);
    q.addBindValue(now);
    if (!contentId.isEmpty())
        q.addBindValue(contentId);
    q.addBindValue(max);
    if (!q.exec()) {
        sqlFailure(q.lastError(), "read batch");
        return out;
    }
    while (q.next()) {
        OutboxItem item;
        item.id = q.value(0).toLongLong();
        item.kind = OutboxKind(q.value(1).toInt());
        item.entityUuid = q.value(2).toString();
        item.contentId = q.value(3).toString();
        item.payload = q.value(4).toByteArray();
        item.attempts = q.value(5).toInt();
        item.createdUtc = ledgerdb::fromDb(q.value(6));
        item.nextRetryUtc = ledgerdb::fromDb(q.value(7));
        out << item;
    }

    // Remember which revision goes out; freeze sessions from here on (the
    // server may have them even if the reply never arrives).
    Transaction t(d);
    bool ok = t.active();
    for (const OutboxItem &item : out) {
        if (!ok)
            break;
        // For annotations, also note whether this upload carries the
        // deletion, so only a confirmed deletion removes the tombstone.
        q.prepare(QStringLiteral(
                "UPDATE outbox SET sent_rev = payload_rev, sent_tombstone = (kind = ? AND"
                " EXISTS (SELECT 1 FROM annotation WHERE annotation.uuid = outbox.entity_uuid"
                " AND annotation.deleted_utc IS NOT NULL)) WHERE id = ?"));
        q.addBindValue(int(OutboxKind::Annotation));
        q.addBindValue(item.id);
        ok = q.exec();
        if (ok && item.kind == OutboxKind::Session) {
            q.prepare(QStringLiteral("UPDATE session SET sent_utc = ? WHERE uuid = ?"
                                     " AND sent_utc IS NULL"));
            q.addBindValue(now);
            q.addBindValue(item.entityUuid);
            ok = q.exec();
        }
    }
    if (!ok || !t.commit()) {
        sqlFailure(q.lastError().isValid() ? q.lastError() : d.lastError(), "mark batch sent");
        return QVector<OutboxItem>();
    }
    return out;
}

Status SqlOutbox::markConfirmed(const QVector<qint64> &ids)
{
    QSqlDatabase d = db();
    if (!d.isValid())
        return closedStatus();
    if (ids.isEmpty())
        return Status::success();
    const QVariant now = ledgerdb::toDb(QDateTime::currentDateTimeUtc());

    Transaction t(d);
    if (!t.active())
        return sqlFailure(d.lastError(), "begin");
    QSqlQuery q(d);
    int removed = 0;
    for (qint64 id : ids) {
        q.prepare(QStringLiteral("SELECT kind, entity_uuid, payload_rev, sent_rev, sent_tombstone"
                                 " FROM outbox WHERE id = ?"));
        q.addBindValue(id);
        if (!q.exec())
            return sqlFailure(q.lastError(), "read item");
        if (!q.next())
            continue; // Already confirmed: confirming twice is harmless.
        const int kind = q.value(0).toInt();
        const QString uuid = q.value(1).toString();
        const qint64 sentRev = q.value(3).toLongLong();
        const bool current = q.value(2).toLongLong() == sentRev;
        const bool sentDeletion = q.value(4).toInt() != 0;

        if (sentRev == 0)
            continue; // Never handed out: nothing of it can have been confirmed.
        if (!current) {
            // Changed after it was handed out, or never handed out: the
            // newer payload still has to go. The server answered, so retry
            // promptly.
            q.prepare(QStringLiteral("UPDATE outbox SET attempts = 0, next_retry_utc = NULL"
                                     " WHERE id = ?"));
            q.addBindValue(id);
            if (!q.exec())
                return sqlFailure(q.lastError(), "keep changed item");
            continue;
        }

        q.prepare(QStringLiteral("DELETE FROM outbox WHERE id = ?"));
        q.addBindValue(id);
        if (!q.exec())
            return sqlFailure(q.lastError(), "remove item");
        ++removed;

        if (kind == int(OutboxKind::Session)) {
            q.prepare(QStringLiteral("UPDATE session SET confirmed_utc = ? WHERE uuid = ?"
                                     " AND confirmed_utc IS NULL"));
            q.addBindValue(now);
            q.addBindValue(uuid);
            if (!q.exec())
                return sqlFailure(q.lastError(), "confirm session");
        } else if (kind == int(OutboxKind::Annotation) && sentDeletion) {
            // The confirmed upload was the deletion: the tombstone has done
            // its job. A confirmed earlier edit leaves it for the deletion
            // still to be queued. (If the annotation was edited back to life
            // since, deleted_utc is NULL and nothing is removed.)
            q.prepare(QStringLiteral("DELETE FROM annotation WHERE uuid = ?"
                                     " AND deleted_utc IS NOT NULL"));
            q.addBindValue(uuid);
            if (!q.exec())
                return sqlFailure(q.lastError(), "remove tombstone");
        }
    }
    if (!t.commit())
        return sqlFailure(d.lastError(), "commit");
    log::debug(kLog,
               QStringLiteral("%1 of %2 confirmed items removed").arg(removed).arg(ids.size()));
    return Status::success();
}

Status SqlOutbox::markFailed(const QVector<qint64> &ids, const QDateTime &nowUtc)
{
    QSqlDatabase d = db();
    if (!d.isValid())
        return closedStatus();
    if (!nowUtc.isValid())
        return Status::error(StatusCode::InvalidArgument, QStringLiteral("markFailed: no time"));
    if (ids.isEmpty())
        return Status::success();

    Transaction t(d);
    if (!t.active())
        return sqlFailure(d.lastError(), "begin");
    QSqlQuery q(d);
    for (qint64 id : ids) {
        q.prepare(QStringLiteral("SELECT attempts FROM outbox WHERE id = ?"));
        q.addBindValue(id);
        if (!q.exec())
            return sqlFailure(q.lastError(), "read item");
        if (!q.next())
            continue;
        const int attempts = q.value(0).toInt() + 1;
        q.prepare(QStringLiteral("UPDATE outbox SET attempts = ?, next_retry_utc = ?"
                                 " WHERE id = ?"));
        q.addBindValue(attempts);
        q.addBindValue(ledgerdb::toDb(nowUtc.addSecs(backoffSeconds(attempts))));
        q.addBindValue(id);
        if (!q.exec())
            return sqlFailure(q.lastError(), "schedule retry");
    }
    if (!t.commit())
        return sqlFailure(d.lastError(), "commit");
    return Status::success();
}

int SqlOutbox::pendingCount()
{
    QSqlDatabase d = db();
    if (!d.isValid())
        return 0;
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT count(*) FROM outbox")) || !q.next()) {
        sqlFailure(q.lastError(), "count");
        return 0;
    }
    return q.value(0).toInt();
}

} // namespace

std::unique_ptr<Outbox> createOutbox(Ledger *ledger)
{
    return std::unique_ptr<Outbox>(new SqlOutbox(ledger));
}

} // namespace smalt
