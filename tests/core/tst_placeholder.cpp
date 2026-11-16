// Scaffold placeholder for task 10: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "smalt/ledger.h"
#include "smalt/outbox.h"

#include <QtTest>

class TstCorePlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void specValues()
    {
        QCOMPARE(smalt::kMinSessionSeconds, 10);
        QCOMPARE(smalt::kOutboxBatchSize, 20);
        QCOMPARE(smalt::kOutboxMaxBackoffSeconds, 6 * 3600);
        QVERIFY(int(smalt::OutboxKind::Session) < int(smalt::OutboxKind::Position));
        QVERIFY(int(smalt::OutboxKind::Position) < int(smalt::OutboxKind::Annotation));
        QVERIFY(int(smalt::OutboxKind::Annotation) < int(smalt::OutboxKind::Status));
    }
};

QTEST_GUILESS_MAIN(TstCorePlaceholder)
#include "tst_placeholder.moc"
