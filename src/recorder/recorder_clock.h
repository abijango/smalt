// Time for the recorder, behind a seam so the desktop tests can drive reading,
// sleep and clock jumps with a fake clock (tests/recorder/fake_recorder_clock.h).
//
// Three clocks (SPEC, "How sessions are captured": "the device clock and a
// monotonic clock"):
// - nowUtc(): the device clock, for the start and end times written to the
//   ledger. It can jump (NTP after Wi-Fi comes up, the user setting the time).
// - monotonicMs(): CLOCK_MONOTONIC (QElapsedTimer). It doesn't jump and it
//   doesn't advance while the Kobo is suspended, so every reading duration
//   comes from it.
// - sinceBootMs(): CLOCK_BOOTTIME. It doesn't jump and it does advance while
//   suspended, so it measures how long the Kobo slept ("New session after
//   sleep"). -1 if the system doesn't have it; the recorder then falls back
//   to the device clock.
//
// Internal to task 20.
#pragma once

#include <QDateTime>

#include <memory>

namespace smalt {
namespace recorder {

class RecorderClock
{
public:
    virtual ~RecorderClock() = default;

    virtual QDateTime nowUtc() const = 0;
    virtual qint64 monotonicMs() const = 0;
    virtual qint64 sinceBootMs() const = 0;
};

// QDateTime, QElapsedTimer and clock_gettime(CLOCK_BOOTTIME).
std::shared_ptr<RecorderClock> createSystemRecorderClock();

} // namespace recorder
} // namespace smalt
