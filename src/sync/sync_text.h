// Pure helpers of the sync engine (task 21): vocabularies, payloads and the
// copy the engine shows. No I/O, so the desktop tests check them directly.
//
// Spec: Status and ratings (the six statuses and Nickel's three), Sync
// engine ("Order of a full sync"), UX details ("Where sync status shows",
// "Copy").
#pragma once

#include "smalt/bookorbit_client.h"
#include "smalt/ledger.h"
#include "smalt/sync.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace smalt {
namespace sync {

// --- Status vocabularies ---

// BookOrbit's read-status values for PUT plugin/catalog/books/{id}/read-status
// (KoreaderCatalogSetReadStatusDto at BookOrbit c77966cc; the same list is in
// src/bookorbit/qt_bookorbit_client.cpp setReadStatus() and the mock's
// SETTABLE_READ_STATUSES): unread, want_to_read, reading, on_hold, read,
// abandoned. Unknown maps to an empty string (nothing to send).
QString bookOrbitStatus(ReadStatus status);
// The reverse; anything else is Unknown.
ReadStatus statusFromBookOrbit(const QString &value);

// Nickel's ReadStatus column: 0 unread, 1 reading, 2 finished. SPEC "Status
// and ratings": Reading and On hold -> reading; Read -> finished; Unread and
// Want to read -> unread; Abandoned has no Nickel equivalent (-1: leave
// Nickel alone). Unknown -> -1.
constexpr int kNickelUnread = 0;
constexpr int kNickelReading = 1;
constexpr int kNickelFinished = 2;
int nickelStatus(ReadStatus status);

// --- Outbox payloads ---

// Position item (OutboxKind::Position): the kosync PUT syncs/progress body
// (VERIFY-FINDINGS item 6). `progress` stays empty: the ledger holds Nickel's
// ChapterIDBookmarked, not an XPointer (conversion is task 31). Device and
// device_id are filled by the client from its config.
QByteArray positionPayload(const QString &partialMd5, double percent0to100, qint64 timestamp);
bool parsePositionPayload(const QByteArray &payload, RemoteProgress *out);

// Status item (OutboxKind::Status): only the book. The values are read from
// the ledger when the item is sent, so a remote value merged after the item
// was queued is never overwritten with an older local one.
QByteArray statusPayload(const QString &contentId, qint64 bookId);

// Annotation item (OutboxKind::Annotation), built by task 32: one exchange
// for one book. {"document", "changes": [...], "keys": [...],
// "keysComplete": bool, "sidecarState": ""} (see bookorbit_client.h
// exchangeAnnotations()).
struct AnnotationExchange
{
    QString document;
    QJsonArray changes;
    QJsonArray keys;
    bool keysComplete = true;
    QString sidecarState;
};

bool parseAnnotationPayload(const QByteArray &payload, AnnotationExchange *out);

// --- Copy ---

// Home screen header (SPEC "Where sync status shows"): "Synced 10:42 · 3
// waiting", "Offline · 3 waiting". `nowLocal` decides between a time (today)
// and a date. Running: "Syncing…".
QString headerText(const SyncStatus &status, const QDateTime &nowLocal);

// "Synced: 2 sessions, 1 highlight, 1 position" (SPEC "Copy").
QString summaryText(const SyncResult &result);

// The toast for a failure (SPEC "Copy": "Repeated failure", "Key rejected").
QString failureText(ErrorKind error);

} // namespace sync
} // namespace smalt
