// The ledger: Smalt's own SQLite record of what was read on this device.
//
// Spec: On-device data model (all), Crash safety ("Ledger protection"),
// Sync engine ("Conflict rules"). Implemented by task 10 in src/core/ledger/.
//
// Rules the implementation keeps (and callers can rely on):
// - File at Paths::ledger(), on a FAT partition: rollback journal,
//   synchronous=FULL, no WAL, short transactions.
// - close() on USB connect, open() again after (Nickel unmounts the partition).
// - Integrity check on open; a failing file is renamed with a timestamp and a
//   fresh ledger starts. Never deleted (CLAUDE.md hard rule 3).
// - Sessions are append-only; sessions under kMinSessionSeconds are refused.
// - Every row that syncs has a UUID made on the device.
//
// Threading: a Ledger is used from one thread at a time. Callers on Nickel's
// UI thread keep calls short; anything slow goes to a worker.
#pragma once

#include "smalt/paths.h"
#include "smalt/status.h"

#include <QDateTime>
#include <QString>
#include <QVector>

#include <memory>

namespace smalt {

// Sessions shorter than this are dropped (SPEC, "How sessions are captured",
// matching BookOrbit's rule for KOReader).
constexpr int kMinSessionSeconds = 10;

// How a book came to be on the device (book_map.origin).
enum class BookOrigin {
    Downloaded, // Smalt downloaded it from BookOrbit.
    Matched, // An existing file matched once through plugin/match-check.
    Migrated, // A Kobo-sync copy recorded by the migration (task 34).
};

// book_map: Nickel ContentID <-> BookOrbit IDs.
struct BookMapEntry
{
    QString contentId; // Nickel's content.ContentID.
    qint64 bookId = 0; // BookOrbit book ID.
    qint64 bookFileId = 0; // BookOrbit file ID.
    QString partialMd5; // KOReader's partial MD5 of the file (the "document" digest).
    BookOrigin origin = BookOrigin::Downloaded;
    QDateTime createdUtc;
};

// A position in a book. On firmware 4 Nickel stores a chapter path plus
// element ID; BookOrbit uses a percentage plus an XPointer
// (VERIFY-FINDINGS items 2 and 7).
struct Position
{
    QString locator; // Nickel ChapterIDBookmarked, or an XPointer for remote positions.
    double percent = 0.0; // 0..100.
};

// session: one reading session (SPEC, "How sessions are captured" and
// "Sleeping inside a book").
struct SessionRecord
{
    QString uuid; // Made on the device; empty on append means "make one".
    QString contentId;
    QDateTime startUtc;
    QDateTime endUtc;
    qint64 activeSeconds = 0; // From the TimeSpentReading delta.
    Position start;
    Position end;
    int pagesTurned = 0;
    bool estimated = false; // Active time fell back to the monotonic interval.
    bool approximate = false; // End position wasn't fresh at sleep; corrected later.
};

// progress: latest local position, plus the latest known remote one for the
// offline jump prompt (SPEC, "Reading progress").
struct ProgressRecord
{
    QString contentId;
    Position local;
    QDateTime localUpdatedUtc;
    bool hasRemote = false;
    Position remote;
    QDateTime remoteUpdatedUtc;
    QString remoteDevice; // Device name the server reported, for the prompt copy.
};

// annotation: highlights and notes, deletions kept as tombstones until the
// server confirms them (SPEC, "Tables"; "Highlights and notes").
struct AnnotationRecord
{
    QString uuid; // Local UUID.
    QString remoteId; // Empty until the server assigns one.
    QString contentId;
    QString anchorStart; // Position of the start (CREngine/XPointer).
    QString anchorEnd;
    QString text; // Highlighted text.
    QString context; // Surrounding text.
    QString note;
    QString colour;
    QDateTime createdUtc;
    QDateTime updatedUtc;
    QDateTime deletedUtc; // Null unless deleted (tombstone).
};

enum class ReadStatus { Unknown, Reading, Finished, Abandoned };

// book_state: status and rating, each with local and remote update times;
// newest change wins per field (SPEC, "Conflict rules").
struct BookStateRecord
{
    QString contentId;
    ReadStatus status = ReadStatus::Unknown;
    QDateTime statusLocalUpdatedUtc;
    QDateTime statusRemoteUpdatedUtc;
    int rating = 0; // 0 = none, 1..5 stars.
    QDateTime ratingLocalUpdatedUtc;
    QDateTime ratingRemoteUpdatedUtc;
};

// pending_decision: books removed upstream, waiting for the user
// (SPEC, "Books removed upstream").
enum class PendingReason { DeletedUpstream, LeftCollection };

enum class PendingChoice { Undecided, RemoveFromKobo, KeepUnlinked, DecideLater };

struct PendingDecision
{
    QString contentId;
    PendingReason reason = PendingReason::DeletedUpstream;
    PendingChoice choice = PendingChoice::Undecided;
    QDateTime createdUtc;
};

class Ledger
{
public:
    virtual ~Ledger() = default;

