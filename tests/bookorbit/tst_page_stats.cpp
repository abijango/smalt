// Acceptance: "Page-stats retries never double-count (watermark respected); a
// sweep follows every upload." (include/smalt/page_stats.h)
#include "smalt/page_stats.h"
#include "bookorbit/qt_bookorbit_client.h"
#include "mock_server.h"

#include <QtTest>

using namespace smalt;
using testing::MockServer;

namespace {

constexpr qint64 kBase = 1700000000;

QVector<PageEvent> events(int count, qint64 firstStart = kBase, int stepSeconds = 60)
{
    QVector<PageEvent> v;
    for (int i = 0; i < count; ++i)
        v.append(PageEvent{ i % 300, firstStart + qint64(i) * stepSeconds, 20, 300 });
    return v;
}

} // namespace

class TstPageStats : public QObject
{
    Q_OBJECT

    MockServer m_server;
    std::unique_ptr<QtBookOrbitClient> m_client;
    QString m_hash[5]; // m_hash[1..4]: digests of fixture files 101..104

    struct Run
    {
        bool done = false;
        int doneCalls = 0;
        SessionUploadResult result;
        QMap<QString, qint64> notified; // what onWatermark reported (last value per book)
        int notifications = 0;
    };

    // Runs an upload and waits; `store` plays the ledger and keeps watermarks between runs.
    QSharedPointer<Run> upload(QVector<SessionUploadBook> books,
                               QMap<QString, qint64> *store = nullptr,
                               SessionUploadOptions options = SessionUploadOptions())
    {
        QSharedPointer<Run> run(new Run);
        if (store) {
            for (int i = 0; i < books.size(); ++i)
                books[i].watermark = store->value(books[i].hash, 0);
        }
        uploadSessions(
                m_client.get(), books, options,
                [run, store](const QString &hash, qint64 watermark) {
                    run->notified.insert(hash, watermark);
                    ++run->notifications;
                    if (store)
                        store->insert(hash, watermark);
                },
                [run](const SessionUploadResult &r) {
                    run->result = r;
                    run->done = true;
                    ++run->doneCalls;
                });
        (void)QTest::qWaitFor([&] { return run->done; }, 20000);
        QTest::qWait(10);
        return run;
    }

    SessionUploadBook book(int fixture, const QVector<PageEvent> &evs)
    {
        SessionUploadBook b;
        b.hash = m_hash[fixture];
        b.bookFileId = 100 + fixture;
        b.events = evs;
        return b;
    }

    int storedEvents(int fixture)
    {
        return m_server.state().value("pageEvents").toObject().value(m_hash[fixture]).toInt();
    }

    QStringList paths()
    {
        QStringList out;
        const QJsonArray all = m_server.requests();
        for (int i = 0; i < all.size(); ++i)
            out << all.at(i).toObject().value("path").toString();
        return out;
    }

    int eventsInRequest(int index)
    {
        int n = 0;
        const QJsonArray books = m_server.requestsTo("/plugin/page-stats")
                                         .at(index)
                                         .toObject()
                                         .value("body")
                                         .toObject()
                                         .value("books")
                                         .toArray();
        for (int i = 0; i < books.size(); ++i)
            n += books.at(i).toObject().value("events").toArray().size();
        return n;
    }

private slots:

    void initTestCase()
    {
        const QString error = m_server.start();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QJsonObject files = m_server.state().value("files").toObject();
        for (int i = 1; i <= 4; ++i)
            m_hash[i] = files.value(QString::number(100 + i)).toObject().value("hash").toString();
    }

    void init()
    {
        m_server.reset();
        m_server.requests(true);
        m_client.reset(new QtBookOrbitClient(m_server.clientConfig()));
        m_client->setTimeouts(1500, 1500);
    }

    void cleanup() { m_client.reset(); }

