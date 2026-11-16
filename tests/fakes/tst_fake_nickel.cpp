#include "fakes/fake_nickel.h"

#include <QSignalSpy>
#include <QtTest>

using smalt::StatusCode;
using smalt::testing::FakeNickel;

class TstFakeNickel : public QObject
{
    Q_OBJECT

private slots:

    void openAndCloseEmitSignals()
    {
        FakeNickel nickel;
        smalt::ContentRow row;
        row.contentId = QStringLiteral("book-1");
        nickel.putContent(row);
        QSignalSpy opened(&nickel, SIGNAL(bookOpened(QString)));
        QSignalSpy closed(&nickel, SIGNAL(bookClosed(QString)));

        QVERIFY(nickel.openBook(row.contentId).ok());
        QCOMPARE(nickel.openContentId(), row.contentId);
        QCOMPARE(opened.count(), 1);
        nickel.simulateBookClosed();
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(0).toString(), row.contentId);
        QVERIFY(nickel.openContentId().isEmpty());
    }

    void writesWaitForTheBookToClose()
    {
        FakeNickel nickel;
        smalt::ContentRow row;
        row.contentId = QStringLiteral("book-1");
        nickel.putContent(row);
        nickel.simulateBookOpened(row.contentId);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QCOMPARE(int(nickel.writePosition(row.contentId, QStringLiteral("c#1"), 54, now).code),
                 int(StatusCode::Busy));
        QCOMPARE(int(nickel.writeReadStatus(row.contentId, 2).code), int(StatusCode::Busy));
        QCOMPARE(int(nickel.deleteBook(row.contentId).code), int(StatusCode::Busy));
        nickel.simulateBookClosed();
        QVERIFY(nickel.writePosition(row.contentId, QStringLiteral("c#1"), 54, now).ok());
        QCOMPARE(nickel.content(row.contentId).percentRead, 54.0);
    }

    void usbConnectBlocksWrites()
    {
        FakeNickel nickel;
        smalt::ContentRow row;
        row.contentId = QStringLiteral("book-1");
        nickel.putContent(row);
        nickel.simulateUsbConnected();
        QCOMPARE(int(nickel.writeRating(row.contentId, 4).code), int(StatusCode::Busy));
        nickel.simulateUsbDisconnected();
        QVERIFY(nickel.writeRating(row.contentId, 4).ok());
    }

    void readingTimeAccumulates()
    {
        FakeNickel nickel;
        nickel.addReadingTime(QStringLiteral("book-1"), 30);
        nickel.addReadingTime(QStringLiteral("book-1"), 45);
        smalt::ContentRow row;
        QVERIFY(nickel.readContent(QStringLiteral("book-1"), &row).ok());
        QCOMPARE(row.timeSpentReading, qint64(75));
        QCOMPARE(int(nickel.readContent(QStringLiteral("missing"), &row).code),
                 int(StatusCode::NotFound));
    }

    void wifiStateIsTracked()
    {
        FakeNickel nickel;
        QSignalSpy network(&nickel, SIGNAL(networkChanged(bool)));
        QVERIFY(!nickel.wifiEnabled());
        QVERIFY(!nickel.connectWifi().ok()); // Off: can't connect.
        QVERIFY(nickel.setWifiEnabled(true).ok());
        QVERIFY(nickel.connectWifi().ok());
        QVERIFY(nickel.networkConnected());
        QVERIFY(nickel.setWifiEnabled(false).ok());
        QVERIFY(!nickel.networkConnected());
        QCOMPARE(network.count(), 2);
        QCOMPARE(nickel.wifiEnableCalls, 1);
        QCOMPARE(nickel.wifiDisableCalls, 1);
    }

    void sleepHoldsBalance()
    {
        FakeNickel nickel;
        QVERIFY(nickel.holdSleep().ok());
        QCOMPARE(nickel.sleepHolds, 1);
        nickel.releaseSleep();
        nickel.releaseSleep();
        QCOMPARE(nickel.sleepHolds, 0);
    }
};

QTEST_GUILESS_MAIN(TstFakeNickel)
#include "tst_fake_nickel.moc"
