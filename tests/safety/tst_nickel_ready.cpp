// Wait for Nickel (SPEC, Crash safety): the signal, the direct-check fallback
// when the signal was missed, and giving up so nothing waits forever.
#include "fake_clock.h"
#include "fakes/fake_nickel.h"
#include "safety/nickel_ready.h"

#include "smalt/safety.h"

#include <QElapsedTimer>
#include <QtTest>

#include <memory>

using smalt::safety::NickelReadyWaiter;
using smalt::testing::FakeClock;
using smalt::testing::FakeNickel;

class TstNickelReady : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<FakeClock> m_clock;
    std::unique_ptr<FakeNickel> m_nickel;

private slots:

    void init()
    {
        m_clock = std::make_shared<FakeClock>();
        m_nickel.reset(new FakeNickel);
    }

    void alreadyReady()
    {
        m_nickel->setReady(true);
        NickelReadyWaiter waiter(m_nickel.get(), m_clock);
        int ready = 0;
        waiter.start([&ready]() { ++ready; });
        QCOMPARE(ready, 1);
        QVERIFY(waiter.ready());
        QCOMPARE(m_clock->pendingTimers(), 0);
    }

    void signal()
    {
        NickelReadyWaiter waiter(m_nickel.get(), m_clock);
        int ready = 0;
        int gaveUp = 0;
        waiter.start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        m_clock->advance(2500);
        QCOMPARE(ready, 0);
        m_nickel->simulateNickelReady();
        QCOMPARE(ready, 1);
        QVERIFY(!waiter.readyViaFallback());
        QCOMPARE(m_clock->pendingTimers(), 0);
        // A repeated signal doesn't run it again.
        m_nickel->simulateNickelReady();
        m_clock->advance(2 * smalt::kReadinessTimeoutMs);
        QCOMPARE(ready, 1);
        QCOMPARE(gaveUp, 0);
    }

    void missedSignalFallsBackToDirectCheck()
    {
        NickelReadyWaiter waiter(m_nickel.get(), m_clock);
        int ready = 0;
        waiter.start([&ready]() { ++ready; });
        m_clock->advance(3000); // Three polls so far, all not ready.
        m_nickel->setReady(true); // Nickel finished, but the signal never came.
        m_clock->advance(smalt::safety::kNickelReadyPollMs - 1);
        QCOMPARE(ready, 0);
        m_clock->advance(1);
        QCOMPARE(ready, 1);
        QVERIFY(waiter.readyViaFallback());
        QCOMPARE(m_clock->pendingTimers(), 0);
    }

    void neverReadyGivesUp()
    {
        NickelReadyWaiter waiter(m_nickel.get(), m_clock);
        int ready = 0;
        int gaveUp = 0;
        waiter.start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        m_clock->advance(smalt::kReadinessTimeoutMs - 1);
        QCOMPARE(gaveUp, 0);
        m_clock->advance(1);
        QCOMPARE(gaveUp, 1);
        QVERIFY(waiter.gaveUp());
        QCOMPARE(m_clock->pendingTimers(), 0);
        // Late readiness is ignored: the readiness timeout already decided.
        m_nickel->simulateNickelReady();
        QCOMPARE(ready, 0);
        QVERIFY(!waiter.ready());
    }

    void startTwiceIgnored()
    {
        NickelReadyWaiter waiter(m_nickel.get(), m_clock);
        int first = 0;
        int second = 0;
        waiter.start([&first]() { ++first; });
        waiter.start([&second]() { ++second; });
        m_nickel->simulateNickelReady();
        QCOMPARE(first, 1);
        QCOMPARE(second, 0);
    }

    void destroyedWaiterStopsListening()
    {
        int ready = 0;
        {
            NickelReadyWaiter waiter(m_nickel.get(), m_clock);
            waiter.start([&ready]() { ++ready; });
        }
        QCOMPARE(m_clock->pendingTimers(), 0);
        m_nickel->simulateNickelReady();
        m_clock->advance(smalt::kReadinessTimeoutMs);
        QCOMPARE(ready, 0);
    }

    void noBridgeGivesUpAtOnce()
    {
        NickelReadyWaiter waiter(nullptr, m_clock);
        int ready = 0;
        int gaveUp = 0;
        waiter.start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        QCOMPARE(ready, 0);
        QCOMPARE(gaveUp, 1);
    }

    void giveUpShorterThanPollEndsOnTime()
    {
        NickelReadyWaiter waiter(m_nickel.get(), m_clock, smalt::safety::kNickelReadyPollMs, 300);
        int gaveUp = 0;
        waiter.start([]() {}, [&gaveUp]() { ++gaveUp; });
        m_clock->advance(299);
        QCOMPARE(gaveUp, 0);
        m_clock->advance(1);
        QCOMPARE(gaveUp, 1);
        QCOMPARE(m_clock->pendingTimers(), 0);
    }

    // --- The public API (include/smalt/safety.h), on the real Qt clock ---

    void publicAlreadyReady()
    {
        m_nickel->setReady(true);
        std::unique_ptr<smalt::NickelReadyWait> wait =
                smalt::createNickelReadyWait(m_nickel.get(), 5000);
        int ready = 0;
        int gaveUp = 0;
        wait->start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        QCOMPARE(ready, 1);
        QVERIFY(wait->ready());
        QCOMPARE(gaveUp, 0);
    }

    void publicReadyOnSignal()
    {
        std::unique_ptr<smalt::NickelReadyWait> wait =
                smalt::createNickelReadyWait(m_nickel.get(), 5000);
        int ready = 0;
        int gaveUp = 0;
        wait->start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        QVERIFY(!wait->ready());
        m_nickel->simulateNickelReady();
        QCOMPARE(ready, 1);
        QVERIFY(wait->ready());
        m_nickel->simulateNickelReady();
        QTest::qWait(50);
        QCOMPARE(ready, 1);
        QCOMPARE(gaveUp, 0);
    }

    void publicReadyOnMissedSignalFallback()
    {
        std::unique_ptr<smalt::NickelReadyWait> wait =
                smalt::createNickelReadyWait(m_nickel.get(), 5000);
        int ready = 0;
        int gaveUp = 0;
        wait->start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        m_nickel->setReady(true); // No signal: only the 1 s direct check sees it.
        QCOMPARE(ready, 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready, 1, 3 * smalt::safety::kNickelReadyPollMs);
        QVERIFY(wait->ready());
        QCOMPARE(gaveUp, 0);
    }

    void publicGivesUpAfterGiveUpMs()
    {
        const int giveUpMs = 200;
        std::unique_ptr<smalt::NickelReadyWait> wait =
                smalt::createNickelReadyWait(m_nickel.get(), giveUpMs);
        int ready = 0;
        int gaveUp = 0;
        QElapsedTimer elapsed;
        qint64 gaveUpAfterMs = -1;
        elapsed.start();
        wait->start([&ready]() { ++ready; },
                    [&gaveUp, &gaveUpAfterMs, &elapsed]() {
                        ++gaveUp;
                        gaveUpAfterMs = elapsed.elapsed();
                    });
        QTRY_COMPARE_WITH_TIMEOUT(gaveUp, 1, 5000);
        // The real clock's timers are precise, so never early.
        QVERIFY2(gaveUpAfterMs >= giveUpMs, qPrintable(QString::number(gaveUpAfterMs)));
        QVERIFY2(gaveUpAfterMs < smalt::safety::kNickelReadyPollMs,
                 qPrintable(QString::number(gaveUpAfterMs)));
        QVERIFY(!wait->ready());
        // Late readiness is ignored, and nothing runs twice.
        m_nickel->simulateNickelReady();
        QTest::qWait(50);
        QCOMPARE(ready, 0);
        QCOMPARE(gaveUp, 1);
        QVERIFY(!wait->ready());
    }

    void publicDestroyedWaitIsQuiet()
    {
        int ready = 0;
        int gaveUp = 0;
        {
            std::unique_ptr<smalt::NickelReadyWait> wait =
                    smalt::createNickelReadyWait(m_nickel.get(), 100);
            wait->start([&ready]() { ++ready; }, [&gaveUp]() { ++gaveUp; });
        }
        m_nickel->setReady(true);
        m_nickel->simulateNickelReady();
        QTest::qWait(300);
        QCOMPARE(ready, 0);
        QCOMPARE(gaveUp, 0);
    }
};

QTEST_GUILESS_MAIN(TstNickelReady)
#include "tst_nickel_ready.moc"
