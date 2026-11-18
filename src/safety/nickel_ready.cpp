#include "nickel_ready.h"

#include "smalt/log.h"

#include <QObject>

namespace smalt {
namespace safety {
namespace {

const char kModule[] = "safety";

} // namespace

NickelReadyWaiter::NickelReadyWaiter(NickelBridge *nickel, std::shared_ptr<Clock> clock, int pollMs,
                                     int giveUpMs)
    : m_nickel(nickel),
      m_clock(clock ? std::move(clock) : createQtClock()),
      m_pollMs(pollMs > 0 ? pollMs : kNickelReadyPollMs),
      m_giveUpMs(giveUpMs > 0 ? giveUpMs : kReadinessTimeoutMs)
{
}

NickelReadyWaiter::~NickelReadyWaiter()
{
    stop();
}

void NickelReadyWaiter::start(std::function<void()> onReady, std::function<void()> onGiveUp)
{
    if (m_started)
        return;
    m_started = true;
    m_onReady = std::move(onReady);
    m_onGiveUp = std::move(onGiveUp);

    if (!m_nickel) {
        log::error(kModule, QStringLiteral("no Nickel bridge: not waiting for Nickel"));
        m_gaveUp = true;
        std::function<void()> fn;
        fn.swap(m_onGiveUp);
        if (fn)
            fn();
        return;
    }
    if (m_nickel->isReady()) {
        becomeReady(false);
        return;
    }
    // Functor connect without a context object (Qt 5.0); the connection is
    // dropped in stop(), so the functor never outlives the waiter.
    m_connection = QObject::connect(m_nickel, &NickelBridge::nickelReady,
                                    [this]() { becomeReady(false); });
    m_timer = m_clock->startTimer(m_pollMs, [this]() { poll(m_pollMs); });
}

void NickelReadyWaiter::poll(int stepMs)
{
    m_timer = 0;
    if (m_ready || m_gaveUp)
        return;
    m_waitedMs += stepMs;
    if (m_nickel->isReady()) {
        becomeReady(true);
        return;
    }
    if (m_waitedMs >= m_giveUpMs) {
        stop();
        m_gaveUp = true;
        log::warn(kModule,
                  QStringLiteral("Nickel not ready after %1 ms; stopped waiting").arg(m_waitedMs));
        std::function<void()> fn;
        fn.swap(m_onGiveUp);
        if (fn)
            fn();
        return;
    }
    // The last poll lands exactly on the give-up time.
    const int next = qMin(m_pollMs, m_giveUpMs - m_waitedMs);
    m_timer = m_clock->startTimer(next, [this, next]() { poll(next); });
}

void NickelReadyWaiter::becomeReady(bool viaFallback)
{
    if (m_ready || m_gaveUp)
        return;
    stop();
    m_ready = true;
    m_viaFallback = viaFallback;
    log::info(kModule,
              viaFallback ? QStringLiteral("Nickel ready (direct check; signal missed)")
                          : QStringLiteral("Nickel ready"));
    std::function<void()> fn;
    fn.swap(m_onReady);
    if (fn)
        fn();
}

void NickelReadyWaiter::stop()
{
    if (m_connection)
        QObject::disconnect(m_connection);
    m_connection = QMetaObject::Connection();
    if (m_timer) {
        m_clock->cancelTimer(m_timer);
        m_timer = 0;
    }
}

} // namespace safety
} // namespace smalt
