#include "recorder_clock.h"

#include <QElapsedTimer>

#include <time.h>

namespace smalt {
namespace recorder {
namespace {

class SystemRecorderClock : public RecorderClock
{
public:
    SystemRecorderClock() { m_monotonic.start(); }

    QDateTime nowUtc() const override { return QDateTime::currentDateTimeUtc(); }

    qint64 monotonicMs() const override { return m_monotonic.elapsed(); }

    qint64 sinceBootMs() const override
    {
#ifdef CLOCK_BOOTTIME
        struct timespec ts;
        if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0)
            return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
#endif
        return -1;
    }

private:
    QElapsedTimer m_monotonic;
};

} // namespace

std::shared_ptr<RecorderClock> createSystemRecorderClock()
{
    return std::make_shared<SystemRecorderClock>();
}

} // namespace recorder
} // namespace smalt
