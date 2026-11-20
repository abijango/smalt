// Internal entry points of src/safety/ (task 12): the same objects the public
// factories in include/smalt/safety.h make, with the clock and the installed
// library's identity injectable for tests, plus helpers the tests check
// directly.
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

} // namespace safety
} // namespace smalt
