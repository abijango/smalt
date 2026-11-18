// Time and one-shot timers for src/safety/, behind a seam so the desktop tests
// can drive boot sequences, the readiness timeout and the sleep watchdog with
// a fake clock (tests/safety/fake_clock.h).
//
// Internal to task 12. The public interface (include/smalt/safety.h) uses the
// real clock; createSafetyWith() and friends in safety_internal.h take one.
#pragma once

#include <QDateTime>

#include <functional>
#include <memory>

namespace smalt {
namespace safety {

class Clock
{
public:
    virtual ~Clock() = default;

    virtual QDateTime nowUtc() const = 0;

    // Runs `fn` once, `ms` milliseconds from now, on the calling thread's
    // event loop, never earlier. Returns an id for cancelTimer() (always > 0).
    virtual int startTimer(int ms, std::function<void()> fn) = 0;

    // Cancels a timer that hasn't fired yet. Unknown or fired ids are ignored.
    virtual void cancelTimer(int id) = 0;
};

// The real clock: QDateTime and single-shot QTimers. Timers are created
// lazily, so constructing it is safe before Nickel's QApplication exists;
// startTimer() needs an event loop on the calling thread (Nickel's UI thread).
std::shared_ptr<Clock> createQtClock();

} // namespace safety
} // namespace smalt
