// createRecorder() with the clock injectable, for the desktop tests.
// Internal to task 20; everything else uses include/smalt/recorder.h.
#pragma once

#include "recorder_clock.h"

#include "smalt/recorder.h"

#include <memory>

namespace smalt {
namespace recorder {

// Page numbers on the percentage scale: when the bridge can't tell the page
// number (pageTurned with page < 0) or page turns can't be observed at all,
// a page event's page is the book's percentage at the start of that stretch
// of reading, out of 100 pages. BookOrbit needs totalPages >= 1.
constexpr int kPercentScalePages = 100;

// The open session's row is rewritten at most this often while reading (and
// always at sleep, USB connect and close). Page events are written at every
// page you leave; crash recovery rebuilds the session's end from them.
constexpr int kOpenSessionUpdateMs = 60 * 1000;

// `clock` null: the system clock.
std::unique_ptr<Recorder> createRecorderWith(Ledger *ledger, NickelBridge *nickel,
                                             SettingsStore *settings,
                                             std::shared_ptr<RecorderClock> clock);

} // namespace recorder
} // namespace smalt
