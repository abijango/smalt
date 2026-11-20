// The sync engine's pure helpers (task 21): spec values, the status
// vocabularies, payloads and the copy in the header and toasts.
#include "smalt/sync.h"
#include "sync/sync_text.h"

#include <QtTest>

using namespace smalt;
using namespace smalt::sync;

class TstSyncText : public QObject
{
    Q_OBJECT

private slots:

    void specValues()
    {
        QCOMPARE(kPreSleepHoldMs, 15000);
        QCOMPARE(kPreSleepMinBatteryPercent, 20);
        QCOMPARE(kPreSleepPullFreshSeconds, 6 * 3600);
        QCOMPARE(kRecentSuccessSkipSeconds, 5 * 60);
        QCOMPARE(kManualSyncTimeoutMs, 60000);
        QCOMPARE(kToastAfterConsecutiveFailures, 3);
    }

    void sixStatusesMapToBookOrbitAndBack()
    {
        const ReadStatus all[] = {
            ReadStatus::Unread, ReadStatus::WantToRead, ReadStatus::Reading,
            ReadStatus::OnHold, ReadStatus::Read,       ReadStatus::Abandoned
        };
        const char *wire[] = {
            "unread", "want_to_read", "reading", "on_hold", "read", "abandoned"
        };
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(bookOrbitStatus(all[i]), QLatin1String(wire[i]));
            QCOMPARE(int(statusFromBookOrbit(QLatin1String(wire[i]))), int(all[i]));
        }
        QVERIFY(bookOrbitStatus(ReadStatus::Unknown).isEmpty());
        QCOMPARE(int(statusFromBookOrbit(QStringLiteral("complete"))), int(ReadStatus::Unknown));
    }

    void statusesMapToNickelsThree()
    {
        QCOMPARE(nickelStatus(ReadStatus::Reading), kNickelReading);
        QCOMPARE(nickelStatus(ReadStatus::OnHold), kNickelReading);
        QCOMPARE(nickelStatus(ReadStatus::Read), kNickelFinished);
        QCOMPARE(nickelStatus(ReadStatus::Unread), kNickelUnread);
        QCOMPARE(nickelStatus(ReadStatus::WantToRead), kNickelUnread);
        QCOMPARE(nickelStatus(ReadStatus::Abandoned), -1);
        QCOMPARE(nickelStatus(ReadStatus::Unknown), -1);
    }

    void positionPayloadIsKosync()
    {
        const QByteArray p = positionPayload(QStringLiteral("ABCDEF0123456789ABCDEF0123456789"),
                                             54.0, 1700000000);
        RemoteProgress r;
        QVERIFY(parsePositionPayload(p, &r));
        QCOMPARE(r.document, QStringLiteral("abcdef0123456789abcdef0123456789"));
        QCOMPARE(r.percentage, 0.54);
        QCOMPARE(r.timestamp, qint64(1700000000));
        QVERIFY(r.progress.isEmpty());
        RemoteProgress clamp;
        QVERIFY(parsePositionPayload(positionPayload(QStringLiteral("aa"), 140.0, 1), &clamp));
        QCOMPARE(clamp.percentage, 1.0);
        QVERIFY(!parsePositionPayload("{}", nullptr));
    }

    void headerText()
    {
        const QDateTime now = QDateTime(QDate(2026, 11, 20), QTime(12, 0), Qt::LocalTime);
        SyncStatus s;
        s.online = true;
        s.lastSuccessUtc = QDateTime(QDate(2026, 11, 20), QTime(10, 42), Qt::LocalTime).toUTC();
        s.waiting = 3;
        QCOMPARE(sync::headerText(s, now), QStringLiteral("Synced 10:42 · 3 waiting"));
        s.online = false;
        QCOMPARE(sync::headerText(s, now), QStringLiteral("Offline · 3 waiting"));
        s.waiting = 0;
        QCOMPARE(sync::headerText(s, now), QStringLiteral("Offline"));
        s.online = true;
        QCOMPARE(sync::headerText(s, now), QStringLiteral("Synced 10:42"));
        s.running = true;
        QCOMPARE(sync::headerText(s, now), QStringLiteral("Syncing…"));
    }

    void summaryText()
    {
        SyncResult r;
        r.sessionsPushed = 2;
        r.highlightsPushed = 1;
        r.positionsPushed = 1;
        QCOMPARE(sync::summaryText(r),
                 QStringLiteral("Synced: 2 sessions, 1 highlight, 1 position"));
        QCOMPARE(sync::summaryText(SyncResult()), QStringLiteral("Synced: nothing new"));
    }

    void failureCopy()
    {
        QCOMPARE(failureText(ErrorKind::Offline),
                 QStringLiteral(
                         "Couldn't reach BookOrbit. Your reading is saved and will sync later."));
        QCOMPARE(failureText(ErrorKind::AuthRejected),
                 QStringLiteral("BookOrbit didn't accept your KOReader login. Check it in "
                                "BookOrbit's Settings > KOReader."));
    }
};

QTEST_GUILESS_MAIN(TstSyncText)
#include "tst_sync_text.moc"
