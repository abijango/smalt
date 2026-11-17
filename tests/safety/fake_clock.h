// A manual clock for the safety tests: time only moves when the test calls
// advance(), and timers fire in deadline order as it passes them.
#pragma once

#include "safety/clock.h"

#include <QDateTime>
#include <QMap>
#include <QPair>

#include <functional>

namespace smalt {
namespace testing {

class FakeClock : public safety::Clock
{
public:
    explicit FakeClock(const QDateTime &start = QDateTime(QDate(2026, 11, 17), QTime(6, 30),
                                                          Qt::UTC))
        : m_now(start)
    {
    }

    QDateTime nowUtc() const override { return m_now; }

    int startTimer(int ms, std::function<void()> fn) override
    {
        const int id = ++m_nextId;
        m_timers.insert(id, qMakePair(m_elapsedMs + qMax(0, ms), fn));
        return id;
    }

    void cancelTimer(int id) override { m_timers.remove(id); }

    // Moves time forward by `ms`, firing every timer that falls due on the way
    // (including timers started by a callback, if they fall due in range).
    void advance(qint64 ms)
    {
        const qint64 target = m_elapsedMs + ms;
        for (;;) {
            int dueId = 0;
            qint64 dueAt = 0;
            for (auto it = m_timers.constBegin(); it != m_timers.constEnd(); ++it) {
                if (it.value().first <= target && (dueId == 0 || it.value().first < dueAt)) {
                    dueId = it.key();
                    dueAt = it.value().first;
                }
            }
            if (dueId == 0)
                break;
            const std::function<void()> fn = m_timers.take(dueId).second;
            m_now = m_now.addMSecs(dueAt - m_elapsedMs);
            m_elapsedMs = dueAt;
            fn();
        }
        m_now = m_now.addMSecs(target - m_elapsedMs);
        m_elapsedMs = target;
    }

    qint64 elapsedMs() const { return m_elapsedMs; }

    int pendingTimers() const { return m_timers.size(); }

    // A new boot: the wall clock moves on, pending timers die with the process.
    void reboot(qint64 offMs = 60000)
    {
        m_timers.clear();
        m_now = m_now.addMSecs(offMs);
    }

private:
    QDateTime m_now;
    qint64 m_elapsedMs = 0;
    int m_nextId = 0;
    QMap<int, QPair<qint64, std::function<void()>>> m_timers;
};

} // namespace testing
} // namespace smalt
