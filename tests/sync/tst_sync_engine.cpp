// Acceptance tests for the sync engine (docs/tasks/21-sync-engine.md): the
// real client against the mock BookOrbit, a real ledger and outbox in a
// temporary dir, FakeNickel, FakeRecorder and a fake clock.
#include "sync_fixture.h"

#include "sync/sync_text.h"

using namespace smalt;
using synctest::Harness;
using synctest::kBook1;
using synctest::kBook2;

class TstSyncEngine : public QObject
{
    Q_OBJECT

    ::testing::MockServer m_mock;
    std::unique_ptr<Harness> h;

    QDateTime hoursAgo(int hours) const
    {
        return QDateTime::currentDateTimeUtc().addSecs(-hours * 3600);
    }

    QJsonObject rule(const QString &path, const QString &method = QString())
    {
        return ::testing::MockServer::rule(path, method);
    }

private slots:

    void initTestCase()
    {
        const QString err = m_mock.start();
        QVERIFY2(err.isEmpty(), qPrintable(err));
    }

    void init()
    {
        m_mock.reset();
        h.reset(new Harness(&m_mock));
        const QString err = h->setUp();
        QVERIFY2(err.isEmpty(), qPrintable(err));
        m_mock.requests(true);
    }

    void cleanup() { h.reset(); }

    // --- Each trigger runs the right work and respects its limits ---

    void bookClosedPushesThatBookAndRestoresWifi()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->read(kBook2, hoursAgo(2), 3);
        QVERIFY(!h->nickel->wifiEnabled());