    void uploadsThenSweeps()
    {
        SessionUploadOptions options;
        options.annotationsUpserted = 4;
        const auto run = upload({ book(1, events(3)) }, nullptr, options);
        QCOMPARE(run->doneCalls, 1);
        QVERIFY2(run->result.result.ok(), qPrintable(run->result.result.message));
        QVERIFY(run->result.complete);
        QCOMPARE(run->result.requests, 1);
        QCOMPARE(run->result.eventsSent, 3);
        QCOMPARE(run->result.eventsAccepted, 3);
        QCOMPARE(run->result.eventsDuplicate, 0);
        QCOMPARE(run->result.booksAcknowledged, 1);
        QCOMPARE(run->result.watermarks.value(m_hash[1]), kBase + 120);
        QCOMPARE(run->notified.value(m_hash[1]), kBase + 120);
        QVERIFY(run->result.sweepSent);
        QVERIFY(!run->result.sweepOwed);
        QVERIFY(!run->result.sweepReply.value("libraryVersion").toString().isEmpty());

        QCOMPARE(paths(), (QStringList{ "/plugin/page-stats", "/plugin/sweeps" }));
        const QJsonObject sweep =
                m_server.requestsTo("/plugin/sweeps").at(0).toObject().value("body").toObject();
        QCOMPARE(sweep.value("booksMatched").toInt(), 1);
        QCOMPARE(sweep.value("pageStatsUploaded").toInt(), 3);
        QCOMPARE(sweep.value("annotationsUpserted").toInt(), 4);
        QCOMPARE(storedEvents(1), 3);
    }

    void nothingNewMeansNoRequestsAndNoSweep()
    {
        SessionUploadBook b = book(1, events(3));
        b.watermark = kBase + 120; // everything is already acknowledged
        const auto run = upload({ b });
        QCOMPARE(run->doneCalls, 1);
        QVERIFY(run->result.complete);
        QCOMPARE(run->result.requests, 0);
        QCOMPARE(run->result.eventsSkippedAtOrBelowWatermark, 3); // reported, not silent
        QVERIFY(!run->result.sweepSent);
        QVERIFY(!run->result.sweepOwed);
        QCOMPARE(m_server.requests().size(), 0);

        const auto none = upload({});
        QVERIFY(none->result.complete);
        QCOMPARE(m_server.requests().size(), 0);
    }

    void doneIsNeverCalledFromInsideTheCall()
    {
        bool inside = true;
        bool sawInside = false;
        bool done = false;
        uploadSessions(m_client.get(), {}, SessionUploadOptions(), nullptr,
                       [&](const SessionUploadResult &) {
                           sawInside = inside;
                           done = true;
                       });
        inside = false;
        QVERIFY(!done);
        (void)QTest::qWaitFor([&] { return done; }, 2000);
        QVERIFY(done);
        QVERIFY(!sawInside);
    }

    void aSecondRunWithTheStoredWatermarkSendsNothing()
    {
        QMap<QString, qint64> ledger;
        const auto first = upload({ book(1, events(5)) }, &ledger);
        QVERIFY(first->result.complete);
        QCOMPARE(ledger.value(m_hash[1]), kBase + 4 * 60);
        m_server.requests(true);

        const auto second = upload({ book(1, events(5)) }, &ledger); // same events again
        QVERIFY(second->result.complete);
        QCOMPARE(second->result.requests, 0);
        QCOMPARE(m_server.requests().size(), 0);

        // New sessions after the watermark go out, and only those.
        const auto third = upload({ book(1, events(8)) }, &ledger); // 5 old + 3 new
        QCOMPARE(third->result.eventsSent, 3);
        QCOMPARE(eventsInRequest(0), 3);
        QCOMPARE(storedEvents(1), 8);
        QCOMPARE(third->result.eventsDuplicate, 0);
    }

