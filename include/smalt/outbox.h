// The outbox: positions, highlights and statuses that still have to reach
// BookOrbit.
//
// Spec: On-device data model ("Tables": outbox), Sync engine ("Order of a
// full sync", "Conflict rules", "Networking": backoff). Implemented by task 10
// in src/core/outbox/. The outbox is a table in the ledger file.
//
// Reading itself doesn't go through the outbox: page events are uploaded per
// book from the ledger, by watermark (include/smalt/ledger.h), before any of
// the items here.
//
// Rules: nothing is removed until the server confirms it (CLAUDE.md hard
// rule 3). Batches come out oldest first, in kind order (positions, then
// annotations, then status), at most kOutboxBatchSize items. A book's Position
// items are held back while that book has page events above its watermark
// (SPEC: a position never goes before its book's unsent reading). Failed items
// retry with exponential backoff, capped at kOutboxMaxBackoffSeconds.
#pragma once

#include "smalt/status.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <memory>

namespace smalt {

class Ledger;

constexpr int kOutboxBatchSize = 20; // "Batches of 20; each batch is one request."
constexpr int kOutboxMaxBackoffSeconds = 6 * 60 * 60; // "capped at 6 hours"

// Push order within a sync. Lower values go first. (Value 0 was Session before
// page events; it is no longer used.)
enum class OutboxKind {
    Position = 1,
    Annotation = 2,
    Status = 3, // Status and rating.
};

struct OutboxItem
{
    qint64 id = 0; // Row ID, for confirm/fail.
    OutboxKind kind = OutboxKind::Position;
    QString entityUuid; // UUID of the ledger row this uploads.
    QString contentId; // Book, so "book closed" can push just that book.
    QByteArray payload; // Serialised request body fragment (JSON).
    int attempts = 0;
    QDateTime createdUtc;
    QDateTime nextRetryUtc; // Null means "ready now".
};

class Outbox
{
public:
    virtual ~Outbox() = default;

    // Queues one item. Re-enqueueing the same kind + entityUuid replaces the
    // payload of the unconfirmed item instead of adding a second one.
    virtual Status enqueue(OutboxKind kind, const QString &entityUuid, const QString &contentId,
                           const QByteArray &payload) = 0;

    // Up to `max` items of a single kind that are ready at `nowUtc`, from the
    // lowest kind that has any, oldest first. Empty `contentId` = any book.
    // Skips Position items of books with page events above their watermark.
    virtual QVector<OutboxItem> nextBatch(const QDateTime &nowUtc, int max = kOutboxBatchSize,
                                          const QString &contentId = QString()) = 0;

    // Removes items the server confirmed. An item whose payload changed after
    // it was handed out stays queued with the newer payload.
    virtual Status markConfirmed(const QVector<qint64> &ids) = 0;

    // Counts an attempt and schedules the next retry with backoff.
    virtual Status markFailed(const QVector<qint64> &ids, const QDateTime &nowUtc) = 0;

    // For "3 waiting" in the header: outbox items plus unconfirmed sessions.
    virtual int pendingCount() = 0;
};

// Makes the outbox over an open ledger. Defined by task 10 (src/core/outbox/).
std::unique_ptr<Outbox> createOutbox(Ledger *ledger);

} // namespace smalt
