// The sync engine: triggers in, results out.
//
// Spec: Sync engine (all), Crash safety ("Separate switches", "Sleep
// watchdog", "No network at startup"), UX details ("Where sync status
// shows"). Implemented by task 21 in src/sync/.
#pragma once

#include "smalt/bookorbit_client.h"

#include <QDateTime>
#include <QObject>
#include <QString>

namespace smalt {

// Limits from the spec's Triggers table.
constexpr int kPreSleepHoldMs = 15000; // "Holds sleep for at most 15 s"
constexpr int kPreSleepMinBatteryPercent = 20; // "skipped below 20% battery"
constexpr int kPreSleepPullFreshSeconds = 6 * 3600; // "a pull under 6 h old"
constexpr int kRecentSuccessSkipSeconds = 5 * 60; // "in the last 5 minutes, with an empty outbox"
constexpr int kManualSyncTimeoutMs = 60000; // "Sync now": 60 s
constexpr int kToastAfterConsecutiveFailures = 3; // "failed three syncs in a row"

enum class SyncTrigger {
    BookClosed, // Push that book's session, position, highlights, status.
    BeforeSleep, // End the open session, push the outbox, quick position pull.
    Wake, // Pull, then push; background.
    WifiAlreadyOn, // Full sync.
    Manual, // "Sync now": full sync with progress.
};

struct SyncRequest
{
    SyncTrigger trigger = SyncTrigger::Manual;
    QString contentId; // BookClosed: the book that closed.
};

enum class SyncOutcome {
    Skipped, // A limit said no (see skipReason).
    Succeeded,
    Partial, // Some work done, then cut short (timeout, watchdog, Wi-Fi loss).
    Failed,
};

struct SyncResult
{
    SyncTrigger trigger = SyncTrigger::Manual;
    SyncOutcome outcome = SyncOutcome::Skipped;
    QString skipReason; // For the log.
    ErrorKind error = ErrorKind::None; // Why it failed, if it did.
    // Counts for "Synced: 2 sessions, 1 highlight, 1 position".
    int sessionsPushed = 0;
    int positionsPushed = 0;
    int highlightsPushed = 0;
    int statusesPushed = 0;
    int itemsPulled = 0;
    QDateTime finishedUtc;
};

// For the home screen header: "Synced 10:42 · 3 waiting" / "Offline · 3 waiting".
struct SyncStatus
{
    bool running = false;
    bool online = false;
    QDateTime lastSuccessUtc; // Null if never.
    int waiting = 0; // Outbox items.
    int consecutiveFailures = 0;
    ErrorKind lastError = ErrorKind::None;
};

class SyncEngine : public QObject
{
    Q_OBJECT

public:
    explicit SyncEngine(QObject *parent = nullptr) : QObject(parent) { }

    ~SyncEngine() override = default;

    // Queues a trigger. Returns at once; the work runs in the background and
    // never blocks the UI thread. Respects `disable-sync` and never runs
    // during startup.
    virtual void request(const SyncRequest &request) = 0;

    // Stops the current sync as soon as it safely can (watchdog fired).
    virtual void abort() = 0;

    virtual SyncStatus status() const = 0;

signals:
    void started(smalt::SyncTrigger trigger);
    // "Syncing 3 of 5"-style static progress for "Sync now".
    void progress(const QString &text);
    void finished(const smalt::SyncResult &result);
    void statusChanged();
};

class Ledger;
class Outbox;
class NickelBridge;
class Safety;

// Wires the engine to its collaborators; none are owned. Defined by task 21
// (src/sync/).
SyncEngine *createSyncEngine(Ledger *ledger, Outbox *outbox, BookOrbitClient *client,
                             NickelBridge *nickel, Safety *safety, QObject *parent = nullptr);

} // namespace smalt
