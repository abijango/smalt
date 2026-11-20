// The ledger: Smalt's own SQLite record of what was read on this device.
//
// Spec: On-device data model (all; "How sessions are captured", "How BookOrbit
// builds sessions", "Sleeping inside a book", "Correcting a session"), Crash
// safety ("Ledger protection"), Sync engine ("Order of a full sync",
// "Conflict rules"), Status and ratings, Stats ("Sources"). Implemented by
// task 10 in src/core/ledger/.
//
// Rules the implementation keeps (and callers can rely on):
// - File at Paths::ledger(), on a FAT partition: rollback journal,
//   synchronous=FULL, no WAL, short transactions.
// - close() on USB connect, open() again after (Nickel unmounts the partition).
// - Integrity check on open; only real corruption renames the file (with a
//   timestamp) and starts a fresh ledger. Never deleted (CLAUDE.md hard rule 3).
// - Page events are append-only and never edited. Sessions are never deleted;
//   a session's times can change only while it is open, and its end position
//   can be corrected at any time.
// - Every row that syncs has a UUID made on the device.
//
// Reading is uploaded as page events, not through the outbox: per book, every
// page event newer than the book's watermark still has to go (see
// pageEventsAfter() and the watermark in book_map). Only books with a linked
// book_map row are pending; reading of unmatched or unlinked books stays in
// the ledger and goes out if the book is matched later.
//
// Same-second rule: event times are whole seconds and the watermark is
// exclusive, so a push while reading sends only events that start before the
// start second of the page on screen; that page and anything starting in the
// same second wait for the next push. appendPageEvent() refuses (and logs) an
// event at or below its book's watermark, as a backstop.
//
// "Rejected for good": a page-stats request refused with ClientError (other
// than 401/403/404/408/429). The sync engine then moves that book's
// watermark past the rejected events and logs it; the events stay here.
//
// Threading: a Ledger is used from one thread at a time (Qt SQL connections
// belong to the thread that opened them). Callers on Nickel's UI thread keep
// calls short; anything slow goes to a worker.
#pragma once

#include "smalt/paths.h"
#include "smalt/status.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace smalt {

// Sessions with less active reading than this are dropped (SPEC, "How sessions
// are captured", matching BookOrbit's rule for KOReader).
constexpr int kMinSessionSeconds = 10;

// "New session after sleep" default (SPEC, "Sleeping inside a book"): waking
// within this time continues the same session.
constexpr int kDefaultNewSessionAfterSleepSeconds = 15 * 60;

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
    // Start time (Unix seconds) of the newest page event that needs no further
    // upload for this file from this device: acknowledged by the server (its
    // page-stats watermark), or rejected for good and logged. 0 = none.
    qint64 statsWatermark = 0;
};

// A position in a book. On firmware 4 Nickel stores a chapter path plus
// element ID; BookOrbit uses a percentage plus an XPointer
// (VERIFY-FINDINGS items 2 and 7).
struct Position
{
    QString locator; // Nickel ChapterIDBookmarked, or an XPointer for remote positions.
    double percent = 0.0; // 0..100.
};

// The life of a session (SPEC, "Sleeping inside a book").
enum class SessionState {
    Open, // Reading, or asleep within "New session after sleep".
    Finished, // Closed, or a sleep longer than the threshold ended it.
    Dropped, // Finished with less than kMinSessionSeconds of reading; kept, never counted.
};

// session: one reading session.
struct SessionRecord
{
    QString uuid; // Made on the device; empty on begin/append means "make one".
    QString contentId;
    SessionState state = SessionState::Open;
    QDateTime startUtc;
    QDateTime endUtc; // While open: the latest observation (or the moment of sleep).
    qint64 activeSeconds = 0; // Sleep never counted.
    Position start;
    Position end;
    int startPage = -1; // Page numbers as in the page events; -1 = unknown.
    int endPage = -1;
    int totalPages = 0;
    int pagesTurned = 0;
    bool estimated = false; // Built from counters because page turns couldn't be observed.
    bool approximate = false; // End position wasn't fresh at sleep; corrected later.
    // Read-only, filled by queries: every page event of the session is at or
    // below its book's watermark (BookOrbit has it all).
    bool confirmed = false;
};

// page_event: one per page read (SPEC, "How sessions are captured"). Written
// when you leave the page; the page on screen ends at close or sleep.
struct PageEventRecord
{
    QString sessionUuid;
    QString contentId;
    int page = 0;
    int totalPages = 0;
    QDateTime startUtc; // Uploaded as Unix seconds.
    int durationSeconds = 0;
};

// progress: latest local position, plus the latest known remote one for the
// offline jump prompt (SPEC, "Reading progress").
struct ProgressRecord
{
    QString contentId;
    QString uuid; // Stable per book; the outbox entityUuid for Position items.
    Position local;
    QDateTime localUpdatedUtc;
    bool hasRemote = false;
    Position remote;
    QDateTime remoteUpdatedUtc;
    QString remoteDevice; // Device name the server reported, for the prompt copy.
};

// annotation: highlights and notes, deletions kept as tombstones until the
// server confirms them (SPEC, "Tables"; "Highlights and notes").
// annotationsForBook() returns tombstones too (deletedUtc set); screens filter them.
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

// BookOrbit's six statuses (SPEC, "Status and ratings"); Nickel has three.
enum class ReadStatus { Unknown, Unread, WantToRead, Reading, OnHold, Read, Abandoned };

