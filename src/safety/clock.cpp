#include "clock.h"

#include <QHash>
#include <QObject>
#include <QTimer>

namespace smalt {
namespace safety {
namespace {

// Qt 5.2: QTimer::singleShot with a functor is 5.4, so each timer is a
// single-shot QTimer connected to a functor (functor connect is 5.0).
class QtClock : public Clock
{
public:
    ~QtClock() override
    {
        for (QTimer *timer : m_timers)
            delete timer;
    }

    QDateTime nowUtc() const override { return QDateTime::currentDateTimeUtc(); }

    int startTimer(int ms, std::function<void()> fn) override
    {
        const int id = nextId();
        QTimer *timer = new QTimer;
        timer->setSingleShot(true);
        m_timers.insert(id, timer);
        QObject::connect(timer, &QTimer::timeout, [this, id, fn]() {
            QTimer *fired = m_timers.take(id);
            if (!fired)
                return;
            fired->deleteLater();
            if (fn)
                fn();
        });
        timer->start(ms < 0 ? 0 : ms);
        return id;
    }

    void cancelTimer(int id) override
    {
        QTimer *timer = m_timers.take(id);
        if (!timer)
            return;
        timer->stop();
        timer->deleteLater();
    }

private:
    int nextId()
    {
        if (++m_lastId <= 0)
            m_lastId = 1;
        return m_lastId;
    }

    int m_lastId = 0;
    QHash<int, QTimer *> m_timers;
};

} // namespace

std::shared_ptr<Clock> createQtClock()
{
    return std::make_shared<QtClock>();
}

} // namespace safety
} // namespace smalt
