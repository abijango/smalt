// Wait for Nickel (SPEC, Crash safety, "Wait for Nickel"): the home screen is
// only replaced after Nickel reports it has finished starting. If that signal
// is missed, the waiter checks Nickel's state directly instead of waiting
// forever.
//
// The Nickel side is NickelBridge (include/smalt/nickel.h): its nickelReady()
// signal and isReady(), which checks Nickel's state directly. Task 30 provides
// the real bridge and calls this from the plugin.
#pragma once

#include "clock.h"

#include "smalt/nickel.h"
#include "smalt/safety.h"

#include <QMetaObject>

#include <functional>
#include <memory>

namespace smalt {
namespace safety {

constexpr int kNickelReadyPollMs = 1000;

class NickelReadyWaiter
{
public:
    // `nickel` must outlive the waiter. Polls isReady() every `pollMs` as the
    // fallback, and stops polling after `giveUpMs` (the readiness timeout
    // takes over from there).
    NickelReadyWaiter(NickelBridge *nickel, std::shared_ptr<Clock> clock,
                      int pollMs = kNickelReadyPollMs, int giveUpMs = kReadinessTimeoutMs);
    ~NickelReadyWaiter();

    NickelReadyWaiter(const NickelReadyWaiter &) = delete;
    NickelReadyWaiter &operator=(const NickelReadyWaiter &) = delete;

    // `onReady` runs exactly once, on the first of: Nickel already ready when
    // start() is called, the nickelReady() signal, or a poll that finds
    // isReady() true. If none happens within `giveUpMs`, `onGiveUp` (may be
    // empty) runs once instead. Calling start() again is ignored.
    void start(std::function<void()> onReady, std::function<void()> onGiveUp = nullptr);

    bool ready() const { return m_ready; }

    // True when readiness came from the direct check, not the signal.
    bool readyViaFallback() const { return m_viaFallback; }

    bool gaveUp() const { return m_gaveUp; }

private:
    void becomeReady(bool viaFallback);
    void poll(int stepMs);
    void stop();

    NickelBridge *m_nickel;
    std::shared_ptr<Clock> m_clock;
    int m_pollMs;
    int m_giveUpMs;
    int m_waitedMs = 0;
    int m_timer = 0;
    bool m_started = false;
    bool m_ready = false;
    bool m_viaFallback = false;
    bool m_gaveUp = false;
    QMetaObject::Connection m_connection;
    std::function<void()> m_onReady;
    std::function<void()> m_onGiveUp;
};

} // namespace safety
} // namespace smalt
