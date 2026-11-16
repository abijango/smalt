// Scaffold placeholder for task 11: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "smalt/bookorbit_client.h"

#include <QtTest>

class TstBookOrbitPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void specValues()
    {
        QCOMPARE(smalt::kConnectTimeoutMs, 5000);
        QCOMPARE(smalt::kRequestTimeoutMs, 15000);
        smalt::HttpResult r;
        QVERIFY(r.ok());
        r.error = smalt::ErrorKind::Timeout;
        QVERIFY(!r.ok());
    }
};

QTEST_GUILESS_MAIN(TstBookOrbitPlaceholder)
#include "tst_placeholder.moc"
