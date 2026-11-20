// Internal entry point of src/sync/ (task 21): the engine createSyncEngine()
// makes, with the clock, the sleep watchdog and the startup quiet period
// injectable for the desktop tests (tests/sync/).
#pragma once

#include "safety/clock.h"
#include "smalt/safety.h"
#include "smalt/sync.h"

#include <functional>
#include <memory>

namespace smalt {
namespace sync {

// How long one sync may take, per trigger. "Sync now" is the spec's 60 s.
// The pre-sleep sync stops a second before the 15 s watchdog so it can put
// Wi-Fi back before the device sleeps. Background triggers "give up quickly".
constexpr int kBackgroundSyncTimeoutMs = 45000;
constexpr int kPreSleepSyncTimeoutMs = kPreSleepHoldMs - 1000;
constexpr int kWhileReadingTimeoutMs = 30000;
// How long to wait for Wi-Fi to connect after asking Nickel (bounded by the
// sync's own deadline).
constexpr int kWifiConnectTimeoutMs = 20000;
// "No network at startup": triggers are ignored until Nickel is ready and
// this long after the engine was created (the readiness timeout).
constexpr int kStartupQuietMs = kReadinessTimeoutMs;
// Pull limits: books whose remote position and status are pulled.
constexpr int kPullRecentDays = 90;
constexpr int kPullMaxBooks = 50;
constexpr int kPreSleepPullMaxBooks = 5;
// Safety valve for the outbox loop (each batch is at most kOutboxBatchSize).
constexpr int kMaxOutboxBatchesPerSync = 50;

// remote_cache keys and kind the engine uses (disposable bookkeeping; losing
// them only means a position or status is sent once more).
constexpr const char *kCacheKind = "sync";

struct EngineDeps
{
    Ledger *ledger = nullptr;
    Outbox *outbox = nullptr;
    BookOrbitClient *client = nullptr;
    NickelBridge *nickel = nullptr;
    Safety *safety = nullptr;
    Recorder *recorder = nullptr;
    SettingsStore *settings = nullptr;
    std::shared_ptr<safety::Clock> clock; // Null: the Qt clock.
    // Makes the pre-sleep watchdog. Null: createSleepWatchdog() on `clock`.
    std::function<std::unique_ptr<SleepWatchdog>()> makeWatchdog;
    int startupQuietMs = kStartupQuietMs;
};

SyncEngine *createSyncEngineWith(const EngineDeps &deps, QObject *parent = nullptr);

} // namespace sync
} // namespace smalt