// book_state: status and rating, each with local and remote update times;
// newest change wins per field (SPEC, "Conflict rules").
struct BookStateRecord
{
    QString contentId;
    QString uuid; // Stable per book; the outbox entityUuid for Status items.
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
    virtual QString pullCursor() = 0; // Empty if never pulled.
    virtual QDateTime lastPullUtc() = 0;
    virtual Status setPullCursor(const QString &cursor, const QDateTime &pulledUtc) = 0;
    virtual QDateTime lastSyncUtc() = 0; // Last sync that finished successfully.
    virtual Status setLastSyncUtc(const QDateTime &atUtc) = 0;
    // A sweep is owed when page events were acknowledged but no sweep got through.
    virtual bool sweepOwed() = 0;
    virtual Status setSweepOwed(bool owed) = 0;

    // --- session ---
    // Starts an open session. Fills record->uuid if empty. InvalidArgument for a
    // missing book or start time; refuses a duplicate UUID.
    virtual Status beginSession(SessionRecord *record) = 0;
    // Updates an open session's running end, active time, end position and page
    // counts (at sleep, or as reading goes on). InvalidArgument once it is not open.
    virtual Status updateOpenSession(const SessionRecord &record) = 0;
    // Finishes an open session with its final values: Finished, or Dropped when
    // activeSeconds < kMinSessionSeconds. A Dropped session and its page events
    // are kept but never counted.
    virtual Status finishSession(const SessionRecord &record) = 0;
    // Appends a complete session in one step (estimated sessions built from
    // counters). Refuses sessions under kMinSessionSeconds and duplicate UUIDs.
    virtual Status appendSession(SessionRecord *record) = 0;
    // Corrects a session's end position at any time, including after its page
    // events were sent (SPEC, "Correcting a session"). Never changes a time.
    // NotFound for an unknown UUID.
    virtual Status correctSessionEnd(const QString &uuid, const Position &end,
                                     bool approximate) = 0;
    virtual Status session(const QString &uuid, SessionRecord *out) = 0;
    // The open session, if any (at most one per device). NotFound if none.
    virtual Status openSession(SessionRecord *out) = 0;
    virtual QVector<SessionRecord> sessionsForBook(const QString &contentId) = 0;
    // Newest first, at most `limit` (Stats: book detail "Your reading").
    virtual QVector<SessionRecord> recentSessionsForBook(const QString &contentId, int limit) = 0;
    // Sessions starting in [fromUtc, toUtc), oldest first; Dropped ones excluded (Stats).
    // recentSessionsForBook() excludes Dropped sessions too; sessionsForBook() doesn't.
    virtual QVector<SessionRecord> sessionsInRange(const QDateTime &fromUtc,
                                                   const QDateTime &toUtc) = 0;
    // Finished or open sessions with page events still above their book's
    // watermark, oldest first; Dropped sessions and unmatched or unlinked books
    // excluded (recovery after a crash). Stats doesn't add these sessions'
    // activeSeconds: with push while reading they are partly on the server.
    // It sums pageEventsInRange() events that aren't confirmed, plus the open
    // session's current page.
    virtual QVector<SessionRecord> unconfirmedSessions() = 0;

    // --- page_event (append-only) ---
    // InvalidArgument for an unknown or Dropped session, a different book from
    // the session's, a negative page or duration, a missing start time, an
    // event outside a Finished session's start and end, or one at or below its
    // book's watermark (logged); refuses a duplicate (book, page, start).
    virtual Status appendPageEvent(const PageEventRecord &event) = 0;
    // Page events starting in [fromUtc, toUtc) across all books, oldest first,
    // Dropped sessions excluded. `confirmed` (if not null) gets one flag per
    // event: at or below its book's watermark (Stats: timeline blocks, minutes
    // per day split at midnight, merging unsent minutes).
    virtual QVector<PageEventRecord> pageEventsInRange(const QDateTime &fromUtc,
                                                       const QDateTime &toUtc,
                                                       QVector<bool> *confirmed = nullptr) = 0;
    // A book's page events with a start time after `afterUnixSeconds`, oldest
    // first, at most `limit` (0 = no limit). What still has to be uploaded.
    virtual QVector<PageEventRecord> pageEventsAfter(const QString &contentId,
                                                     qint64 afterUnixSeconds, int limit = 0) = 0;
    // Linked books with page events above their watermark, for the push.
    virtual QStringList booksWithPendingPageEvents() = 0;

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
    // Moves the watermark forward only (a smaller value is ignored, Ok).
    virtual Status setStatsWatermark(const QString &contentId, qint64 unixSeconds) = 0;

    // --- remote_cache (disposable; safe to wipe) ---
    // Dashboard (Stats), catalogue pages and cover thumbnails. NotFound if absent.
    virtual Status cacheGet(const QString &key, QByteArray *value, QDateTime *updatedUtc) = 0;
    virtual Status cachePut(const QString &key, const QString &kind, const QByteArray &value,
                            const QDateTime &updatedUtc) = 0;
    virtual Status cacheClear(const QString &kind) = 0; // Empty kind = everything.

    // --- pending_decision ---
    virtual Status addPendingDecision(const PendingDecision &decision) = 0;
    virtual QVector<PendingDecision> pendingDecisions() = 0;
    virtual Status resolvePendingDecision(const QString &contentId, PendingChoice choice) = 0;
};

// Makes the SQLite ledger at paths.ledger(). Doesn't open it. Defined by
// task 10 (src/core/ledger/).
std::unique_ptr<Ledger> createLedger(const Paths &paths);

} // namespace smalt