    void aLostAcknowledgementIsRetriedWithoutDoubleCounting()
    {
        // The server stores the events but the reply never arrives (500 after processing).
        QJsonObject rule = MockServer::rule("/plugin/page-stats", "POST");
        rule.insert("status", 500);
        rule.insert("after_processing", true);
        rule.insert("times", 1);
        m_server.setRules({ rule });

        QMap<QString, qint64> ledger;
        const auto first = upload({ book(1, events(4)) }, &ledger);
        QCOMPARE(first->result.result.error, ErrorKind::ServerError);
        QVERIFY(!first->result.complete);
        QVERIFY(ledger.isEmpty()); // no acknowledgement, so the watermark did not move
        QVERIFY(first->result.watermarks.isEmpty());
        QVERIFY(!first->result.sweepSent);
        QCOMPARE(m_server.requestsTo("/plugin/sweeps").size(), 0); // a failed upload sends no sweep
        QCOMPARE(storedEvents(1), 4); // the server did keep them

        const auto retry = upload({ book(1, events(4)) }, &ledger);
        QVERIFY2(retry->result.result.ok(), qPrintable(retry->result.result.message));
        QVERIFY(retry->result.complete);
        QCOMPARE(retry->result.eventsSent, 4);
        QCOMPARE(retry->result.eventsAccepted, 0); // all duplicates on the server
        QCOMPARE(retry->result.eventsDuplicate, 4);
        QCOMPARE(storedEvents(1), 4); // still 4, never 8
        QCOMPARE(ledger.value(m_hash[1]), kBase + 3 * 60);
        QVERIFY(retry->result.sweepSent);

        const auto again = upload({ book(1, events(4)) }, &ledger);
        QCOMPARE(again->result.requests, 0);
        QCOMPARE(storedEvents(1), 4);
    }

    void aTimeoutLeavesTheWatermarkAlone()
    {
        QJsonObject rule = MockServer::rule("/plugin/page-stats", "POST");
        rule.insert("delay_ms", 4000);
        rule.insert("times", 1);
        m_server.setRules({ rule });
        m_client->setTimeouts(1500, 300);
        QMap<QString, qint64> ledger;
        const auto run = upload({ book(1, events(2)) }, &ledger);
        QCOMPARE(run->result.result.error, ErrorKind::Timeout);
        QVERIFY(ledger.isEmpty());
        QVERIFY(!run->result.sweepSent);
        QVERIFY(!run->result.sweepOwed); // nothing was acknowledged, so nothing is owed
    }

    void largeUploadsGoInBatchesOf500AndTheWatermarkAdvancesPerBatch()
    {
        QMap<QString, qint64> ledger;
        const auto run = upload({ book(1, events(1200)) }, &ledger);
        QVERIFY2(run->result.result.ok(), qPrintable(run->result.result.message));
        QCOMPARE(run->result.requests, 3);
        QCOMPARE(eventsInRequest(0), 500);
        QCOMPARE(eventsInRequest(1), 500);
        QCOMPARE(eventsInRequest(2), 200);
        QCOMPARE(run->notifications, 3); // told after each acknowledged batch
        QCOMPARE(storedEvents(1), 1200);
        QCOMPARE(ledger.value(m_hash[1]), kBase + 1199 * 60);
        // One sweep after the whole run, not one per batch.
        QCOMPARE(m_server.requestsTo("/plugin/sweeps").size(), 1);
        QCOMPARE(paths().last(), QStringLiteral("/plugin/sweeps"));
        QCOMPARE(m_server.requestsTo("/plugin/sweeps")
                         .at(0)
                         .toObject()
                         .value("body")
                         .toObject()
                         .value("pageStatsUploaded")
                         .toInt(),
                 1200);
    }