        h->recorder->fireBookClosed(kBook1);
        const SyncResult r = h->waitResult(0);
        QCOMPARE(int(r.trigger), int(SyncTrigger::BookClosed));
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(r.sessionsPushed, 1);
        QCOMPARE(r.positionsPushed, 1);
        QCOMPARE(h->serverEvents(h->hash1), 4);
        QCOMPARE(h->serverEvents(h->hash2), 0); // Only that book.
        QVERIFY(h->allSent(kBook1));
        QVERIFY(!h->allSent(kBook2));
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/sweeps")), 1);
        // Wi-Fi was off: turned on for the sync and off again.
        QCOMPARE(h->nickel->wifiEnableCalls, 1);
        QCOMPARE(h->nickel->wifiDisableCalls, 1);
        QVERIFY(!h->nickel->wifiEnabled());
    }

    void bookClosedWithNothingQueuedKeepsWifiOff()
    {
        h->recorder->fireBookClosed(kBook1);
        const SyncResult r = h->waitResult(0);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Skipped));
        QCOMPARE(h->nickel->wifiEnableCalls, 0);
        QCOMPARE(m_mock.requests().size(), 0);
    }

    void beforeSleepHoldsSleepPushesEverythingAndReleases()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->read(kBook2, hoursAgo(2), 3);
        int holdsDuring = -1;
        QObject::connect(h->engine.get(), &SyncEngine::started, [this, &holdsDuring](SyncTrigger) {
            holdsDuring = h->nickel->sleepHolds;
        });
        h->recorder->fireReadyForSleep();
        QCOMPARE(h->nickel->sleepHolds, 1); // Held at once, before the sync starts.
        const SyncResult r = h->waitResult(0);
        QCOMPARE(int(r.trigger), int(SyncTrigger::BeforeSleep));
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(holdsDuring, 1);
        QCOMPARE(h->nickel->sleepHolds, 0);
        QCOMPARE(r.sessionsPushed, 2);
        QCOMPARE(r.positionsPushed, 2);
        QVERIFY(h->allSent(kBook1));
        QVERIFY(h->allSent(kBook2));
        // Quick pull of remote positions only.
        QVERIFY(h->requestsTo(QStringLiteral("/syncs/progress/") + h->hash1, QStringLiteral("GET"))
                >= 1);
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/catalog/books/1")), 0);
        QVERIFY(!h->nickel->wifiEnabled());
        QVERIFY(h->ledger->lastPullUtc().isValid());
    }

    void beforeSleepSkipsBelowBatteryThreshold()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->nickel->setBatteryPercent(19);
        h->recorder->fireReadyForSleep();
        QCOMPARE(h->results.size(), 1);
        QCOMPARE(int(h->results.last().outcome), int(SyncOutcome::Skipped));
        QVERIFY(h->results.last().skipReason.contains(QStringLiteral("battery")));
        QCOMPARE(h->nickel->sleepHolds, 0);
        QCOMPARE(h->nickel->wifiEnableCalls, 0);
    }

    void beforeSleepSkipsWithEmptyOutboxAndFreshPull()
    {
        const QDateTime now = h->clock->nowUtc();
        QVERIFY(h->ledger->setPullCursor(QStringLiteral("x"), now.addSecs(-3600)).ok());
        h->recorder->fireReadyForSleep();
        QCOMPARE(h->results.size(), 1);
        QCOMPARE(int(h->results.last().outcome), int(SyncOutcome::Skipped));
        QCOMPARE(h->nickel->sleepHolds, 0);

        // A pull older than 6 h: the pre-sleep sync runs.
        QVERIFY(h->ledger->setPullCursor(QStringLiteral("x"), now.addSecs(-7 * 3600)).ok());
        h->recorder->fireReadyForSleep();
        const SyncResult r = h->waitResult(1);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->nickel->sleepHolds, 0);
    }

    void wakePullsFirstThenPushes()
    {
        h->read(kBook1, hoursAgo(3), 4);
        const SyncResult r = h->run(SyncTrigger::Wake);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        const QStringList order = h->requestOrder();
        const int pull = order.indexOf(QStringLiteral("GET /syncs/progress/") + h->hash1);
        const int push = order.indexOf(QStringLiteral("POST /plugin/page-stats"));
        QVERIFY2(pull >= 0 && push >= 0 && pull < push, qPrintable(order.join(QLatin1Char('|'))));
        QVERIFY(h->allSent(kBook1));
    }

    void wifiAlreadyOnRunsFullSyncAndLeavesWifiOn()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->nickel->simulateNetwork(true);
        const SyncResult r = h->waitResult(0);
        QCOMPARE(int(r.trigger), int(SyncTrigger::WifiAlreadyOn));
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QVERIFY(h->allSent(kBook1));
        QVERIFY(h->requestsTo(QStringLiteral("/plugin/catalog/books/1")) >= 1); // Status pull.
        QCOMPARE(h->nickel->wifiEnableCalls, 0);
        QCOMPARE(h->nickel->wifiDisableCalls, 0);
        QVERIFY(h->nickel->networkConnected());
    }

    void recentSuccessSkipsBackgroundTriggersButNotSyncNow()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));
        h->clock->advance(60 * 1000);
        QCOMPARE(int(h->run(SyncTrigger::Wake).outcome), int(SyncOutcome::Skipped));
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));
        // After 5 minutes the background trigger runs again.
        h->clock->advance(6 * 60 * 1000);
        h->nickel->simulateNetwork(true);
        QCOMPARE(int(h->waitResult(h->results.size()).outcome), int(SyncOutcome::Succeeded));
    }

    void manualSyncToastsTheSummary()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QVector<QString> progress;
        QObject::connect(h->engine.get(), &SyncEngine::progress,
                         [&progress](const QString &t) { progress << t; });
        const SyncResult r = h->run(SyncTrigger::Manual);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->nickel->toasts,
                 QStringList() << QStringLiteral("Synced: 1 session, 1 position"));
        QVERIFY(!progress.isEmpty());
        const SyncStatus st = h->engine->status();
        QVERIFY(st.lastSuccessUtc.isValid());
        QCOMPARE(st.waiting, 0);
    }

    // --- Push while reading ---

    void whileReadingOnlyWhenConnectedNeverTurnsWifiOn()
    {
        h->readOpen(kBook1, hoursAgo(1), 5);
        const SyncResult r = h->run(SyncTrigger::WhileReading, kBook1);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Skipped));
        QCOMPARE(h->nickel->wifiEnableCalls, 0);
        QCOMPARE(m_mock.requests().size(), 0);
    }

    void whileReadingSendsOnlyFinishedPagesAndOneSweep()
    {
        const QDateTime start = hoursAgo(1);
        h->readOpen(kBook1, start, 5);
        // An event in the same second as the page on screen waits (same-second rule).
        h->nickel->simulateNetwork(true);
        h->waitResult(0); // The Wi-Fi trigger's own sync; clear the slate.
        m_mock.reset();
        m_mock.requests(true);
        h->recorder->open = kBook1;

        // Five more pages; the page on screen starts at the 10th page's second.
        SessionRecord open;
        QVERIFY(h->ledger->openSession(&open).ok());
        for (int i = 5; i < 10; ++i) {
            PageEventRecord e;
            e.sessionUuid = open.uuid;
            e.contentId = kBook1;
            e.page = 1 + i;
            e.totalPages = 300;
            e.startUtc = start.addSecs(i * 60);
            e.durationSeconds = 60;
            QVERIFY(h->ledger->appendPageEvent(e).ok());
        }
        // The last of them starts in the same second as the page on screen.
        h->recorder->pageStart = start.addSecs(9 * 60).toMSecsSinceEpoch() / 1000;
        const int before = h->results.size();
        h->recorder->firePageEventWritten(kBook1);
        h->clock->advance(5 * 60 * 1000); // The live-push timer (default 5 minutes).
        const SyncResult r = h->waitResult(before);
        QCOMPARE(int(r.trigger), int(SyncTrigger::WhileReading));
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/page-stats")), 1);
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/sweeps")), 1);
        QCOMPARE(h->requestsTo(QStringLiteral("/syncs/progress"), QStringLiteral("PUT")), 0);
        BookMapEntry m;
        QVERIFY(h->ledger->bookMapByContentId(kBook1, &m).ok());
        QCOMPARE(m.statsWatermark, start.addSecs(8 * 60).toMSecsSinceEpoch() / 1000);
        QCOMPARE(h->ledger->pageEventsAfter(kBook1, m.statsWatermark).size(), 1);
    }

    void whileReadingConnectBrieflyRestoresWifi()
    {
        Settings s = h->settings->current();
        s.livePushMode = LivePushMode::ConnectBriefly;
        s.livePushMinutes = 15;
        QVERIFY(h->settings->save(s).ok());
        h->readOpen(kBook1, hoursAgo(1), 5);
        const SyncResult r = h->run(SyncTrigger::WhileReading, kBook1);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->serverEvents(h->hash1), 5);
        QCOMPARE(h->nickel->wifiEnableCalls, 1);
        QCOMPARE(h->nickel->wifiDisableCalls, 1);
        QVERIFY(!h->nickel->wifiEnabled());
    }

    // --- Cut off at any point: nothing lost, nothing duplicated ---

    void watchdogCutsThePreSleepSyncAndRetryLosesNothing()
    {
        h->read(kBook1, hoursAgo(3), 6);
        QJsonObject slow = rule(QStringLiteral("/plugin/page-stats"));
        slow.insert(QStringLiteral("delay_ms"), 3000);
        slow.insert(QStringLiteral("times"), 1);
        m_mock.setRules(QJsonArray() << slow);

        h->recorder->fireReadyForSleep();
        QCOMPARE(h->nickel->sleepHolds, 1);
        QTRY_VERIFY(h->requestsTo(QStringLiteral("/plugin/page-stats")) == 1);
        h->clock->advance(kPreSleepHoldMs); // The watchdog's 15 s.
        const SyncResult cut = h->waitResult(0);
        QVERIFY(cut.outcome == SyncOutcome::Failed || cut.outcome == SyncOutcome::Partial);
        QCOMPARE(int(cut.error), int(ErrorKind::Timeout));
        QCOMPARE(h->nickel->sleepHolds, 0);
        QVERIFY(!h->nickel->wifiEnabled()); // Wi-Fi put back before sleeping.

        // Retry at once, while the slow request may still be in flight.
        h->clock->advance(10 * 60 * 1000);
        const SyncResult again = h->run(SyncTrigger::Manual);
        QCOMPARE(int(again.outcome), int(SyncOutcome::Succeeded));
        QTest::qWait(3500); // Let the slow request land too.
        QCOMPARE(h->serverEvents(h->hash1), 6);
        QVERIFY(h->allSent(kBook1));
        QCOMPARE(m_mock.state().value(QStringLiteral("progress")).toObject().size(), 1);
    }

    void droppedConnectionAfterTheServerStoredLosesAndDuplicatesNothing()
    {
        h->read(kBook1, hoursAgo(3), 6);
        QJsonObject lost = rule(QStringLiteral("/plugin/page-stats"));
        lost.insert(QStringLiteral("after_processing"), true);
        lost.insert(QStringLiteral("drop"), true);
        lost.insert(QStringLiteral("times"), 1);
        QJsonObject lostPut = rule(QStringLiteral("/syncs/progress"), QStringLiteral("PUT"));
        lostPut.insert(QStringLiteral("after_processing"), true);
        lostPut.insert(QStringLiteral("drop"), true);
        lostPut.insert(QStringLiteral("times"), 1);
        m_mock.setRules(QJsonArray() << lost << lostPut);

        const SyncResult first = h->run(SyncTrigger::Manual);
        QVERIFY(first.outcome != SyncOutcome::Succeeded);
        QVERIFY(!h->allSent(kBook1)); // The acknowledgement never arrived.
        QVERIFY(!h->nickel->wifiEnabled());

        h->clock->advance(10 * 60 * 1000);
        const SyncResult second = h->run(SyncTrigger::Manual);
        QVERIFY(second.outcome != SyncOutcome::Succeeded); // The lost PUT.
        QVERIFY(h->allSent(kBook1));
        QCOMPARE(h->serverEvents(h->hash1), 6); // The resent events were duplicates.

        h->clock->advance(7 * 3600 * 1000LL); // Past the position's backoff.
        const SyncResult third = h->run(SyncTrigger::Manual);
        QCOMPARE(int(third.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->outbox->pendingCount(), 0);
        QCOMPARE(h->serverEvents(h->hash1), 6);
    }

    void timeoutRestoresWifi()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QJsonObject slow = rule(QStringLiteral("/plugin/page-stats"));
        slow.insert(QStringLiteral("delay_ms"), 2000);
        m_mock.setRules(QJsonArray() << slow);
        const int before = h->results.size();
        SyncRequest r;
        r.trigger = SyncTrigger::Manual;
        h->engine->request(r);
        QTRY_VERIFY(h->requestsTo(QStringLiteral("/plugin/page-stats")) == 1);
        QVERIFY(h->nickel->wifiEnabled());
        h->clock->advance(kManualSyncTimeoutMs);
        const SyncResult res = h->waitResult(before);
        QCOMPARE(int(res.error), int(ErrorKind::Timeout));
        QVERIFY(!h->nickel->wifiEnabled());
        QCOMPARE(h->nickel->wifiDisableCalls, 1);
        QCOMPARE(h->nickel->toasts.size(), 1); // "Sync now" always says how it went.
        QTest::qWait(2200);
    }

    void wifiThatNeverConnectsIsPutBack()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->nickel->setWifiConnectSucceeds(false);
        const int before = h->results.size();
        SyncRequest r;
        r.trigger = SyncTrigger::Manual;
        h->engine->request(r);
        QTRY_VERIFY(h->nickel->wifiEnableCalls == 1);
        h->clock->advance(sync::kWifiConnectTimeoutMs);
        const SyncResult res = h->waitResult(before);
        QCOMPARE(int(res.outcome), int(SyncOutcome::Failed));
        QCOMPARE(int(res.error), int(ErrorKind::Offline));
        QVERIFY(!h->nickel->wifiEnabled());
    }

    void abortPutsWifiBack()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QJsonObject slow = rule(QStringLiteral("/plugin/page-stats"));
        slow.insert(QStringLiteral("delay_ms"), 1500);
        m_mock.setRules(QJsonArray() << slow);
        const int before = h->results.size();
        SyncRequest r;
        r.trigger = SyncTrigger::Manual;
        h->engine->request(r);
        QTRY_VERIFY(h->requestsTo(QStringLiteral("/plugin/page-stats")) == 1);
        h->engine->abort();
        const SyncResult res = h->waitResult(before);
        QCOMPARE(int(res.error), int(ErrorKind::Cancelled));
        QVERIFY(!h->nickel->wifiEnabled());
        QTest::qWait(1700);
    }

    // --- Writes for an open book wait until close ---

    void pulledStatusWaitsForTheBookToClose()
    {
        // BookOrbit says book 1 is read (set from another device).
        bool done = false;
        h->client->setReadStatus(1, QStringLiteral("read"),
                                 [&done](const Reply<bool> &) { done = true; });
        QTRY_VERIFY(done);
        m_mock.requests(true);

        h->readOpen(kBook1, hoursAgo(1), 3);
        h->nickel->simulateNetwork(true); // Wi-Fi on: full sync while the book is open.
        const SyncResult r = h->waitResult(0);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        BookStateRecord st;
        QVERIFY(h->ledger->bookState(kBook1, &st).ok());
        QCOMPARE(int(st.status), int(ReadStatus::Read));
        QCOMPARE(h->nickel->content(kBook1).readStatus, 0); // Not written while open.

        h->nickel->simulateBookClosed();
        h->recorder->open.clear();
        h->recorder->fireBookClosed(kBook1);
        QCOMPARE(h->nickel->content(kBook1).readStatus, sync::kNickelFinished);
        h->waitResult(1);
    }

    void remotePositionIsStoredNotApplied()
    {
        h->read(kBook1, hoursAgo(5), 2);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));

        // Another device moves ahead.
        RemoteProgress p;
        p.document = h->hash1;
        p.percentage = 0.54;
        p.progress = QStringLiteral("/body/DocFragment[12]/body/p[3]");
        p.device = QStringLiteral("Phone");
        p.deviceId = QStringLiteral("phone-1");
        p.timestamp = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000 - 7200;
        bool done = false;
        h->client->putProgress(p, [&done](const Reply<bool> &) { done = true; });
        QTRY_VERIFY(done);

        h->clock->advance(10 * 60 * 1000);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));
        ProgressRecord rec;
        QVERIFY(h->ledger->progress(kBook1, &rec).ok());
        QVERIFY(rec.hasRemote);
        QCOMPARE(rec.remote.locator, p.progress);
        QCOMPARE(qRound(rec.remote.percent), 54);
        QCOMPARE(rec.remoteDevice, QStringLiteral("Phone"));
        QCOMPARE(qRound(rec.local.percent), 42); // The local position is untouched.
        QVERIFY(h->nickel->content(kBook1).chapterIdBookmarked.isEmpty()); // Never applied.
    }

    void ownPositionIsNotStoredAsRemote()
    {
        h->read(kBook1, hoursAgo(5), 2);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));
        ProgressRecord rec;
        QVERIFY(h->ledger->progress(kBook1, &rec).ok());
        QVERIFY(!rec.hasRemote);
    }

    // --- Ordering and rejection ---

    void rejectedBookDoesNotBlockTheOthers()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->read(kBook2, hoursAgo(2), 3);
        QJsonObject bad = rule(QStringLiteral("/plugin/page-stats"));
        bad.insert(QStringLiteral("body_contains"), h->hash1);
        bad.insert(QStringLiteral("status"), 400);
        m_mock.setRules(QJsonArray() << bad);

        const SyncResult r = h->run(SyncTrigger::Manual);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->serverEvents(h->hash1), 0);
        QCOMPARE(h->serverEvents(h->hash2), 3);
        // Book 1's watermark moved past the refused events, so its position went.
        QVERIFY(h->allSent(kBook1));
        QCOMPARE(h->ledger->pageEventsAfter(kBook1, 0).size(), 4); // Kept in the ledger.
        const QJsonObject progress = m_mock.state().value(QStringLiteral("progress")).toObject();
        QVERIFY(progress.contains(h->hash1));
        QVERIFY(progress.contains(h->hash2));
    }

    void positionWaitsBehindUnsentPageEvents()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QJsonObject down = rule(QStringLiteral("/plugin/page-stats"));
        down.insert(QStringLiteral("status"), 500);
        m_mock.setRules(QJsonArray() << down);

        const SyncResult r = h->run(SyncTrigger::Manual);
        QVERIFY(r.outcome != SyncOutcome::Succeeded);
        QCOMPARE(h->requestsTo(QStringLiteral("/syncs/progress"), QStringLiteral("PUT")), 0);

        // Even a sync that skips page events entirely (the page-stats outage
        // goes on) never sends the position first.
        h->clock->advance(10 * 60 * 1000);
        h->run(SyncTrigger::Manual);
        QCOMPARE(h->requestsTo(QStringLiteral("/syncs/progress"), QStringLiteral("PUT")), 0);

        m_mock.clearRules();
        h->clock->advance(10 * 60 * 1000);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));
        const QStringList order = h->requestOrder();
        QVERIFY(order.lastIndexOf(QStringLiteral("POST /plugin/page-stats"))
                < order.indexOf(QStringLiteral("PUT /syncs/progress")));
    }

    void sweepOwedSurvivesARestart()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QJsonObject down = rule(QStringLiteral("/plugin/sweeps"));
        down.insert(QStringLiteral("status"), 500);
        m_mock.setRules(QJsonArray() << down);
        h->run(SyncTrigger::Manual);
        QVERIFY(h->allSent(kBook1));
        QVERIFY(h->ledger->sweepOwed());

        // Restart: a new engine over the ledger reopened from disk.
        h->engine.reset();
        h->ledger->close();
        QVERIFY(h->ledger->open().ok());
        QVERIFY(h->ledger->sweepOwed());
        h->makeEngine();
        m_mock.clearRules();
        m_mock.requests(true);
        h->clock->advance(10 * 60 * 1000);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/sweeps")), 1);
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/page-stats")), 0);
        QVERIFY(!h->ledger->sweepOwed());
    }

    void statusChangeIsPushedInBookOrbitsVocabulary()
    {
        QVERIFY(h->ledger
                        ->setLocalStatus(kBook1, ReadStatus::OnHold,
                                         QDateTime::currentDateTimeUtc())
                        .ok());
        QVERIFY(h->ledger->setLocalRating(kBook1, 4, QDateTime::currentDateTimeUtc()).ok());
        h->read(kBook1, hoursAgo(3), 2); // So book 1 is among the recent books.
        const SyncResult r = h->run(SyncTrigger::Manual);
        QCOMPARE(int(r.outcome), int(SyncOutcome::Succeeded));
        QCOMPARE(r.statusesPushed, 1);
        const QJsonObject state = m_mock.state();
        QCOMPARE(state.value(QStringLiteral("readStatus"))
                         .toObject()
                         .value(QStringLiteral("1"))
                         .toString(),
                 QStringLiteral("on_hold"));
        QCOMPARE(state.value(QStringLiteral("ratings"))
                         .toObject()
                         .value(QStringLiteral("1"))
                         .toInt(),
                 4);
        // Not sent again, and the pull doesn't take it for a remote change.
        m_mock.requests(true);
        h->clock->advance(10 * 60 * 1000);
        QCOMPARE(int(h->run(SyncTrigger::Manual).statusesPushed), 0);
        QCOMPARE(h->requestsTo(QStringLiteral("/plugin/catalog/books/1/read-status")), 0);
        BookStateRecord st;
        QVERIFY(h->ledger->bookState(kBook1, &st).ok());
        QCOMPARE(int(st.status), int(ReadStatus::OnHold));
    }

    // --- Switches and startup ---

    void disableSyncStopsEverything()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->touch(h->paths->disableSyncSwitch());
        h->run(SyncTrigger::Manual);
        h->recorder->fireBookClosed(kBook1);
        h->recorder->fireReadyForSleep();
        h->nickel->simulateNetwork(true);
        h->nickel->simulateWake();
        QTest::qWait(300);
        for (const SyncResult &r : h->results)
            QCOMPARE(int(r.outcome), int(SyncOutcome::Skipped));
        QVERIFY(h->results.size() >= 5);
        QCOMPARE(m_mock.requests().size(), 0);
        QCOMPARE(h->nickel->sleepHolds, 0);
        QCOMPARE(h->nickel->wifiEnableCalls, 0);
    }

    void neverRunsDuringStartup()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->nickel->setReady(false);
        QCOMPARE(int(h->run(SyncTrigger::Manual).outcome), int(SyncOutcome::Skipped));
        h->nickel->setReady(true);

        // The startup quiet period holds back every background trigger.
        sync::EngineDeps d;
        d.ledger = h->ledger.get();
        d.outbox = h->outbox.get();
        d.client = h->client.get();
        d.nickel = h->nickel.get();
        d.safety = h->safety.get();
        d.recorder = h->recorder.get();
        d.settings = h->settings.get();
        d.clock = h->clock;
        std::unique_ptr<SyncEngine> e(sync::createSyncEngineWith(d));
        QVector<SyncResult> got;
        QObject::connect(e.get(), &SyncEngine::finished, [&got](const SyncResult &r) { got << r; });
        SyncRequest r;
        r.trigger = SyncTrigger::Wake;
        e->request(r);
        QTRY_VERIFY(!got.isEmpty());
        QCOMPARE(int(got.last().outcome), int(SyncOutcome::Skipped));
        QVERIFY(got.last().skipReason.contains(QStringLiteral("startup")));
        QCOMPARE(m_mock.requests().size(), 0);
    }

    void sameFailureThreeTimesInARowToastsOnce()
    {
        h->read(kBook1, hoursAgo(3), 4);
        QJsonObject down = rule(QStringLiteral("/plugin/page-stats"));
        down.insert(QStringLiteral("status"), 500);
        m_mock.setRules(QJsonArray() << down);
        for (int i = 0; i < 4; ++i) {
            const SyncResult r = h->run(SyncTrigger::Wake);
            QCOMPARE(int(r.error), int(ErrorKind::ServerError));
            QCOMPARE(h->nickel->toasts.size(), i >= 2 ? 1 : 0);
            h->clock->advance(10 * 60 * 1000);
        }
        QCOMPARE(h->nickel->toasts.first(),
                 QStringLiteral(
                         "Couldn't reach BookOrbit. Your reading is saved and will sync later."));
        QCOMPARE(h->engine->status().consecutiveFailures, 4);
        QVERIFY(h->engine->status().waiting > 0);
    }

    void closeThenSleepCollapseIntoOneConnection()
    {
        h->read(kBook1, hoursAgo(3), 4);
        h->read(kBook2, hoursAgo(2), 3);
        QJsonObject slow = rule(QStringLiteral("/plugin/page-stats"));
        slow.insert(QStringLiteral("delay_ms"), 500);
        slow.insert(QStringLiteral("times"), 1);
        m_mock.setRules(QJsonArray() << slow);
        h->recorder->fireBookClosed(kBook1);
        QTRY_VERIFY(h->requestsTo(QStringLiteral("/plugin/page-stats")) == 1);
        h->recorder->fireReadyForSleep(); // Sleep while the close sync runs.
        QCOMPARE(h->nickel->sleepHolds, 1);
        QTRY_VERIFY_WITH_TIMEOUT(h->nickel->sleepHolds == 0, 20000);
        QVERIFY(h->allSent(kBook1));
        QVERIFY(h->allSent(kBook2));
        // One Wi-Fi connection for both.
        QCOMPARE(h->nickel->wifiEnableCalls, 1);
        QCOMPARE(h->nickel->wifiDisableCalls, 1);
        QVERIFY(!h->nickel->wifiEnabled());
    }
};

QTEST_GUILESS_MAIN(TstSyncEngine)
#include "tst_sync_engine.moc"