    // --- Lifecycle (On-device data model, "Where it lives") ---
    virtual Status open() = 0; // Opens, migrates and integrity-checks.
    virtual void close() = 0; // Safe to call when closed. Used on USB connect.
    virtual bool isOpen() const = 0;
    virtual Status backupIfDue(const QDateTime &nowUtc) = 0; // Daily copy, keeps the last 7.

    // --- server row ---
    virtual QString deviceId() = 0; // Stable per Kobo; made on first open.

    // --- session (append-only) ---
    // Refuses sessions under kMinSessionSeconds (StatusCode::InvalidArgument)
    // and duplicates of an existing UUID. Fills in record->uuid if empty.
    virtual Status appendSession(SessionRecord *record) = 0;
    virtual QVector<SessionRecord> sessionsForBook(const QString &contentId) = 0;

    // --- progress ---
    virtual Status setLocalProgress(const QString &contentId, const Position &position,
                                    const QDateTime &updatedUtc) = 0;
    virtual Status setRemoteProgress(const QString &contentId, const Position &position,
                                     const QDateTime &updatedUtc, const QString &remoteDevice) = 0;
    virtual Status progress(const QString &contentId, ProgressRecord *out) = 0;

    // --- annotation ---
    virtual Status upsertAnnotation(AnnotationRecord *record) = 0; // Fills uuid if empty.
    virtual Status markAnnotationDeleted(const QString &uuid, const QDateTime &deletedUtc) = 0;
    virtual QVector<AnnotationRecord> annotationsForBook(const QString &contentId) = 0;

    // --- book_state ---
    virtual Status bookState(const QString &contentId, BookStateRecord *out) = 0;
    virtual Status setLocalStatus(const QString &contentId, ReadStatus status,
                                  const QDateTime &atUtc) = 0;
    virtual Status setLocalRating(const QString &contentId, int rating, const QDateTime &atUtc) = 0;
    // Applies a remote value only where it is newer than the local one.
    virtual Status mergeRemoteState(const BookStateRecord &remote) = 0;

    // --- book_map ---
    virtual Status putBookMap(const BookMapEntry &entry) = 0;
    virtual Status bookMapByContentId(const QString &contentId, BookMapEntry *out) = 0;
    virtual Status bookMapByBookId(qint64 bookId, BookMapEntry *out) = 0;
    virtual Status unlinkBook(const QString &contentId) = 0; // "Keep, unlinked".

    // --- pending_decision ---
    virtual Status addPendingDecision(const PendingDecision &decision) = 0;
    virtual QVector<PendingDecision> pendingDecisions() = 0;
    virtual Status resolvePendingDecision(const QString &contentId, PendingChoice choice) = 0;
};

// Makes the SQLite ledger at paths.ledger(). Doesn't open it. Defined by
// task 10 (src/core/ledger/).
std::unique_ptr<Ledger> createLedger(const Paths &paths);

} // namespace smalt