    void aBatchNeverSplitsEventsThatShareAStartTime()
    {
        // Pairs of events share a start time (different pages). With room for 499,
        // the cut would fall inside a pair, so the batch stops one event earlier.
        QVector<PageEvent> evs;
        for (int i = 0; i < 1000; ++i)
            evs.append(PageEvent{ i % 300, kBase + (i / 2), 10, 300 });
        SessionUploadOptions options;
        options.maxEventsPerRequest = 499;
        QMap<QString, qint64> ledger;
        const auto run = upload({ book(1, evs) }, &ledger, options);
        QVERIFY2(run->result.result.ok(), qPrintable(run->result.result.message));
        QCOMPARE(eventsInRequest(0), 498);
        QCOMPARE(storedEvents(1), 1000);
        QCOMPARE(run->result.eventsDuplicate, 0); // nothing resent, so no duplicates to ignore
        QCOMPARE(ledger.value(m_hash[1]), kBase + 499);
    }

    void failurePartwayKeepsEarlierBatchesAndResumesAfterThem()
    {
        QJsonObject rule = MockServer::rule("/plugin/page-stats", "POST");
        rule.insert("status", 500);
        rule.insert("skip", 1); // the second request fails
        rule.insert("times", 1);
        m_server.setRules({ rule });

        QMap<QString, qint64> ledger;
        const auto first = upload({ book(1, events(1200)) }, &ledger);
        QCOMPARE(first->result.result.error, ErrorKind::ServerError);
        QVERIFY(!first->result.complete);
        QCOMPARE(first->result.eventsSent, 500); // only the acknowledged batch counts
        QCOMPARE(ledger.value(m_hash[1]), kBase + 499 * 60);
        QVERIFY(first->result.sweepOwed); // something was acknowledged, no sweep yet
        QVERIFY(!first->result.sweepSent);
        QCOMPARE(m_server.requestsTo("/plugin/sweeps").size(), 0);
        QCOMPARE(storedEvents(1), 500);

        const auto second = upload({ book(1, events(1200)) }, &ledger);
        QVERIFY2(second->result.result.ok(), qPrintable(second->result.result.message));
        QVERIFY(second->result.complete);
        QCOMPARE(second->result.eventsSent, 700); // the rest, not the first 500 again
        QCOMPARE(second->result.eventsAccepted, 700);
        QCOMPARE(second->result.eventsDuplicate, 0);
        QCOMPARE(storedEvents(1), 1200);
        QVERIFY(second->result.sweepSent);
    }

    void manyBooksShareRequestsWithinTheServersLimits()
    {
        SessionUploadOptions options;
        options.maxBooksPerRequest = 2;
        QVector<SessionUploadBook> books;
        for (int i = 1; i <= 4; ++i)
            books.append(book(i, events(3, kBase + i * 1000)));
        QMap<QString, qint64> ledger;
        const auto run = upload(books, &ledger, options);
        QVERIFY2(run->result.result.ok(), qPrintable(run->result.result.message));
        QCOMPARE(run->result.requests, 2);
        QCOMPARE(run->result.booksAcknowledged, 4);
        for (int i = 1; i <= 4; ++i)
            QCOMPARE(storedEvents(i), 3);
        QCOMPARE(m_server.requestsTo("/plugin/sweeps")
                         .at(0)
                         .toObject()
                         .value("body")
                         .toObject()
                         .value("booksMatched")
                         .toInt(),
                 4);

        // A small event budget spreads books over requests, still without loss.
        m_server.reset();
        m_server.requests(true);
        SessionUploadOptions tight;
        tight.maxEventsPerRequest = 5;
        QMap<QString, qint64> ledger2;
        const auto run2 = upload(books, &ledger2, tight);
        QVERIFY2(run2->result.result.ok(), qPrintable(run2->result.result.message));
        for (int i = 1; i <= 4; ++i)
            QCOMPARE(storedEvents(i), 3);
        QCOMPARE(run2->result.eventsSent, 12);
        QCOMPARE(run2->result.eventsDuplicate, 0);
        QVERIFY(run2->result.requests >= 3);
        for (int i = 0; i < run2->result.requests; ++i)
            QVERIFY(eventsInRequest(i) <= 5);
    }

