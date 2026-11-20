// Task 20 acceptance tests (docs/tasks/20-recorder.md and its addendum):
// FakeNickel, a real ledger in a temporary folder and a fake clock.
//
// SPEC: On-device data model ("How sessions are captured", "Sleeping inside a
// book", "Correcting a session"), Crash safety.
#include "fake_recorder_clock.h"

#include "../../src/recorder/recorder_internal.h"
#include "fakes/fake_nickel.h"
#include "smalt/ledger.h"
#include "smalt/paths.h"
#include "smalt/recorder.h"
#include "smalt/settings.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace smalt;
using smalt::testing::FakeNickel;
using smalt::testing::FakeRecorderClock;

namespace {

const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/Le Guin/The Dispossessed.epub");
const QString kOtherBook = QStringLiteral("file:///mnt/onboard/BookOrbit/Jemisin/Stone Sky.epub");
const QString kStoreBook = QStringLiteral("3f0c2d5e-7a41-4c3e-9a1b-0d6f2e8c1b55");
const QString kChapter5 = QStringLiteral("OEBPS/ch05.xhtml#kobo.1.1");

qint64 unixSecs(const QDateTime &t)
{
    return t.toMSecsSinceEpoch() / 1000;
}

ContentRow bookRow(const QString &contentId, double percent)
{
    ContentRow row;
    row.contentId = contentId;
    row.title = QStringLiteral("A book");
    row.filePath = QString(contentId).remove(QStringLiteral("file://"));
    row.mimeType = QStringLiteral("application/epub+zip");
    row.chapterIdBookmarked = kChapter5;
    row.percentRead = percent;
    row.timeSpentReading = 1000;
    return row;
}

} // namespace

class TstRecorder : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Ledger> m_ledger;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<FakeNickel> m_nickel;
    std::shared_ptr<FakeRecorderClock> m_clock;
    std::unique_ptr<Recorder> m_rec;
    QDateTime m_t0;

    void makeRecorder()
    {
        m_rec = recorder::createRecorderWith(m_ledger.get(), m_nickel.get(), m_settings.get(),
                                             m_clock);
        m_rec->start();
    }

    // Kobalt ends a reading interval: it adds the active seconds to
    // TimeSpentReading and writes the position (VERIFY-FINDINGS items 1, 2).
    void kobaltSaves(const QString &contentId, qint64 seconds, const QString &chapter,
                     double percent)
    {
        m_nickel->addReadingTime(contentId, seconds);
        m_nickel->setPosition(contentId, chapter, percent);
    }

    QVector<SessionRecord> sessions(const QString &contentId = kBook)
    {
        return m_ledger->sessionsForBook(contentId);
    }

    QVector<PageEventRecord> events(const QString &contentId = kBook)
    {
        return m_ledger->pageEventsAfter(contentId, 0);
    }

    void checkEvent(const PageEventRecord &e, int page, int total, const QDateTime &start,
                    int seconds)
    {
        QCOMPARE(e.page, page);
        QCOMPARE(e.totalPages, total);
        QCOMPARE(unixSecs(e.startUtc), unixSecs(start));
        QCOMPARE(e.durationSeconds, seconds);
    }

    void setNewSessionAfterSleep(int seconds)
    {
        Settings s = m_settings->current();
        s.newSessionAfterSleepSeconds = seconds;
        QVERIFY(m_settings->save(s).ok());
    }

