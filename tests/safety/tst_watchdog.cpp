// Task 12 acceptance test: the sleep watchdog always releases the sleep hold,
// exactly once, within 15 s (SPEC, Crash safety, "Sleep watchdog").
#include "fake_clock.h"
#include "safety/safety_internal.h"

#include "smalt/safety.h"

#include <QElapsedTimer>
#include <QtTest>

#include <climits>
#include <memory>

using smalt::SleepWatchdog;
using smalt::testing::FakeClock;

class TstWatchdog : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<FakeClock> m_clock;

private slots:

    void init() { m_clock = std::make_shared<FakeClock>(); }

    void alwaysFiresWithin15s_data()
    {
        QTest::addColumn<int>("requested");
        QTest::addColumn<int>("expected");
        QTest::newRow("default") << smalt::kSleepWatchdogMs << 15000;
        QTest::newRow("shorter") << 5000 << 5000;
        QTest::newRow("1 ms") << 1 << 1;
        QTest::newRow("just over") << 15001 << 15000;
        QTest::newRow("a minute") << 60000 << 15000;
        QTest::newRow("INT_MAX") << INT_MAX << 15000;
        QTest::newRow("zero") << 0 << 15000;
        QTest::newRow("negative") << -1 << 15000;
    }

    void alwaysFiresWithin15s()
    {
        QFETCH(int, requested);
        QFETCH(int, expected);
        QCOMPARE(smalt::safety::effectiveWatchdogLimit(requested), expected);

        std::unique_ptr<SleepWatchdog> dog =
                smalt::safety::createSleepWatchdogWith(m_clock, requested);
        int released = 0;
        dog->start([&released]() { ++released; });
        m_clock->advance(expected - 1);
        QCOMPARE(released, 0);
        m_clock->advance(1);
        QCOMPARE(released, 1);
        QVERIFY(m_clock->elapsedMs() <= smalt::kSleepWatchdogMs);
        // Exactly once, however long the sync keeps going.
        m_clock->advance(10 * smalt::kSleepWatchdogMs);
        dog->done();
        QCOMPARE(released, 1);
    }

    void doneReleasesOnce()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
        int released = 0;
        dog->start([&released]() { ++released; });
        m_clock->advance(4000);
        dog->done();
        QCOMPARE(released, 1);
        QCOMPARE(m_clock->pendingTimers(), 0);
        dog->done();
        m_clock->advance(2 * smalt::kSleepWatchdogMs);
        QCOMPARE(released, 1);
    }

    void doneBeforeStartDoesNothing()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
        dog->done();
        int released = 0;
        dog->start([&released]() { ++released; });
        m_clock->advance(smalt::kSleepWatchdogMs);
        QCOMPARE(released, 1);
    }

    void reusableAfterRelease()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
        int first = 0;
        int second = 0;
        dog->start([&first]() { ++first; });
        dog->done();
        dog->start([&second]() { ++second; });
        m_clock->advance(smalt::kSleepWatchdogMs);
        QCOMPARE(first, 1);
        QCOMPARE(second, 1);
    }

    void secondStartWhileHeldNeverHolds()
    {
        // A second hold while the first is running is refused at once, so
        // both releases still run exactly once and neither waits past 15 s.
        std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
        int first = 0;
        int second = 0;
        dog->start([&first]() { ++first; });
        m_clock->advance(1000);
        dog->start([&second]() { ++second; });
        QCOMPARE(second, 1);
        QCOMPARE(first, 0);
        m_clock->advance(smalt::kSleepWatchdogMs - 1000);
        QCOMPARE(first, 1);
        QCOMPARE(second, 1);
    }

    void releaseMayCallDone()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
        int released = 0;
        SleepWatchdog *raw = dog.get();
        dog->start([&released, raw]() {
            ++released;
            raw->done();
        });
        m_clock->advance(smalt::kSleepWatchdogMs);
        QCOMPARE(released, 1);
    }

    void destroyingReleases()
    {
        int released = 0;
        {
            std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
            dog->start([&released]() { ++released; });
        }
        QCOMPARE(released, 1);
        QCOMPARE(m_clock->pendingTimers(), 0);
        m_clock->advance(smalt::kSleepWatchdogMs);
        QCOMPARE(released, 1);
    }

    void emptyReleaseIsSafe()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::safety::createSleepWatchdogWith(m_clock);
        dog->start(std::function<void()>());
        m_clock->advance(smalt::kSleepWatchdogMs);
        dog->done();
    }

    // The public factory, on real Qt timers.
    void realTimerFires()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::createSleepWatchdog(50);
        int released = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        dog->start([&released]() { ++released; });
        QTRY_COMPARE_WITH_TIMEOUT(released, 1, 5000);
        QVERIFY(elapsed.elapsed() >= 40);
        QTest::qWait(100);
        QCOMPARE(released, 1);
    }

    void realTimerDone()
    {
        std::unique_ptr<SleepWatchdog> dog = smalt::createSleepWatchdog();
        int released = 0;
        dog->start([&released]() { ++released; });
        dog->done();
        QCOMPARE(released, 1);
        QTest::qWait(50);
        QCOMPARE(released, 1);
    }
};

QTEST_GUILESS_MAIN(TstWatchdog)
#include "tst_watchdog.moc"