    void anUnknownBookIsReportedAndSkipped()
    {
        SessionUploadBook ghost;
        ghost.hash = QString(32, QLatin1Char('e'));
        ghost.events = events(3);
        QMap<QString, qint64> ledger;
        const auto run = upload({ ghost, book(2, events(2)) }, &ledger);
        QVERIFY2(run->result.result.ok(), qPrintable(run->result.result.message));
        QVERIFY(run->result.complete);
        QCOMPARE(run->result.unmatched, QStringList{ ghost.hash });
        QVERIFY(!ledger.contains(ghost.hash)); // its watermark does not move
        QCOMPARE(ledger.value(m_hash[2]), kBase + 60);
        QCOMPARE(run->result.booksAcknowledged, 1);
        QCOMPARE(run->result.eventsSent, 2);
        QCOMPARE(m_server.requestsTo("/plugin/sweeps")
                         .at(0)
                         .toObject()
                         .value("body")
                         .toObject()
                         .value("booksMatched")
                         .toInt(),
                 1);
    }

    void onlyUnknownBooksMeansNoSweep()
    {
        SessionUploadBook ghost;
        ghost.hash = QString(32, QLatin1Char('e'));
        ghost.events = events(3);
        const auto run = upload({ ghost });
        QCOMPARE(run->result.unmatched.size(), 1);
        QVERIFY(run->result.complete);
        QVERIFY(!run->result.sweepSent);
        QVERIFY(!run->result.sweepOwed);
        QCOMPARE(m_server.requestsTo("/plugin/sweeps").size(), 0);
    }

    void eventsTheServerWouldRefuseAreSkippedNotBlocking()
    {
        QVector<PageEvent> evs = events(3);
        evs.append(PageEvent{ 1, 0, 10, 300 }); // startTime 0
        evs.append(PageEvent{ 1, kBase + 5000, 10, 0 }); // no pages
        evs.append(PageEvent{ -1, kBase + 6000, 10, 300 }); // negative page
        evs.append(PageEvent{ 1, kBase + 7000, 999999, 300 }); // long, but valid: clamped
        QMap<QString, qint64> ledger;
        const auto run = upload({ book(1, evs) }, &ledger);
        QVERIFY2(run->result.result.ok(), qPrintable(run->result.result.message));
        QCOMPARE(run->result.eventsSkippedInvalid, 3);
        QCOMPARE(run->result.eventsSent, 4);
        QCOMPARE(storedEvents(1), 4);
        QCOMPARE(ledger.value(m_hash[1]), kBase + 7000);
    }

    void aFailedSweepIsReportedAsOwedAndWatermarksStay()
    {
        QJsonObject rule = MockServer::rule("/plugin/sweeps", "POST");
        rule.insert("status", 500);
        m_server.setRules({ rule });
        QMap<QString, qint64> ledger;
        const auto run = upload({ book(1, events(3)) }, &ledger);
        QVERIFY(run->result.result.ok()); // the upload itself succeeded
        QVERIFY(run->result.complete);
        QVERIFY(!run->result.sweepSent);
        QVERIFY(run->result.sweepOwed);
        QCOMPARE(run->result.sweepResult.error, ErrorKind::ServerError);
        QCOMPARE(ledger.value(m_hash[1]), kBase + 120);

        // The caller can send the sweep later on its own.
        m_server.clearRules();
        bool done = false;
        bool ok = false;
        QJsonObject counts;
        counts.insert("booksMatched", 1);
        counts.insert("pageStatsUploaded", 3);
        m_client->sendSweep(counts, [&](const Reply<QJsonObject> &r) {
            done = true;
            ok = r.result.ok();
        });
        (void)QTest::qWaitFor([&] { return done; }, 3000);
        QVERIFY(ok);
    }

