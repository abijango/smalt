// Uploads reading sessions as page events and tells the server the device
// measures its own sessions (Sync engine, "Order of a full sync", step 1;
// docs/tasks/11-bookorbit-client.md, "Sessions").
//
// The rules (from the KOReader plugin, bookorbit_state.lua applyStatsAck and
// bookorbit_book_sync.lua at BookOrbit commit c77966cc529423e993fae8a298aa77048b43bba2):
//
// - Per book, only events with startTime > watermark are sent. The watermark is
//   the highest startTime the server has acknowledged for that book.
// - Events go in requests of at most 500 events and 50 books (the server's
//   limits). A request never splits a group of events with the same startTime.
// - A watermark moves only when the server acknowledges the batch, and the
//   caller is told right then (onWatermark) so it can store it. A request that
//   fails or times out leaves the watermark where it was; running the same
//   upload again resends exactly the events that were not acknowledged. If the
//   acknowledgement was lost after the server stored them, the server counts
//   them as duplicates and ignores them, so nothing is counted twice.
// - Once at least one batch was acknowledged and the run reached its end, a
//   POST plugin/sweeps follows with the counts. If that sweep fails, the result
//   says so (sweepOwed) and the caller must send it again later.
// - A book the server does not know (listed in `unmatched`) is dropped from the
//   run and reported; its watermark does not move.
//
// Events the server would refuse (startTime < 1, totalPages < 1, page < 0) are
// skipped and counted in eventsSkippedInvalid, so one bad event cannot block a
// book's later sessions. durationSeconds is clamped to 0..86400 as the plugin does.
#pragma once

#include "smalt/bookorbit_client.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace smalt {

struct SessionUploadBook
{
    QString hash; // Partial MD5.
    qint64 bookFileId = 0; // 0 = omit.
    QVector<PageEvent> events;
    qint64 watermark = 0; // Highest startTime acknowledged so far (0 = none).
};

struct SessionUploadOptions
{
    int maxEventsPerRequest = 500;
    int maxBooksPerRequest = 50;
    int annotationsUpserted = 0; // For the sweep's counts.
    bool sendSweep = true;
};

struct SessionUploadResult
{
    HttpResult result; // The first failure, or ok.
    bool complete =
            false; // Every book's pending events were acknowledged (or the book is unmatched).
    QMap<QString, qint64> watermarks; // New watermark per book (only books that advanced).
    QStringList unmatched; // Digests the server doesn't know.
    int requests = 0; // page-stats requests sent.
    int eventsSent = 0; // Events in acknowledged requests.
    int eventsAccepted = 0;
    int eventsDuplicate = 0;
    int eventsSkippedInvalid = 0;
    int eventsSkippedAtOrBelowWatermark = 0; // Not sent: startTime <= the book's watermark.
    int booksAcknowledged = 0;
    bool sweepSent = false; // A sweep was sent and acknowledged.
    bool sweepOwed = false; // Events were acknowledged but no sweep got through.
    HttpResult sweepResult;
    QJsonObject sweepReply;
};

// Runs the upload on `client` (which must outlive the call) and calls `done`
// exactly once, later, on the client's thread. `onWatermark` (may be empty) runs
// after each acknowledged batch with the book's digest and its new watermark.
void uploadSessions(BookOrbitClient *client, const QVector<SessionUploadBook> &books,
                    const SessionUploadOptions &options,
                    std::function<void(const QString &hash, qint64 watermark)> onWatermark,
                    std::function<void(const SessionUploadResult &)> done);

} // namespace smalt
