// Crash safety: the startup marker, two-strike recovery, switch files and
// the readiness timeout.
//
// Spec: Crash safety ("The companion's safeguards"). Implemented by task 12
// in src/safety/. Goals: the Kobo always reaches a working home screen, and
// reading data survives anything the mod does.
#pragma once

#include "smalt/paths.h"
#include "smalt/status.h"

#include <QString>

#include <functional>
#include <memory>

namespace smalt {

constexpr int kReadinessTimeoutMs = 30000; // "not ready 30 seconds after boot"
constexpr int kSleepWatchdogMs = 15000; // "hard 15-second timer"

// Switch files in .adds/bookorbit/ (see Paths).
enum class SafetySwitch {
    Disabled, // `disabled`: the whole mod stays inert (also written by two-strike recovery).
    DisableHome, // `disable-home`: no home takeover; recording and sync run.
    DisableSync, // `disable-sync`: no network; recording runs.
};

// What this boot may do, decided once at startup.
struct StartupDecision
{
    bool modActive = false; // False: do nothing at all (disabled, or recovery just triggered).
    bool homeTakeoverAllowed = false;
    bool syncAllowed = false;
    bool recordingAllowed = false; // True whenever modActive.
    bool recoveryTriggered = false; // Two strikes this boot: `disabled` and RECOVERY.txt written.
    bool previousStartupCrashed = false; // One leftover marker (logged only).
};

// Why the startup marker is being removed, for the log.
enum class MarkerClearReason { HomeDrawn, CleanShutdown, PowerOff, FallbackToNickelHome };

class Safety
{
public:
    virtual ~Safety() = default;

    // Reads the marker and switch files and applies two-strike recovery.
    // Call once, early, before anything else in the mod runs.
    virtual StartupDecision evaluateStartup() = 0;

    // Startup marker: covers the mod's whole startup (hooks, recorder, ledger),
    // also with `disable-home`; a leftover marker is rewritten with the new
    // strike count by evaluateStartup(). Removed after the home screen has
    // drawn, on clean shutdown, power-off, fallback to Nickel's home, and when
    // the readiness timeout fires. Refused (Unavailable) when the mod is inert
    // or readiness already timed out.
    virtual Status writeStartupMarker() = 0;
    virtual Status clearStartupMarker(MarkerClearReason reason) = 0;

    // Re-reads a switch file (they can change over USB).
    virtual bool isSwitchOn(SafetySwitch which) const = 0;

    // Readiness: call markReady() once the companion is ready. If that hasn't
    // happened kReadinessTimeoutMs after startReadinessTimer(), `onTimeout`
    // runs once and readinessTimedOut() becomes true: Nickel's home is used
    // this boot, recording still runs.
    virtual void startReadinessTimer(std::function<void()> onTimeout) = 0;
    virtual void markReady() = 0;
    virtual bool readinessTimedOut() const = 0;
};

// Holds sleep for at most kSleepWatchdogMs. `release` always runs exactly
// once: when done() is called or when the timer fires, whichever is first
// (SPEC, "Sleep watchdog").
class SleepWatchdog
{
public:
    virtual ~SleepWatchdog() = default;
    virtual void start(std::function<void()> release) = 0;
    virtual void done() = 0;
};

class NickelBridge;

// Wait for Nickel (Crash safety, "Wait for Nickel"): `onReady` runs once, when
// Nickel is already ready, on nickelReady(), or from a 1 s check of isReady()
// if the signal was missed; `onGiveUp` runs once after `giveUpMs` instead.
class NickelReadyWait
{
public:
    virtual ~NickelReadyWait() = default;
    virtual void start(std::function<void()> onReady, std::function<void()> onGiveUp) = 0;
    virtual bool ready() const = 0;
};

// Run from the plugin's NickelHook uninstall callback (NickelHook has already
// removed the library). Removes `startup-session` and `disabled` so a
// reinstall starts clean; keeps the ledger, backup/, logs/, config files,
// other switches and RECOVERY.txt (CLAUDE.md hard rule 3).
Status cleanupAfterUninstall(const Paths &paths);

// Defined by task 12 (src/safety/).
std::unique_ptr<Safety> createSafety(const Paths &paths);
std::unique_ptr<NickelReadyWait> createNickelReadyWait(NickelBridge *nickel,
                                                       int giveUpMs = kReadinessTimeoutMs);
std::unique_ptr<SleepWatchdog> createSleepWatchdog(int limitMs = kSleepWatchdogMs);

} // namespace smalt