    void aSweepFollowsEveryUpload()
    {
        QMap<QString, qint64> ledger;
        qint64 start = kBase;
        for (int round = 0; round < 3; ++round) {
            const auto run = upload({ book(1, events(2, start)) }, &ledger);
            QVERIFY(run->result.sweepSent);
            start += 10000;
        }
        QCOMPARE(paths(),
                 (QStringList{ "/plugin/page-stats", "/plugin/sweeps", "/plugin/page-stats",
                               "/plugin/sweeps", "/plugin/page-stats", "/plugin/sweeps" }));
        QCOMPARE(m_server.state().value("sweeps").toArray().size(), 3);
        QCOMPARE(storedEvents(1), 6);
    }

    void sweepCanBeTurnedOff()
    {
        SessionUploadOptions options;
        options.sendSweep = false;
        const auto run = upload({ book(1, events(2)) }, nullptr, options);
        QVERIFY(run->result.complete);
        QVERIFY(!run->result.sweepSent);
        QVERIFY(!run->result.sweepOwed);
        QCOMPARE(m_server.requestsTo("/plugin/sweeps").size(), 0);
    }

    void eventsBehindAWatermarkFromABackwardsClockAreSkippedAndReported()
    {
        // Watermark is ahead of new sessions (clock went back): they are skipped, counted,
        // the call still succeeds, and events after the watermark still go.
        SessionUploadBook b = book(1, events(5)); // kBase .. kBase+240
        b.watermark = kBase + 100;
        const auto run = upload({ b });
        QVERIFY(run->result.result.ok());
        QCOMPARE(run->result.eventsSkippedAtOrBelowWatermark, 2); // kBase, kBase+60
        QCOMPARE(run->result.eventsSent, 3);
    }

    void unorderedEventsAreSortedAndOnlyTheNewOnesGo()
    {
        QVector<PageEvent> evs = events(6);
        std::reverse(evs.begin(), evs.end());
        SessionUploadBook b = book(1, evs);
        b.watermark = kBase + 2 * 60; // events 0..2 were acknowledged earlier
        const auto run = upload({ b });
        QCOMPARE(run->result.eventsSent, 3);
        const QJsonArray sent = m_server.requestsTo("/plugin/page-stats")
                                        .at(0)
                                        .toObject()
                                        .value("body")
                                        .toObject()
                                        .value("books")
                                        .toArray()
                                        .at(0)
                                        .toObject()
                                        .value("events")
                                        .toArray();
        QCOMPARE(sent.at(0).toObject().value("startTime").toDouble(), double(kBase + 3 * 60));
        QCOMPARE(sent.at(2).toObject().value("startTime").toDouble(), double(kBase + 5 * 60));
    }

    void aLivePushEveryFiveMinutesSendsOnlyTheNewEventsAndOneSweepEach()
    {
        // The "While reading" trigger: a handful of events per push, no events between.
        QMap<QString, qint64> store;
        QVector<PageEvent> all = events(3);
        auto push = [&]() { return upload({ book(1, all) }, &store); };

        auto r1 = push();
        QCOMPARE(r1->result.eventsSent, 3);
        QVERIFY(r1->result.sweepSent);
        QCOMPARE(m_server.requests().size(), 2); // one page-stats, one sweep

        // Nothing read since: no request at all, no sweep.
        m_server.requests(true);
        auto r2 = push();
        QVERIFY(r2->result.complete);
        QCOMPARE(r2->result.requests, 0);
        QVERIFY(!r2->result.sweepSent);
        QCOMPARE(m_server.requests().size(), 0);

        // Two more pages read: only those go, with one sweep.
        all += events(2, kBase + 600);
        auto r3 = push();
        QCOMPARE(r3->result.eventsSent, 2);
        QCOMPARE(r3->result.eventsAccepted, 2);
        QCOMPARE(r3->result.eventsDuplicate, 0);
        QCOMPARE(paths(), (QStringList{ "/plugin/page-stats", "/plugin/sweeps" }));
        QCOMPARE(eventsInRequest(0), 2);
        QCOMPARE(storedEvents(1), 5);
    }
};

QTEST_GUILESS_MAIN(TstPageStats)
#include "tst_page_stats.moc"
