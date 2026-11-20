// Shared set-up for the sync engine tests (task 21): the real BookOrbit
// client against tools/mock-bookorbit, a real ledger and outbox in a
// temporary user partition, FakeNickel, FakeRecorder, the real settings and
// safety switches, and a fake clock for the engine's timers and the sleep
// watchdog.
#pragma once

#include "bookorbit/mock_server.h"
#include "fakes/fake_nickel.h"
#include "safety/fake_clock.h"
#include "sync/fake_recorder.h"

#include "safety/safety_internal.h"
#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/outbox.h"
#include "smalt/safety.h"
#include "smalt/settings.h"
#include "smalt/sync.h"
#include "sync/sync_internal.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace synctest {

using namespace smalt;

// Fixture books: the mock's files 101 (book 1) and 102 (book 2).
const QString kBook1 = QStringLiteral("file:///mnt/onboard/BookOrbit/Ada Quill/The Salt Road.epub");
const QString kBook2 = QStringLiteral("file:///mnt/onboard/BookOrbit/Ada Quill/Salt Harvest.epub");

class Harness
{
public:
    explicit Harness(::testing::MockServer *mock) : m_mock(mock) { }

    ~Harness() { tearDown(); }

    // Builds everything; returns an empty string or what went wrong.
    QString setUp()
    {
        dir.reset(new QTemporaryDir);
        if (!dir->isValid())
            return QStringLiteral("no temporary dir");
        paths.reset(new Paths(Paths::forRoot(dir->path())));
        QDir().mkpath(paths->addsDir());
        ledger = createLedger(*paths);
        const Status s = ledger->open();
        if (!s.ok())
            return s.message;
        outbox = createOutbox(ledger.get());
        ClientConfig config = m_mock->clientConfig();
        config.deviceId = ledger->deviceId();
        client = createBookOrbitClient(config);
        nickel.reset(new smalt::testing::FakeNickel);
        nickel->setReady(true);
        nickel->setBatteryPercent(80);
        recorder.reset(new smalt::testing::FakeRecorder);
        settings = createSettingsStore(*paths);
        settings->load();
        safety = createSafety(*paths);
        clock = std::make_shared<smalt::testing::FakeClock>(QDateTime::currentDateTimeUtc());

        const QJsonObject files = m_mock->state().value(QStringLiteral("files")).toObject();
        hash1 = files.value(QStringLiteral("101"))
                        .toObject()
                        .value(QStringLiteral("hash"))
                        .toString();
        hash2 = files.value(QStringLiteral("102"))
                        .toObject()
                        .value(QStringLiteral("hash"))
                        .toString();
        if (hash1.isEmpty() || hash2.isEmpty())
            return QStringLiteral("the mock gave no fixture hashes");
        link(kBook1, 1, 101, hash1);
        link(kBook2, 2, 102, hash2);
        ContentRow row;
        row.contentId = kBook1;
        nickel->putContent(row);
        row.contentId = kBook2;
        nickel->putContent(row);
        makeEngine();
        return QString();
    }

    void makeEngine()
    {
        engine.reset();
        sync::EngineDeps d;
        d.ledger = ledger.get();
        d.outbox = outbox.get();
        d.client = client.get();
        d.nickel = nickel.get();
        d.safety = safety.get();
        d.recorder = recorder.get();
        d.settings = settings.get();
        d.clock = clock;
        std::shared_ptr<safety::Clock> c = clock;
        d.makeWatchdog = [c]() { return safety::createSleepWatchdogWith(c, kPreSleepHoldMs); };
        d.startupQuietMs = 0;
        engine.reset(sync::createSyncEngineWith(d));
        results.clear();
        QObject::connect(engine.get(), &SyncEngine::finished,
                         [this](const SyncResult &r) { results.append(r); });
    }

    void tearDown()
    {
        engine.reset();
        client.reset();
        outbox.reset();
        ledger.reset();
        dir.reset();
    }

    void link(const QString &contentId, qint64 bookId, qint64 fileId, const QString &hash)
    {
        BookMapEntry e;
        e.contentId = contentId;
        e.bookId = bookId;
        e.bookFileId = fileId;
        e.partialMd5 = hash;
        e.createdUtc = QDateTime::currentDateTimeUtc();
        QVERIFY(ledger->putBookMap(e).ok());
    }