private slots:

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        const Paths paths = Paths::forRoot(m_dir->path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        m_ledger = createLedger(paths);
        QVERIFY(m_ledger->open().ok());
        m_settings = createSettingsStore(paths);
        m_settings->load();
        m_nickel.reset(new FakeNickel);
        m_nickel->setReady(true);
        m_nickel->putContent(bookRow(kBook, 41.0));
        m_nickel->putContent(bookRow(kOtherBook, 5.0));
        m_nickel->putContent(bookRow(kStoreBook, 12.0));
        m_clock = std::make_shared<FakeRecorderClock>();
        m_t0 = m_clock->nowUtc();
        makeRecorder();
    }

    void cleanup()
    {
        m_rec.reset();
        if (m_ledger)
            m_ledger->close();
        m_ledger.reset();
        m_settings.reset();
        m_nickel.reset();
        m_dir.reset();
    }

    // Open -> read -> close: one session with the right times, active seconds
    // matching the TimeSpentReading delta, one page event per page left and the
    // page on screen ending at close.
    void openReadCloseIsOneSession()
    {
        QSignalSpy finished(m_rec.get(), SIGNAL(sessionFinished(QString, QString)));
        QSignalSpy closed(m_rec.get(), SIGNAL(bookClosedRecorded(QString)));
        QSignalSpy written(m_rec.get(), SIGNAL(pageEventWritten(QString)));

        m_nickel->simulateBookOpened(kBook);
        QCOMPARE(m_rec->openContentId(), kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(45);
        m_nickel->simulatePageTurn(3, 300);
        m_clock->advanceSecs(25);
        kobaltSaves(kBook, 100, QStringLiteral("OEBPS/ch06.xhtml#kobo.4.2"), 44.0);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        const SessionRecord s = all.first();
        QCOMPARE(s.state, SessionState::Finished);
        QCOMPARE(s.startUtc, m_t0);
        QCOMPARE(s.endUtc, m_t0.addSecs(100));
        QCOMPARE(s.activeSeconds, qint64(100)); // == the TimeSpentReading delta
        QCOMPARE(m_nickel->content(kBook).timeSpentReading - 1000, s.activeSeconds);
        QCOMPARE(s.start.percent, 41.0);
        QCOMPARE(s.start.locator, kChapter5);
        QCOMPARE(s.end.percent, 44.0);
        QCOMPARE(s.end.locator, QStringLiteral("OEBPS/ch06.xhtml#kobo.4.2"));
        QVERIFY(!s.approximate);
        QVERIFY(!s.estimated);
        QCOMPARE(s.pagesTurned, 2);
        QCOMPARE(s.startPage, 1);
        QCOMPARE(s.endPage, 3);
        QCOMPARE(s.totalPages, 300);

        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 3);
        checkEvent(ev[0], 1, 300, m_t0, 30);
        checkEvent(ev[1], 2, 300, m_t0.addSecs(30), 45);
        checkEvent(ev[2], 3, 300, m_t0.addSecs(75), 25);
        for (const PageEventRecord &e : ev)
            QCOMPARE(e.sessionUuid, s.uuid);

        QCOMPARE(written.count(), 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toString(), s.uuid);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.first().first().toString(), kBook);
        QVERIFY(m_rec->openContentId().isEmpty());

        ProgressRecord progress;
        QVERIFY(m_ledger->progress(kBook, &progress).ok());
        QCOMPARE(progress.local.percent, 44.0);
    }

    // Open -> sleep (shorter than "New session after sleep") -> wake -> close:
    // one session, sleep never counted. readyForSleep() comes after the
    // session so far is in the ledger.
    void shortSleepContinuesTheSession()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        kobaltSaves(kBook, 60, QStringLiteral("OEBPS/ch06.xhtml#kobo.1.1"), 43.0);

        bool checked = false;
        connect(m_rec.get(), &Recorder::readyForSleep, this, [&] {
            SessionRecord open;
            QVERIFY(m_ledger->openSession(&open).ok());
            QCOMPARE(open.endUtc, m_t0.addSecs(60));
            QCOMPARE(open.activeSeconds, qint64(60));
            QCOMPARE(open.end.percent, 43.0);
            QVERIFY(!open.approximate);
            QCOMPARE(events().size(), 2); // The page on screen ended at sleep.
            checked = true;
        });
        QSignalSpy ready(m_rec.get(), SIGNAL(readyForSleep()));
        m_nickel->simulateSleep();
        QCOMPARE(ready.count(), 1);
        QVERIFY(checked);
        QVERIFY(m_rec->openContentId().isEmpty());
        QCOMPARE(m_rec->currentPageStartUnix(), qint64(0));

        m_clock->suspend(10 * 60 * 1000);
        m_nickel->simulateWake();
        QCOMPARE(m_rec->openContentId(), kBook);
        m_clock->advanceSecs(40);
        kobaltSaves(kBook, 40, QStringLiteral("OEBPS/ch07.xhtml#kobo.2.1"), 45.0);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].state, SessionState::Finished);
        QCOMPARE(all[0].startUtc, m_t0);
        QCOMPARE(all[0].endUtc, m_t0.addSecs(60 + 600 + 40));
        QCOMPARE(all[0].activeSeconds, qint64(100));
        QCOMPARE(all[0].end.percent, 45.0);

        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 3);
        checkEvent(ev[0], 1, 300, m_t0, 30);
        checkEvent(ev[1], 2, 300, m_t0.addSecs(30), 30);
        checkEvent(ev[2], 2, 300, m_t0.addSecs(660), 40); // Same page, after wake.
    }

    // A sleep longer than the setting finishes the session at the moment of
    // sleep; reading after wake is a new session.
    void longSleepMakesTwoSessions()
    {
        QSignalSpy finished(m_rec.get(), SIGNAL(sessionFinished(QString, QString)));
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        kobaltSaves(kBook, 60, QStringLiteral("OEBPS/ch06.xhtml#kobo.1.1"), 43.0);
        m_nickel->simulateSleep();
        QCOMPARE(finished.count(), 0);

        m_clock->suspend(20 * 60 * 1000);
        m_nickel->simulateWake();
        QCOMPARE(finished.count(), 1);
        m_clock->advanceSecs(40);
        kobaltSaves(kBook, 40, QStringLiteral("OEBPS/ch07.xhtml#kobo.2.1"), 45.0);
        m_nickel->simulateBookClosed();
        QCOMPARE(finished.count(), 2);

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].startUtc, m_t0);
        QCOMPARE(all[0].endUtc, m_t0.addSecs(60)); // The moment of sleep.
        QCOMPARE(all[0].activeSeconds, qint64(60));
        QCOMPARE(all[0].end.percent, 43.0);
        QCOMPARE(all[1].startUtc, m_t0.addSecs(60 + 1200));
        QCOMPARE(all[1].endUtc, m_t0.addSecs(60 + 1200 + 40));
        QCOMPARE(all[1].activeSeconds, qint64(40));
        QCOMPARE(all[1].start.percent, 43.0);
        QCOMPARE(all[1].end.percent, 45.0);
        QVERIFY(!all[1].estimated);
        QVERIFY(all[0].uuid != all[1].uuid);

        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 3);
        QCOMPARE(ev[2].sessionUuid, all[1].uuid);
        checkEvent(ev[2], 2, 300, m_t0.addSecs(1260), 40);
    }

    // "Never": waking always continues the session.
    void neverSettingContinuesAfterAnySleep()
    {
        setNewSessionAfterSleep(kNewSessionAfterSleepNever);
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        m_nickel->simulateSleep();
        m_clock->suspend(13LL * 3600 * 1000);
        m_nickel->simulateWake();
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].activeSeconds, qint64(90));
        QCOMPARE(all[0].endUtc, m_t0.addSecs(60 + 13 * 3600 + 30));
    }

    // The device clock jumps while asleep: durations come from the monotonic
    // clock and how long the Kobo slept from the since-boot clock.
    void clockJumpWhileAsleepDoesNotDistort()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        m_nickel->simulateSleep();
        m_clock->suspend(5 * 60 * 1000);
        m_clock->jumpWall(3 * 3600 * 1000); // NTP after wake, say.
        m_nickel->simulateWake();
        const QDateTime woke = m_t0.addSecs(60 + 300 + 3 * 3600);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(3, 300);
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1); // 5 minutes asleep, whatever the wall clock says.
        QCOMPARE(all[0].activeSeconds, qint64(120));
        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 4);
        checkEvent(ev[0], 1, 300, m_t0, 30);
        checkEvent(ev[1], 2, 300, m_t0.addSecs(30), 30);
        checkEvent(ev[2], 2, 300, woke, 30);
        checkEvent(ev[3], 3, 300, woke.addSecs(30), 30);
    }

    // The other way: the wall clock barely moves (it went back while asleep),
    // but the Kobo slept 30 minutes, so the session ends at the sleep.
    void clockJumpBackDoesNotHideALongSleep()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        m_nickel->simulateSleep();
        m_clock->suspend(30 * 60 * 1000);
        m_clock->jumpWall(-29 * 60 * 1000);
        m_nickel->simulateWake();
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].activeSeconds, qint64(60));
        QCOMPARE(all[0].endUtc, m_t0.addSecs(60));
        QCOMPARE(all[1].activeSeconds, qint64(30));
    }

    // USB connect mid-session: the session so far is written and the ledger
    // closed; after USB the ledger reopens and reading continues.
    void usbMidSessionClosesLedgerSafely()
    {
        QSignalSpy closedSpy(m_rec.get(), SIGNAL(ledgerClosed()));
        QSignalSpy reopenedSpy(m_rec.get(), SIGNAL(ledgerReopened()));
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(20);

        m_nickel->simulateUsbConnected();
        QCOMPARE(closedSpy.count(), 1);
        QVERIFY(m_rec->ledgerClosedForUsb());
        QVERIFY(!m_ledger->isOpen());
        // Nothing reaches the closed ledger, and nothing crashes.
        m_nickel->simulatePageTurn(3, 300);
        m_nickel->simulateSleep();
        m_nickel->simulateWake();
        m_clock->advanceSecs(60);

        bool checked = false;
        connect(m_rec.get(), &Recorder::ledgerReopened, this, [&] {
            SessionRecord open;
            QVERIFY(m_ledger->openSession(&open).ok());
            QCOMPARE(open.activeSeconds, qint64(50)); // Written before the close.
            QCOMPARE(open.endUtc, m_t0.addSecs(50));
            checked = true;
        });
        m_nickel->simulateUsbDisconnected();
        QCOMPARE(reopenedSpy.count(), 1);
        QVERIFY(checked);
        QVERIFY(!m_rec->ledgerClosedForUsb());
        QVERIFY(m_ledger->isOpen());

        m_clock->advanceSecs(20);
        m_nickel->simulatePageTurn(3, 300);
        m_clock->advanceSecs(10);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].activeSeconds, qint64(80));
        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 4);
        checkEvent(ev[0], 1, 300, m_t0, 30);
        checkEvent(ev[1], 2, 300, m_t0.addSecs(30), 20);
        checkEvent(ev[2], 2, 300, m_t0.addSecs(110), 20);
        checkEvent(ev[3], 3, 300, m_t0.addSecs(130), 10);
    }

    // The book closes while the ledger is closed for USB (Kobalt releases the
    // book at USB export): the session is finished when the ledger reopens,
    // at the moment USB connected.
    void bookClosedDuringUsbIsFinishedAfter()
    {
        QSignalSpy closed(m_rec.get(), SIGNAL(bookClosedRecorded(QString)));
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(20);
        m_nickel->simulateUsbConnected();
        m_nickel->simulateBookClosed();
        QCOMPARE(closed.count(), 0);
        m_clock->advanceSecs(300);
        m_nickel->simulateUsbDisconnected();
        QCOMPARE(closed.count(), 1);

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].state, SessionState::Finished);
        QCOMPARE(all[0].endUtc, m_t0.addSecs(50));
        QCOMPARE(all[0].activeSeconds, qint64(50));
        QVERIFY(m_rec->openContentId().isEmpty());
    }

    // Flipping faster than a second: one event per start second.
    void pagesLeftWithinASecondJoinTheNext()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advance(300);
        m_nickel->simulatePageTurn(3, 300);
        m_clock->advance(400);
        m_nickel->simulatePageTurn(4, 300);
        m_clock->advance(20300);
        QCOMPARE(m_rec->currentPageStartUnix(), unixSecs(m_t0.addSecs(30)));
        m_nickel->simulateBookClosed();

        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 2);
        checkEvent(ev[0], 1, 300, m_t0, 30);
        // Pages 2 and 3 were left within the second they started (21:00:30):
        // their time joins page 4, which starts then.
        checkEvent(ev[1], 4, 300, m_t0.addSecs(30), 21);
        QCOMPARE(sessions()[0].pagesTurned, 3);
        QCOMPARE(sessions()[0].activeSeconds, qint64(51));
    }

    // The same-second rule: the page on screen's start second; 0 when no book
    // is being read.
    void currentPageStartFollowsThePageOnScreen()
    {
        QCOMPARE(m_rec->currentPageStartUnix(), qint64(0));
        m_nickel->simulateBookOpened(kBook);
        QCOMPARE(m_rec->currentPageStartUnix(), unixSecs(m_t0));
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        QCOMPARE(m_rec->currentPageStartUnix(), unixSecs(m_t0.addSecs(30)));
        m_nickel->simulateSleep();
        QCOMPARE(m_rec->currentPageStartUnix(), qint64(0));
        m_clock->suspend(60000);
        m_nickel->simulateWake();
        QCOMPARE(m_rec->currentPageStartUnix(), unixSecs(m_t0.addSecs(90)));
        m_nickel->simulateBookClosed();
        QCOMPARE(m_rec->currentPageStartUnix(), qint64(0));
    }

    // Kobalt hasn't saved at sleep: the last saved position is recorded as
    // approximate and corrected at wake; no time changes.
    void approximateEndIsCorrectedAtWake()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        m_nickel->simulateSleep();

        SessionRecord open;
        QVERIFY(m_ledger->openSession(&open).ok());
        QVERIFY(open.approximate);
        QCOMPARE(open.end.percent, 41.0);
        QCOMPARE(open.end.locator, kChapter5);

        m_clock->suspend(10 * 60 * 1000);
        kobaltSaves(kBook, 60, QStringLiteral("OEBPS/ch12.xhtml#kobo.3.1"), 54.0);
        m_nickel->simulateWake();

        SessionRecord after;
        QVERIFY(m_ledger->session(open.uuid, &after).ok());
        QVERIFY(!after.approximate);
        QCOMPARE(after.end.percent, 54.0);
        QCOMPARE(after.end.locator, QStringLiteral("OEBPS/ch12.xhtml#kobo.3.1"));
        QCOMPARE(after.startUtc, open.startUtc);
        QCOMPARE(after.endUtc, open.endUtc);
        QCOMPARE(after.activeSeconds, open.activeSeconds);
    }

    // The SPEC's example (21:00-21:47, 44 minutes, 41% -> 54%): a long sleep
    // finishes the session approximate; wake corrects it.
    void approximateEndOfAFinishedSessionIsCorrectedAtWake()
    {
        m_nickel->simulateBookOpened(kBook);
        for (int page = 2; page <= 45; ++page) {
            m_clock->advanceSecs(60);
            m_nickel->simulatePageTurn(page, 300);
        }
        m_clock->advanceSecs(3 * 60);
        m_nickel->simulateSleep(); // 21:47
        m_clock->suspend(43 * 60 * 1000);
        kobaltSaves(kBook, 47 * 60, QStringLiteral("OEBPS/ch12.xhtml#kobo.1.1"), 54.0);
        m_nickel->simulateWake(); // 22:30
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[1].state, SessionState::Dropped); // Woke and closed at once.
        QCOMPARE(all[0].state, SessionState::Finished);
        QCOMPARE(all[0].startUtc, m_t0);
        QCOMPARE(all[0].endUtc, m_t0.addSecs(47 * 60));
        QCOMPARE(all[0].activeSeconds, qint64(47 * 60));
        QCOMPARE(all[0].end.percent, 54.0);
        QVERIFY(!all[0].approximate);
    }

    // An approximate end at close is corrected at the next sleep.
    void approximateEndAtCloseIsCorrectedAtNextSleep()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();
        QVERIFY(sessions()[0].approximate);

        kobaltSaves(kBook, 60, QStringLiteral("OEBPS/ch06.xhtml#kobo.1.1"), 46.0);
        m_clock->advanceSecs(5);
        m_nickel->simulateSleep();
        QVERIFY(!sessions()[0].approximate);
        QCOMPARE(sessions()[0].end.percent, 46.0);
        QCOMPARE(sessions()[0].endUtc, m_t0.addSecs(60));
    }

    // Crash recovery: a session left open is finished at its last observation
    // (its last page event) before anything new starts.
    void crashLeavesOpenSessionThatStartFinishes()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(20);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(20);
        m_nickel->simulatePageTurn(3, 300);
        m_clock->advanceSecs(10);
        m_rec.reset(); // Nickel crashed: no stop(), nothing more written.

        SessionRecord open;
        QVERIFY(m_ledger->openSession(&open).ok());
        QCOMPARE(open.activeSeconds, qint64(0)); // The row lags behind the events.

        m_clock->advanceSecs(120); // Reboot.
        m_rec = recorder::createRecorderWith(m_ledger.get(), m_nickel.get(), m_settings.get(),
                                             m_clock);
        QSignalSpy finished(m_rec.get(), SIGNAL(sessionFinished(QString, QString)));
        m_rec->start();
        QCOMPARE(finished.count(), 1);

        SessionRecord done;
        QVERIFY(m_ledger->session(open.uuid, &done).ok());
        QCOMPARE(done.state, SessionState::Finished);
        QCOMPARE(done.endUtc, m_t0.addSecs(40));
        QCOMPARE(done.activeSeconds, qint64(40));

        // The book is still open in Nickel: a new session begins after the
        // recovery.
        SessionRecord fresh;
        QVERIFY(m_ledger->openSession(&fresh).ok());
        QVERIFY(fresh.uuid != open.uuid);
        QCOMPARE(fresh.startUtc, m_t0.addSecs(170));
    }

    // stop() writes the session so far; the next start() finishes it.
    void stopWritesTheSessionSoFar()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(20);
        m_nickel->simulatePageTurn(2, 300);
        m_clock->advanceSecs(15);
        m_rec->stop();

        SessionRecord open;
        QVERIFY(m_ledger->openSession(&open).ok());
        QCOMPARE(open.activeSeconds, qint64(35));
        QCOMPARE(open.endUtc, m_t0.addSecs(35));
        // Disconnected: signals no longer reach it.
        m_nickel->simulatePageTurn(3, 300);
        QCOMPARE(events().size(), 2);

        m_nickel->simulateBookClosed();
        makeRecorder();
        SessionRecord done;
        QVERIFY(m_ledger->session(open.uuid, &done).ok());
        QCOMPARE(done.state, SessionState::Finished);
        QCOMPARE(done.activeSeconds, qint64(35));
    }

    // Power-off finishes the session.
    void powerOffFinishesTheSession()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        emit m_nickel->aboutToPowerOff();
        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].state, SessionState::Finished);
        QCOMPARE(all[0].activeSeconds, qint64(30));
    }

    // No page turn observed in the session: page events built from the
    // session's start, active time and percentages; marked estimated.
    void withoutPageTurnsEventsAreSynthesised()
    {
        m_nickel->putContent(bookRow(kOtherBook, 10.0));
        m_nickel->simulateBookOpened(kOtherBook);
        m_clock->advanceSecs(300);
        kobaltSaves(kOtherBook, 300, QStringLiteral("OEBPS/ch02.xhtml#kobo.7.1"), 13.0);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions(kOtherBook);
        QCOMPARE(all.size(), 1);
        QVERIFY(all[0].estimated);
        QCOMPARE(all[0].activeSeconds, qint64(300));
        QCOMPARE(all[0].end.percent, 13.0);
        const QVector<PageEventRecord> ev = events(kOtherBook);
        QCOMPARE(ev.size(), 4);
        checkEvent(ev[0], 10, 100, m_t0, 75);
        checkEvent(ev[1], 11, 100, m_t0.addSecs(75), 75);
        checkEvent(ev[2], 12, 100, m_t0.addSecs(150), 75);
        checkEvent(ev[3], 13, 100, m_t0.addSecs(225), 75);
    }

    // The bridge can't tell page numbers (pageTurned with page -1): pages on
    // the percentage scale.
    void unknownPageNumbersUseThePercentageScale()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(-1, 0);
        m_clock->advanceSecs(40);
        m_nickel->simulatePageTurn(-1, 0);
        m_clock->advanceSecs(20);
        m_nickel->simulateBookClosed();

        const QVector<PageEventRecord> ev = events();
        QCOMPARE(ev.size(), 3);
        checkEvent(ev[0], 41, 100, m_t0, 30);
        checkEvent(ev[1], 41, 100, m_t0.addSecs(30), 40);
        checkEvent(ev[2], 41, 100, m_t0.addSecs(70), 20);
        QVERIFY(!sessions()[0].estimated);
    }

    // Page turns were observed earlier (the hook works): a session spent on one
    // page is that one page, not an estimate.
    void onePageSessionIsNotEstimatedOnceTurnsWereSeen()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulatePageTurn(-1, 0);
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();
        m_nickel->simulateBookOpened(kOtherBook);
        m_clock->advanceSecs(120);
        m_nickel->simulateBookClosed();

        const QVector<SessionRecord> all = sessions(kOtherBook);
        QCOMPARE(all.size(), 1);
        QVERIFY(!all[0].estimated);
        const QVector<PageEventRecord> ev = events(kOtherBook);
        QCOMPARE(ev.size(), 1);
        checkEvent(ev[0], 5, 100, m_t0.addSecs(60), 120);
    }

    // Kobo store books aren't recorded; a book in book_map is, whatever its
    // ContentID.
    void storeBooksAreNotRecorded()
    {
        QSignalSpy closed(m_rec.get(), SIGNAL(bookClosedRecorded(QString)));
        m_nickel->simulateBookOpened(kStoreBook);
        QVERIFY(m_rec->openContentId().isEmpty());
        m_clock->advanceSecs(60);
        m_nickel->simulatePageTurn(2, 300);
        m_nickel->simulateBookClosed();
        QVERIFY(sessions(kStoreBook).isEmpty());
        QCOMPARE(closed.count(), 0);

        BookMapEntry entry;
        entry.contentId = kStoreBook;
        entry.bookId = 7;
        entry.bookFileId = 70;
        entry.origin = BookOrigin::Migrated;
        entry.createdUtc = m_t0;
        QVERIFY(m_ledger->putBookMap(entry).ok());
        m_nickel->simulateBookOpened(kStoreBook);
        m_clock->advanceSecs(60);
        m_nickel->simulateBookClosed();
        QCOMPARE(sessions(kStoreBook).size(), 1);
        QCOMPARE(closed.count(), 1);
    }

    // Under 10 seconds of reading: finished as Dropped by the ledger.
    void shortSessionIsDropped()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(5);
        m_nickel->simulateBookClosed();
        const QVector<SessionRecord> all = sessions();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].state, SessionState::Dropped);
    }

    // readyForSleep() also comes with no book open.
    void readyForSleepWithoutABook()
    {
        QSignalSpy ready(m_rec.get(), SIGNAL(readyForSleep()));
        m_nickel->simulateSleep();
        QCOMPARE(ready.count(), 1);
        m_nickel->simulateWake();
        QVERIFY(sessions().isEmpty());
    }

    // Opening another book without a close finishes the first.
    void anotherBookOpeningFinishesTheFirst()
    {
        m_nickel->simulateBookOpened(kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulateBookOpened(kOtherBook);
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();
        QCOMPARE(sessions(kBook).size(), 1);
        QCOMPARE(sessions(kBook)[0].state, SessionState::Finished);
        QCOMPARE(sessions(kBook)[0].activeSeconds, qint64(30));
        QCOMPARE(sessions(kOtherBook).size(), 1);
        QCOMPARE(sessions(kOtherBook)[0].activeSeconds, qint64(30));
    }

    // A book already open when the recorder starts is recorded from then.
    void bookOpenAtStartIsRecorded()
    {
        m_rec.reset();
        m_nickel->simulateBookOpened(kBook);
        makeRecorder();
        QCOMPARE(m_rec->openContentId(), kBook);
        m_clock->advanceSecs(30);
        m_nickel->simulateBookClosed();
        QCOMPARE(sessions().size(), 1);
        QCOMPARE(sessions()[0].activeSeconds, qint64(30));
    }
};

QTEST_GUILESS_MAIN(TstRecorder)
#include "tst_recorder.moc"
