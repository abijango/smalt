// Scaffold placeholder for task 21: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "smalt/sync.h"

#include <QtTest>

class TstSyncPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void specValues()
    {
        QCOMPARE(smalt::kPreSleepHoldMs, 15000);
        QCOMPARE(smalt::kPreSleepMinBatteryPercent, 20);
        QCOMPARE(smalt::kPreSleepPullFreshSeconds, 6 * 3600);
        QCOMPARE(smalt::kRecentSuccessSkipSeconds, 5 * 60);
        QCOMPARE(smalt::kManualSyncTimeoutMs, 60000);
        QCOMPARE(smalt::kToastAfterConsecutiveFailures, 3);
    }
};

QTEST_GUILESS_MAIN(TstSyncPlaceholder)
#include "tst_placeholder.moc"
