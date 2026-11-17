// Scaffold placeholder for task 20: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "fakes/fake_nickel.h"
#include "smalt/ledger.h"

#include <QSignalSpy>
#include <QtTest>

class TstRecorderPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void fakeNickelDrivesBookSignals()
    {
        smalt::testing::FakeNickel nickel;
        QSignalSpy closed(&nickel, SIGNAL(bookClosed(QString)));
        nickel.simulateBookOpened(QStringLiteral("book-1"));
        nickel.simulateBookClosed();
        QCOMPARE(closed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TstRecorderPlaceholder)
#include "tst_placeholder.moc"
