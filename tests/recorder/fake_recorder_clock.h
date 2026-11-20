// A manual clock for the recorder tests: the device clock, the monotonic clock
// and the since-boot clock move only when the test says so.
#pragma once

#include "../../src/recorder/recorder_clock.h"

#include <QDateTime>

namespace smalt {
namespace testing {

class FakeRecorderClock : public recorder::RecorderClock
{
public:
    explicit FakeRecorderClock(const QDateTime &start = QDateTime(QDate(2026, 11, 17), QTime(21, 0),
                                                                  Qt::UTC))
        : m_wall(start)
    {
    }

    QDateTime nowUtc() const override { return m_wall; }

    qint64 monotonicMs() const override { return m_mono; }

    qint64 sinceBootMs() const override { return m_bootAvailable ? m_boot : -1; }

    // Awake: every clock moves.
    void advance(qint64 ms)
    {
        m_wall = m_wall.addMSecs(ms);
        m_mono += ms;
        m_boot += ms;
    }

    void advanceSecs(qint64 s) { advance(s * 1000); }

    // Suspended: CLOCK_MONOTONIC stands still, CLOCK_BOOTTIME and the device
    // clock move.
    void suspend(qint64 ms)
    {
        m_wall = m_wall.addMSecs(ms);
        m_boot += ms;
    }

    // The device clock alone jumps (NTP, the user setting the time).
    void jumpWall(qint64 ms) { m_wall = m_wall.addMSecs(ms); }

    void setBootAvailable(bool available) { m_bootAvailable = available; }

private:
    QDateTime m_wall;
    qint64 m_mono = 0;
    qint64 m_boot = 1000000;
    bool m_bootAvailable = true;
};

} // namespace testing
} // namespace smalt
