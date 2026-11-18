// Sleep watchdog (SPEC, Crash safety, "Sleep watchdog"): the pre-sleep sync
// runs under a hard 15-second timer. When it fires, the device sleeps
// whatever the network is doing. `release` runs exactly once per start().
#include "safety_internal.h"

#include "smalt/log.h"

namespace smalt {
namespace safety {
namespace {

const char kModule[] = "safety";

class SleepWatchdogImpl : public SleepWatchdog
{
public:
    SleepWatchdogImpl(std::shared_ptr<Clock> clock, int limitMs)
        : m_clock(std::move(clock)), m_limitMs(effectiveWatchdogLimit(limitMs))
    {
    }

    // Never leave the device held awake: destroying an armed watchdog
    // releases at once.
    ~SleepWatchdogImpl() override { release(); }

    void start(std::function<void()> onRelease) override
    {
        if (m_armed) {
            // Already holding: refuse the second hold rather than lose track
            // of its release. The first hold keeps its own deadline.
            log::warn(kModule, QStringLiteral("sleep watchdog already armed; released at once"));
            if (onRelease)
                onRelease();
            return;
        }
        m_armed = true;
        m_release = std::move(onRelease);
        m_timer = m_clock->startTimer(m_limitMs, [this]() {
            m_timer = 0;
            log::warn(kModule,
                      QStringLiteral("sleep watchdog fired after %1 ms: releasing sleep")
                              .arg(m_limitMs));
            this->release();
        });
    }

    void done() override { release(); }

private:
    void release()
    {
        if (!m_armed)
            return;
        m_armed = false;
        if (m_timer) {
            m_clock->cancelTimer(m_timer);
            m_timer = 0;
        }
        std::function<void()> fn;
        fn.swap(m_release);
        if (fn)
            fn();
    }

    std::shared_ptr<Clock> m_clock;
    const int m_limitMs;
    bool m_armed = false;
    int m_timer = 0;
    std::function<void()> m_release;
};

} // namespace

int effectiveWatchdogLimit(int requestedMs)
{
    if (requestedMs <= 0 || requestedMs > kSleepWatchdogMs)
        return kSleepWatchdogMs;
    return requestedMs;
}

std::unique_ptr<SleepWatchdog> createSleepWatchdogWith(std::shared_ptr<Clock> clock, int limitMs)
{
    if (!clock)
        clock = createQtClock();
    return std::unique_ptr<SleepWatchdog>(new SleepWatchdogImpl(std::move(clock), limitMs));
}

} // namespace safety

std::unique_ptr<SleepWatchdog> createSleepWatchdog(int limitMs)
{
    return safety::createSleepWatchdogWith(safety::createQtClock(), limitMs);
}

} // namespace smalt
