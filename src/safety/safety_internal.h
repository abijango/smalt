// Internal entry points of src/safety/ (task 12): the same objects the public
// factories in include/smalt/safety.h make, with the clock and the installed
// library's identity injectable for tests, plus helpers that the public header
// doesn't expose yet (proposed in the task 12 report).
#pragma once

#include "clock.h"

#include "smalt/paths.h"
#include "smalt/safety.h"
#include "smalt/status.h"

#include <QString>

#include <functional>
#include <memory>

namespace smalt {
namespace safety {

// Identifies the installed libsmalt.so so a reinstall can be told apart from
// the copy that triggered recovery. Empty when the library can't be read.
using LibraryIdentity = std::function<QString()>;

// Size, modification time and inode change time of Paths::installedLibrary().
// Extracting KoboRoot.tgz again creates a new file, so the change time moves
// even when the archive (and so the modification time) is the same.
QString installedLibraryIdentity();

std::unique_ptr<Safety> createSafetyWith(const Paths &paths, std::shared_ptr<Clock> clock,
                                         LibraryIdentity libraryIdentity);

std::unique_ptr<SleepWatchdog> createSleepWatchdogWith(std::shared_ptr<Clock> clock,
                                                       int limitMs = kSleepWatchdogMs);

// The limit a SleepWatchdog actually uses: never more than kSleepWatchdogMs,
// and kSleepWatchdogMs when `requestedMs` isn't positive.
int effectiveWatchdogLimit(int requestedMs);

// The text of RECOVERY.txt. `when` is the boot that triggered recovery.
QString recoveryNoticeText(const QDateTime &when);

// Cleanup for the plugin's NickelHook `uninstall` callback, which runs after
// NickelHook has already removed libsmalt.so (because `uninstall` was
// deleted). Removes only Smalt's transient state, so a later reinstall starts
// clean: the startup marker and the `disabled` switch. Everything else stays:
// the ledger and any renamed ledgers, backup/, logs/, config.ini, the other
// switches and RECOVERY.txt (CLAUDE.md hard rule 3). Missing files are fine.
Status cleanupAfterUninstall(const Paths &paths);

} // namespace safety
} // namespace smalt
