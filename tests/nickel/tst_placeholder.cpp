// Scaffold placeholder for task 20 and 30: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "fakes/fake_nickel.h"

#include <QtTest>

class TstNickelPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void writesToAnOpenBookAreRefused()
    {
        smalt::testing::FakeNickel nickel;
        smalt::ContentRow row;
        row.contentId = QStringLiteral("book-1");
        nickel.putContent(row);
        nickel.simulateBookOpened(row.contentId);
        const smalt::Status s =
                nickel.writePosition(row.contentId, QStringLiteral("OPF/c1.html#p1"), 10.0,
                                     QDateTime::currentDateTimeUtc());
        QCOMPARE(int(s.code), int(smalt::StatusCode::Busy));
    }
};

QTEST_GUILESS_MAIN(TstNickelPlaceholder)
#include "tst_placeholder.moc"
