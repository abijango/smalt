// Scaffold placeholder for task 12: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "smalt/safety.h"

#include <QtTest>

class TstSafetyPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void specValues()
    {
        QCOMPARE(smalt::kReadinessTimeoutMs, 30000);
        QCOMPARE(smalt::kSleepWatchdogMs, 15000);
        const smalt::StartupDecision d;
        QVERIFY(!d.modActive); // Inert unless startup says otherwise.
    }
};

QTEST_GUILESS_MAIN(TstSafetyPlaceholder)
#include "tst_placeholder.moc"