    // A finished session of `pages` pages, `seconds` each, starting at
    // `start`, and the book's local position at its end.
    QString read(const QString &contentId, const QDateTime &start, int pages, int seconds = 60,
                 int firstPage = 10, double percentAtEnd = 42.0)
    {
        SessionRecord s;
        s.contentId = contentId;
        s.startUtc = start;
        s.startPage = firstPage;
        if (!ledger->beginSession(&s).ok())
            return QString();
        for (int i = 0; i < pages; ++i) {
            PageEventRecord e;
            e.sessionUuid = s.uuid;
            e.contentId = contentId;
            e.page = firstPage + i;
            e.totalPages = 300;
            e.startUtc = start.addSecs(i * seconds);
            e.durationSeconds = seconds;
            if (!ledger->appendPageEvent(e).ok())
                return QString();
        }
        s.endUtc = start.addSecs(pages * seconds);
        s.activeSeconds = pages * seconds;
        s.endPage = firstPage + pages - 1;
        s.pagesTurned = pages;
        s.totalPages = 300;
        s.end.locator = QStringLiteral("OEBPS/ch3.xhtml#kobo.1.1");
        s.end.percent = percentAtEnd;
        if (!ledger->finishSession(s).ok())
            return QString();
        Position p;
        p.locator = s.end.locator;
        p.percent = percentAtEnd;
        ledger->setLocalProgress(contentId, p, s.endUtc);
        return s.uuid;
    }

    // Starts an open session and writes `pages` finished pages; the page on
    // screen starts right after them (recorder->pageStart).
    QString readOpen(const QString &contentId, const QDateTime &start, int pages, int seconds = 60)
    {
        SessionRecord s;
        s.contentId = contentId;
        s.startUtc = start;
        s.startPage = 1;
        if (!ledger->beginSession(&s).ok())
            return QString();
        for (int i = 0; i < pages; ++i) {
            PageEventRecord e;
            e.sessionUuid = s.uuid;
            e.contentId = contentId;
            e.page = 1 + i;
            e.totalPages = 300;
            e.startUtc = start.addSecs(i * seconds);
            e.durationSeconds = seconds;
            if (!ledger->appendPageEvent(e).ok())
                return QString();
        }
        recorder->open = contentId;
        recorder->pageStart = start.addSecs(pages * seconds).toMSecsSinceEpoch() / 1000;
        nickel->simulateBookOpened(contentId);
        return s.uuid;
    }

    // Requests a sync and waits until the engine reports it.
    SyncResult run(SyncTrigger trigger, const QString &contentId = QString(), int timeoutMs = 20000)
    {
        const int before = results.size();
        SyncRequest r;
        r.trigger = trigger;
        r.contentId = contentId;
        engine->request(r);
        return waitResult(before, timeoutMs);
    }

    SyncResult waitResult(int before, int timeoutMs = 20000)
    {
        QElapsedTimer t;
        t.start();
        while (results.size() <= before && t.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (results.size() <= before) {
            SyncResult none;
            none.skipReason = QStringLiteral("TEST: no result in time");
            none.outcome = SyncOutcome::Failed;
            return none;
        }
        return results.last();
    }

    int serverEvents(const QString &hash)
    {
        return m_mock->state().value(QStringLiteral("pageEvents")).toObject().value(hash).toInt();
    }

    int requestsTo(const QString &path, const QString &method = QString())
    {
        int n = 0;
        const QJsonArray all = m_mock->requests();
        for (int i = 0; i < all.size(); ++i) {
            const QJsonObject o = all.at(i).toObject();
            if (o.value(QStringLiteral("path")).toString() == path
                && (method.isEmpty() || o.value(QStringLiteral("method")).toString() == method))
                ++n;
        }
        return n;
    }

    // Paths in the order the mock saw them.
    QStringList requestOrder()
    {
        QStringList out;
        const QJsonArray all = m_mock->requests();
        for (int i = 0; i < all.size(); ++i) {
            const QJsonObject o = all.at(i).toObject();
            out << o.value(QStringLiteral("method")).toString() + QLatin1Char(' ')
                            + o.value(QStringLiteral("path")).toString();
        }
        return out;
    }

    // Every page event of the book is at or below its watermark.
    bool allSent(const QString &contentId)
    {
        BookMapEntry m;
        if (!ledger->bookMapByContentId(contentId, &m).ok())
            return false;
        return ledger->pageEventsAfter(contentId, m.statsWatermark).isEmpty();
    }

    void touch(const QString &path)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
    }

    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<Paths> paths;
    std::unique_ptr<Ledger> ledger;
    std::unique_ptr<Outbox> outbox;
    std::unique_ptr<BookOrbitClient> client;
    std::unique_ptr<smalt::testing::FakeNickel> nickel;
    std::unique_ptr<smalt::testing::FakeRecorder> recorder;
    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<Safety> safety;
    std::shared_ptr<smalt::testing::FakeClock> clock;
    std::unique_ptr<SyncEngine> engine;
    QVector<SyncResult> results;
    QString hash1;
    QString hash2;

private:
    ::testing::MockServer *m_mock;
};

} // namespace synctest
