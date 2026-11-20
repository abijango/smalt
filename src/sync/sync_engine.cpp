// The sync engine (include/smalt/sync.h). Task 21.
//
// SPEC: Sync engine ("Triggers", "Order of a full sync", "Conflict rules",
// "Wi-Fi control", "Networking"), Crash safety ("Separate switches", "Sleep
// watchdog", "No network at startup"), On-device data model ("How BookOrbit
// builds sessions", "Correcting a session"), Status and ratings, UX details
// ("Where sync status shows", "Copy").
//
// Everything runs on the thread that owns the engine (Nickel's UI thread on
// the Kobo): network calls go through the asynchronous client, timers through
// safety::Clock, and the ledger calls are short. One sync runs at a time;
// triggers that arrive meanwhile wait in a small queue (duplicates folded).
//
// A sync that is cut short (deadline, watchdog, Wi-Fi loss, abort(), USB)
// loses and duplicates nothing:
// - page events go by per-book watermark; a watermark moves only when the
//   server acknowledged the batch (page_stats.h), so a retry resends exactly
//   what was not acknowledged, and the server ignores events it already has;
// - outbox items are removed only after the server confirmed them; an
//   unconfirmed item is sent again later, and kosync positions and statuses
//   are idempotent;
// - "sweep owed" is kept in the ledger and sent on the next sync.
//
// Wi-Fi: the engine notes whether it was on; it turns it on only when there
// is work (or for "Sync now" and "Connect briefly"), and turns it off again
// only if it turned it on, on every path (success, failure, deadline,
// watchdog, abort). A close followed by sleep shares one connection.
#include "sync_internal.h"
#include "sync_text.h"

#include "safety/safety_internal.h"
#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/nickel.h"
#include "smalt/outbox.h"
#include "smalt/page_stats.h"
#include "smalt/recorder.h"
#include "smalt/settings.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QSharedPointer>

namespace smalt {
namespace sync {
namespace {

const char kLog[] = "sync";

QDateTime fromUnix(qint64 seconds)
{
    return QDateTime::fromMSecsSinceEpoch(seconds * 1000).toUTC();
}

qint64 toUnix(const QDateTime &t)
{
    return t.toMSecsSinceEpoch() / 1000;
}

const char *triggerName(SyncTrigger t)
{
    switch (t) {
    case SyncTrigger::WhileReading:
        return "while reading";
    case SyncTrigger::BookClosed:
        return "book closed";
    case SyncTrigger::BeforeSleep:
        return "before sleep";
    case SyncTrigger::Wake:
        return "wake";
    case SyncTrigger::WifiAlreadyOn:
        return "Wi-Fi on";
    case SyncTrigger::Manual:
        return "sync now";
    }
    return "?";
}

// An error that says the connection or the account is the problem, not one
// item: the sync stops there.
bool stopsTheSync(ErrorKind e)
{
    switch (e) {
    case ErrorKind::Offline:
    case ErrorKind::Timeout:
    case ErrorKind::AuthRejected:
    case ErrorKind::ServerError:
    case ErrorKind::TlsError:
    case ErrorKind::Cancelled:
    case ErrorKind::BadResponse:
        return true;
    case ErrorKind::None:
    case ErrorKind::NotFound:
    case ErrorKind::ClientError:
    case ErrorKind::NotSupported:
    case ErrorKind::LocalIo:
        break;
    }
    return false;
}

QString cacheKey(const char *what, const QString &contentId = QString())
{
    QString k = QStringLiteral("sync/") + QLatin1String(what);
    if (!contentId.isEmpty())
        k += QLatin1Char('/') + contentId;
    return k;
}

enum class Step { PushPages, PushOutbox, Pull, PullPositionsOnly, Apply };

class Engine : public SyncEngine
{
    Q_OBJECT

public:
    Engine(const EngineDeps &deps, QObject *parent);
    ~Engine() override;

    void request(const SyncRequest &request) override;
    void abort() override;
    SyncStatus status() const override;

private slots:
    void runQueued();
    void onReadyForSleep();
    void onBookClosed(const QString &contentId);
    void onSessionFinished(const QString &contentId, const QString &uuid);
    void onPageEventWritten(const QString &contentId);
    void onLedgerClosed();

private:
    struct Run
    {
        bool active = false;
        quint64 gen = 0;
        SyncRequest req;
        SyncResult result;
        QVector<Step> steps;
        int deadlineTimer = 0;
        int wifiTimer = 0;
        bool waitingForWifi = false;
        QStringList pullBooks;
        int pullIndex = 0;
        QSet<QString> applyBooks;
        int outboxBatches = 0;
        bool positionsOnly = false; // The pre-sleep quick pull.
        bool anyWork = false;
        ErrorKind softError = ErrorKind::None; // A failure that didn't stop the sync.
        QSet<RequestId> requests; // Ours, to cancel when the sync stops.
    };

    // --- Triggers and limits ---
    void onNetworkChanged(bool connected);
    QString skipReason(const SyncRequest &r);
    bool nothingToPush();
    bool pullFresh(int seconds);
    bool mayBringWifiUp(const SyncRequest &r) const;
    void skip(const SyncRequest &r, const QString &why);
    void armLivePush();
    void onLiveTick();

    // --- Sleep hold ---
    void holdSleep();
    void onSleepReleased();
    void releaseSleepNow();

    // --- One sync ---
    void start(const SyncRequest &r);
    void ensureNetwork();
    void networkReady(bool ok);
    void nextStep();
    void finish(ErrorKind error);

    bool current(quint64 gen) const { return m_run.active && m_run.gen == gen; }

    void track(RequestId id) { m_run.requests.insert(id); }

    void untrack(RequestId id) { m_run.requests.remove(id); }

    void restoreWifi();

    // --- Steps ---
    void pushPages();
    void sendOwedSweep();
    void pushOutbox();
    void sendPosition(const QVector<OutboxItem> &batch, int i);
    void sendAnnotation(const QVector<OutboxItem> &batch, int i);
    void sendStatus(const QVector<OutboxItem> &batch, int i);
    void sendStatusRating(const QVector<OutboxItem> &batch, int i, const BookStateRecord &st,
                          qint64 bookId, bool statusSent, bool sendRating);
    void statusItemDone(const QVector<OutboxItem> &batch, int i, const BookStateRecord &st,
                        bool sentStatus, bool sentRating);
    void itemFailed(const QVector<OutboxItem> &batch, int i, const HttpResult &r,
                    std::function<void()> next);
    void pullNext();
    void pullStatus(const QString &contentId, const BookMapEntry &map);
    void applyStep();

    // --- Local bookkeeping ---
    QStringList recentBooks(int max);
    QStringList pullBooksFor(const SyncRequest &r);
    void collectLocalChanges(const QStringList &books);
    void maybeEnqueuePosition(const QString &contentId);
    void maybeEnqueueStatus(const QString &contentId);
    bool positionIsApproximate(const QString &contentId);
    bool applyToNickel(const QString &contentId);
    void applyPending(const QString &onlyContentId);
    QStringList loadApplyPending();
    void saveApplyPending(const QStringList &books);
    QJsonObject cacheJson(const QString &key);
    void putCacheJson(const QString &key, const QJsonObject &o);
    QString openBook() const;

    EngineDeps m_d;
    std::shared_ptr<safety::Clock> m_clock;
    QDateTime m_createdUtc;
    quint64 m_gen = 0;
    Run m_run;
    QList<SyncRequest> m_queue;
    bool m_queuedRun = false;

    // Wi-Fi: true while Wi-Fi is on because this engine turned it on.
    bool m_wifiOurs = false;
    bool m_restoringWifi = false;

    // Pre-sleep hold.
    std::unique_ptr<SleepWatchdog> m_watchdog;
    bool m_sleepHeld = false;
    bool m_nickelHoldTaken = false;

    // Push while reading.
    int m_liveTimer = 0;

    // Failure rule for toasts and the header.
    int m_consecutiveFailures = 0;
    ErrorKind m_lastError = ErrorKind::None;
    QDateTime m_lastSuccessUtc;
    int m_lastWaiting = 0;
};

Engine::Engine(const EngineDeps &deps, QObject *parent)
    : SyncEngine(parent), m_d(deps), m_clock(deps.clock ? deps.clock : safety::createQtClock())
{
    m_createdUtc = m_clock->nowUtc();
    if (!m_d.makeWatchdog) {
        std::shared_ptr<safety::Clock> clock = m_clock;
        m_d.makeWatchdog = [clock]() {
            return safety::createSleepWatchdogWith(clock, kPreSleepHoldMs);
        };
    }
    if (m_d.recorder) {
        // String-based connections: the engine needs no link-time reference
        // to Recorder's meta-object (task 20's module provides it).
        connect(m_d.recorder, SIGNAL(readyForSleep()), this, SLOT(onReadyForSleep()));
        connect(m_d.recorder, SIGNAL(bookClosedRecorded(QString)), this,
                SLOT(onBookClosed(QString)));
        connect(m_d.recorder, SIGNAL(sessionFinished(QString, QString)), this,
                SLOT(onSessionFinished(QString, QString)));
        connect(m_d.recorder, SIGNAL(pageEventWritten(QString)), this,
                SLOT(onPageEventWritten(QString)));
        connect(m_d.recorder, SIGNAL(ledgerClosed()), this, SLOT(onLedgerClosed()));
    }
    if (m_d.nickel) {
        connect(m_d.nickel, &NickelBridge::wokeUp, this, [this]() {
            SyncRequest r;
            r.trigger = SyncTrigger::Wake;
            request(r);
        });
        connect(m_d.nickel, &NickelBridge::networkChanged, this,
                [this](bool connected) { onNetworkChanged(connected); });
    }
    if (m_d.settings) {
        connect(m_d.settings, &SettingsStore::changed, this, [this]() {
            if (m_liveTimer) {
                m_clock->cancelTimer(m_liveTimer);
                m_liveTimer = 0;
            }
            armLivePush();
        });
    }
}

Engine::~Engine()
{
    if (m_run.active)
        finish(ErrorKind::Cancelled);
    if (m_liveTimer)
        m_clock->cancelTimer(m_liveTimer);
    releaseSleepNow();
}

QString Engine::openBook() const
{
    QString open = m_d.recorder ? m_d.recorder->openContentId() : QString();
    if (open.isEmpty() && m_d.nickel)
        open = m_d.nickel->openContentId();
    return open;
}

// ---------------------------------------------------------------------------
// Triggers and limits
// ---------------------------------------------------------------------------

void Engine::request(const SyncRequest &r)
{
    if (r.trigger == SyncTrigger::BeforeSleep) {
        // Decide now, while Nickel waits: hold sleep only for a sync that will run.
        const QString why = skipReason(r);
        if (!why.isEmpty()) {
            skip(r, why);
            return;
        }
        if (m_sleepHeld) {
            log::info(kLog, QStringLiteral("before sleep: already holding sleep"));
            return;
        }
        holdSleep();
        // The pre-sleep sync pushes the whole outbox: a queued close or live
        // push folds into it ("close-then-sleep collapse").
        QList<SyncRequest> kept;
        for (const SyncRequest &q : m_queue) {
            if (q.trigger == SyncTrigger::Manual || q.trigger == SyncTrigger::Wake)
                kept << q;
            else
                log::info(kLog,
                          QStringLiteral("%1 folded into the pre-sleep sync")
                                  .arg(QLatin1String(triggerName(q.trigger))));
        }
        m_queue = kept;
        m_queue.prepend(r);
    } else {
        for (const SyncRequest &q : m_queue) {
            if (q.trigger == r.trigger && q.contentId == r.contentId)
                return; // Already waiting.
            if (q.trigger == SyncTrigger::BeforeSleep
                && (r.trigger == SyncTrigger::BookClosed
                    || r.trigger == SyncTrigger::WhileReading)) {
                log::info(kLog,
                          QStringLiteral("%1 folded into the pre-sleep sync")
                                  .arg(QLatin1String(triggerName(r.trigger))));
                return;
            }
        }
        if (m_run.active && m_run.req.trigger == r.trigger && m_run.req.contentId == r.contentId
            && r.trigger != SyncTrigger::Manual)
            return; // The same sync is running.
        m_queue.append(r);
    }
    if (!m_run.active && !m_queuedRun) {
        m_queuedRun = true;
        QMetaObject::invokeMethod(this, "runQueued", Qt::QueuedConnection);
    }
}

void Engine::runQueued()
{
    m_queuedRun = false;
    if (m_run.active || m_queue.isEmpty())
        return;
    const SyncRequest r = m_queue.takeFirst();
    const QString why = skipReason(r);
    if (!why.isEmpty()) {
        skip(r, why);
        if (r.trigger == SyncTrigger::BeforeSleep)
            releaseSleepNow();
    } else {
        start(r);
    }
    if (!m_run.active && !m_queue.isEmpty() && !m_queuedRun) {
        m_queuedRun = true;
        QMetaObject::invokeMethod(this, "runQueued", Qt::QueuedConnection);
    }
}

void Engine::skip(const SyncRequest &r, const QString &why)
{
    log::info(kLog,
              QStringLiteral("%1: skipped (%2)").arg(QLatin1String(triggerName(r.trigger)), why));
    SyncResult result;
    result.trigger = r.trigger;
    result.outcome = SyncOutcome::Skipped;
    result.skipReason = why;
    result.finishedUtc = m_clock->nowUtc();
    emit finished(result);
}

bool Engine::nothingToPush()
{
    return m_d.outbox->pendingCount() == 0 && !m_d.ledger->sweepOwed();
}

bool Engine::pullFresh(int seconds)
{
    const QDateTime last = m_d.ledger->lastPullUtc();
    return last.isValid() && last.secsTo(m_clock->nowUtc()) < seconds;
}

QString Engine::skipReason(const SyncRequest &r)
{
    if (!m_d.ledger || !m_d.outbox || !m_d.client || !m_d.nickel)
        return QStringLiteral("not wired");
    if (m_d.safety
        && (m_d.safety->isSwitchOn(SafetySwitch::DisableSync)
            || m_d.safety->isSwitchOn(SafetySwitch::Disabled)))
        return QStringLiteral("`disable-sync` is present");
    if ((m_d.recorder && m_d.recorder->ledgerClosedForUsb()) || !m_d.ledger->isOpen())
        return QStringLiteral("the ledger is closed");
    if (!m_d.nickel->isReady())
        return QStringLiteral("startup: Nickel isn't ready");
    if (r.trigger != SyncTrigger::Manual
        && m_createdUtc.msecsTo(m_clock->nowUtc()) < m_d.startupQuietMs)
        return QStringLiteral("startup: no network yet");

    const Settings s = m_d.settings ? m_d.settings->current() : Settings();
    const bool connected = m_d.nickel->networkConnected();
    switch (r.trigger) {
    case SyncTrigger::Manual:
        return QString(); // "no limits except a 60 s timeout"
    case SyncTrigger::WhileReading: {
        if (s.livePushMode == LivePushMode::Off)
            return QStringLiteral("push while reading is off");
        if (r.contentId.isEmpty() || openBook() != r.contentId)
            return QStringLiteral("the book isn't open");
        if (s.livePushMode == LivePushMode::OnlyWhenConnected && !connected)
            return QStringLiteral("Wi-Fi isn't connected");
        BookMapEntry map;
        if (!m_d.ledger->bookMapByContentId(r.contentId, &map).ok())
            return QStringLiteral("the book isn't linked to BookOrbit");
        const qint64 before = m_d.recorder ? m_d.recorder->currentPageStartUnix() : 0;
        const QVector<PageEventRecord> events =
                m_d.ledger->pageEventsAfter(r.contentId, map.statsWatermark, 1);
        if (before <= 0 || events.isEmpty() || toUnix(events.first().startUtc) >= before)
            return QStringLiteral("no finished pages to send");
        return QString(); // The 5-minute rule doesn't apply.
    }
    case SyncTrigger::BookClosed:
        if (!s.syncOnBookClose)
            return QStringLiteral("switched off in Settings");
        break;
    case SyncTrigger::BeforeSleep:
        if (!s.syncBeforeSleep)
            return QStringLiteral("switched off in Settings");
        {
            const int battery = m_d.nickel->batteryPercent();
            if (battery >= 0 && battery < s.preSleepMinBatteryPercent)
                return QStringLiteral("battery at %1%").arg(battery);
        }
        collectLocalChanges(QStringList() << openBook());
        if (nothingToPush() && pullFresh(kPreSleepPullFreshSeconds))
            return QStringLiteral("nothing queued and the last pull is under 6 h old");
        break;
    case SyncTrigger::Wake:
        if (!s.syncOnWake)
            return QStringLiteral("switched off in Settings");
        break;
    case SyncTrigger::WifiAlreadyOn:
        if (!s.syncWhenWifiOn)
            return QStringLiteral("switched off in Settings");
        if (!connected)
            return QStringLiteral("Wi-Fi isn't connected");
        break;
    }

    if (r.trigger == SyncTrigger::BookClosed)
        collectLocalChanges(QStringList() << r.contentId);
    // "A sync that finished successfully in the last 5 minutes, with an empty
    // outbox, skips any trigger except Sync now and While reading."
    const bool empty = nothingToPush();
    if (empty && m_lastSuccessUtc.isValid()
        && m_lastSuccessUtc.secsTo(m_clock->nowUtc()) < kRecentSuccessSkipSeconds)
        return QStringLiteral("synced in the last 5 minutes and nothing is queued");
    // "Brings Wi-Fi up only if the outbox has items."
    if (r.trigger == SyncTrigger::BookClosed && empty && !connected)
        return QStringLiteral("nothing queued; Wi-Fi stays off");
    if (r.trigger == SyncTrigger::Wake && empty && !connected
        && pullFresh(kPreSleepPullFreshSeconds))
        return QStringLiteral("nothing queued, the last pull is fresh and Wi-Fi is off");
    return QString();
}

bool Engine::mayBringWifiUp(const SyncRequest &r) const
{
    switch (r.trigger) {
    case SyncTrigger::WifiAlreadyOn:
        return false;
    case SyncTrigger::WhileReading:
        return m_d.settings && m_d.settings->current().livePushMode == LivePushMode::ConnectBriefly;
    case SyncTrigger::BookClosed:
    case SyncTrigger::BeforeSleep:
    case SyncTrigger::Wake:
    case SyncTrigger::Manual:
        break;
    }
    return true;
}

void Engine::onReadyForSleep()
{
    // "Write the open book's session so far to the ledger, then push": the
    // recorder has written it; queue its position unless it is approximate.
    if (m_liveTimer) {
        m_clock->cancelTimer(m_liveTimer);
        m_liveTimer = 0;
    }
    SyncRequest r;
    r.trigger = SyncTrigger::BeforeSleep;
    r.contentId = openBook();
    request(r);
}

void Engine::onBookClosed(const QString &contentId)
{
    if (m_liveTimer) {
        m_clock->cancelTimer(m_liveTimer);
        m_liveTimer = 0;
    }
    if (m_d.ledger && m_d.ledger->isOpen()) {
        maybeEnqueuePosition(contentId);
        // Pulled changes that waited for this book to close (no network).
        applyPending(contentId);
    }
    SyncRequest r;
    r.trigger = SyncTrigger::BookClosed;
    r.contentId = contentId;
    request(r);
}

void Engine::onSessionFinished(const QString &contentId, const QString &)
{
    maybeEnqueuePosition(contentId);
}

void Engine::onPageEventWritten(const QString &)
{
    armLivePush();
}

void Engine::onLedgerClosed()
{
    if (m_run.active) {
        log::info(kLog, QStringLiteral("ledger closed for USB: stopping the sync"));
        finish(ErrorKind::Cancelled);
    }
}

void Engine::onNetworkChanged(bool connected)
{
    if (m_run.active && m_run.waitingForWifi) {
        if (connected)
            networkReady(true);
        return;
    }
    if (!connected || m_restoringWifi || m_run.active)
        return;
    SyncRequest r;
    r.trigger = SyncTrigger::WifiAlreadyOn;
    request(r);
}

void Engine::armLivePush()
{
    if (m_liveTimer || !m_d.settings || !m_d.recorder)
        return;
    const Settings s = m_d.settings->current();
    if (s.livePushMode == LivePushMode::Off || m_d.recorder->openContentId().isEmpty())
        return;
    const int minutes = s.livePushMinutes > 0 ? s.livePushMinutes : kDefaultLivePushMinutes;
    m_liveTimer = m_clock->startTimer(minutes * 60 * 1000, [this]() { onLiveTick(); });
}

void Engine::onLiveTick()
{
    m_liveTimer = 0;
    const QString open = m_d.recorder ? m_d.recorder->openContentId() : QString();
    if (open.isEmpty())
        return; // Re-armed by the next page event.
    SyncRequest r;
    r.trigger = SyncTrigger::WhileReading;
    r.contentId = open;
    request(r);
    armLivePush();
}

// ---------------------------------------------------------------------------
// Sleep hold
// ---------------------------------------------------------------------------

void Engine::holdSleep()
{
    m_sleepHeld = true;
    const Status s = m_d.nickel->holdSleep();
    m_nickelHoldTaken = s.ok();
    if (!s.ok())
        log::warn(kLog,
                  QStringLiteral("can't hold sleep (%1); the pre-sleep sync runs until the "
                                 "device sleeps")
                          .arg(s.message));
    m_watchdog = m_d.makeWatchdog();
    m_watchdog->start([this]() { onSleepReleased(); });
}

void Engine::onSleepReleased()
{
    // The sync finished, or the 15 s watchdog fired: the device sleeps now,
    // whatever the network is doing. Wi-Fi is put back first.
    m_sleepHeld = false;
    for (int i = m_queue.size() - 1; i >= 0; --i)
        if (m_queue.at(i).trigger == SyncTrigger::BeforeSleep)
            m_queue.removeAt(i);
    if (m_run.active)
        finish(ErrorKind::Timeout);
    else
        restoreWifi();
    if (m_nickelHoldTaken) {
        m_nickelHoldTaken = false;
        m_d.nickel->releaseSleep();
    }
}

void Engine::releaseSleepNow()
{
    if (m_sleepHeld && m_watchdog)
        m_watchdog->done(); // Runs onSleepReleased() once.
}

// ---------------------------------------------------------------------------
// One sync
// ---------------------------------------------------------------------------

void Engine::start(const SyncRequest &r)
{
    m_run = Run();
    m_run.active = true;
    m_run.gen = ++m_gen;
    m_run.req = r;
    m_run.result.trigger = r.trigger;
    switch (r.trigger) {
    case SyncTrigger::WhileReading:
        m_run.steps << Step::PushPages;
        break;
    case SyncTrigger::BookClosed:
        m_run.steps << Step::PushPages << Step::PushOutbox << Step::Pull << Step::Apply;
        break;
    case SyncTrigger::BeforeSleep:
        m_run.steps << Step::PushPages << Step::PushOutbox << Step::PullPositionsOnly;
        break;
    case SyncTrigger::Wake:
        // "Pull first (positions, highlights, status, collections), then push."
        m_run.steps << Step::Pull << Step::Apply << Step::PushPages << Step::PushOutbox;
        break;
    case SyncTrigger::WifiAlreadyOn:
    case SyncTrigger::Manual:
        m_run.steps << Step::PushPages << Step::PushOutbox << Step::Pull << Step::Apply;
        break;
    }
    m_run.pullBooks = pullBooksFor(r);
    if (r.trigger != SyncTrigger::WhileReading)
        collectLocalChanges(m_run.pullBooks);

    int timeout = kBackgroundSyncTimeoutMs;
    if (r.trigger == SyncTrigger::Manual)
        timeout = kManualSyncTimeoutMs;
    else if (r.trigger == SyncTrigger::BeforeSleep)
        timeout = kPreSleepSyncTimeoutMs;
    else if (r.trigger == SyncTrigger::WhileReading)
        timeout = kWhileReadingTimeoutMs;
    const quint64 gen = m_run.gen;
    m_run.deadlineTimer = m_clock->startTimer(timeout, [this, gen]() {
        if (!current(gen))
            return;
        m_run.deadlineTimer = 0;
        log::warn(kLog,
                  QStringLiteral("%1: timed out; stopping")
                          .arg(QLatin1String(triggerName(m_run.req.trigger))));
        finish(ErrorKind::Timeout);
    });

    log::info(kLog, QStringLiteral("%1: started").arg(QLatin1String(triggerName(r.trigger))));
    emit started(r.trigger);
    emit statusChanged();
    if (r.trigger == SyncTrigger::Manual)
        emit progress(QStringLiteral("Connecting"));
    ensureNetwork();
}

void Engine::ensureNetwork()
{
    if (m_d.nickel->networkConnected()) {
        nextStep();
        return;
    }
    if (!mayBringWifiUp(m_run.req)) {
        log::info(kLog,
                  QStringLiteral("Wi-Fi isn't connected and this trigger doesn't turn it on"));
        finish(ErrorKind::Offline);
        return;
    }
    const quint64 gen = m_run.gen;
    m_run.waitingForWifi = true;
    m_run.wifiTimer = m_clock->startTimer(kWifiConnectTimeoutMs, [this, gen]() {
        if (!current(gen))
            return;
        m_run.wifiTimer = 0;
        log::warn(kLog, QStringLiteral("Wi-Fi didn't connect in time"));
        networkReady(false);
    });
    if (!m_d.nickel->wifiEnabled()) {
        const Status s = m_d.nickel->setWifiEnabled(true);
        if (!s.ok()) {
            log::warn(kLog, QStringLiteral("can't turn Wi-Fi on: %1").arg(s.message));
            networkReady(false);
            return;
        }
        m_wifiOurs = true;
        log::info(kLog, QStringLiteral("turned Wi-Fi on"));
    }
    if (!current(gen) || !m_run.waitingForWifi)
        return;
    if (m_d.nickel->networkConnected()) {
        networkReady(true);
        return;
    }
    const Status s = m_d.nickel->connectWifi();
    if (!s.ok() && current(gen) && m_run.waitingForWifi) {
        log::warn(kLog, QStringLiteral("can't connect Wi-Fi: %1").arg(s.message));
        networkReady(false);
    }
}

void Engine::networkReady(bool ok)
{
    if (!m_run.active || !m_run.waitingForWifi)
        return;
    m_run.waitingForWifi = false;
    if (m_run.wifiTimer) {
        m_clock->cancelTimer(m_run.wifiTimer);
        m_run.wifiTimer = 0;
    }
    if (!ok) {
        finish(ErrorKind::Offline);
        return;
    }
    nextStep();
}

void Engine::nextStep()
{
    if (!m_run.active)
        return;
    if (m_run.steps.isEmpty()) {
        finish(m_run.softError);
        return;
    }
    const Step step = m_run.steps.takeFirst();
    switch (step) {
    case Step::PushPages:
        if (m_run.req.trigger == SyncTrigger::Manual)
            emit progress(QStringLiteral("Sending reading"));
        pushPages();
        break;
    case Step::PushOutbox:
        if (m_run.req.trigger == SyncTrigger::Manual)
            emit progress(QStringLiteral("Sending changes"));
        pushOutbox();
        break;
    case Step::Pull:
    case Step::PullPositionsOnly:
        if (m_run.req.trigger == SyncTrigger::Manual)
            emit progress(QStringLiteral("Getting updates"));
        m_run.pullIndex = 0;
        m_run.positionsOnly = step == Step::PullPositionsOnly;
        pullNext();
        break;
    case Step::Apply:
        applyStep();
        break;
    }
}

void Engine::finish(ErrorKind error)
{
    if (!m_run.active)
        return;
    m_run.active = false;
    m_run.waitingForWifi = false;
    if (m_run.deadlineTimer)
        m_clock->cancelTimer(m_run.deadlineTimer);
    if (m_run.wifiTimer)
        m_clock->cancelTimer(m_run.wifiTimer);
    m_run.deadlineTimer = m_run.wifiTimer = 0;
    // Cancel our own requests only: the client is shared with downloads.
    const QList<RequestId> ours = m_run.requests.values();
    m_run.requests.clear();
    for (RequestId id : ours)
        m_d.client->cancel(id);

    SyncResult &result = m_run.result;
    result.error = error;
    if (error == ErrorKind::None)
        result.outcome = SyncOutcome::Succeeded;
    else if (m_run.anyWork)
        result.outcome = SyncOutcome::Partial;
    else
        result.outcome = SyncOutcome::Failed;
    result.finishedUtc = m_clock->nowUtc();

    // A close followed by sleep shares one connection: keep Wi-Fi for the
    // pre-sleep sync that is waiting.
    bool sleepWaiting = false;
    for (const SyncRequest &q : m_queue)
        sleepWaiting = sleepWaiting || q.trigger == SyncTrigger::BeforeSleep;
    if (!sleepWaiting)
        restoreWifi();

    const bool ledgerOpen = m_d.ledger->isOpen();
    if (result.outcome == SyncOutcome::Succeeded) {
        m_lastSuccessUtc = result.finishedUtc;
        if (ledgerOpen)
            m_d.ledger->setLastSyncUtc(result.finishedUtc);
    }
    if (ledgerOpen)
        m_lastWaiting = m_d.outbox->pendingCount();

    // Toasts: after "Sync now", or when the same problem failed three syncs
    // in a row. Push while reading stays silent (invisible while you read).
    const bool counts = result.trigger != SyncTrigger::WhileReading;
    if (counts) {
        if (result.outcome == SyncOutcome::Succeeded) {
            m_consecutiveFailures = 0;
            m_lastError = ErrorKind::None;
        } else if (error != ErrorKind::Cancelled) {
            m_consecutiveFailures = error == m_lastError ? m_consecutiveFailures + 1 : 1;
            m_lastError = error;
        }
    }
    if (result.trigger == SyncTrigger::Manual) {
        m_d.nickel->showToast(result.outcome == SyncOutcome::Succeeded ? summaryText(result)
                                                                       : failureText(error));
    } else if (counts && result.outcome != SyncOutcome::Succeeded && error != ErrorKind::Cancelled
               && m_consecutiveFailures == kToastAfterConsecutiveFailures) {
        m_d.nickel->showToast(failureText(error));
    }

    log::info(kLog,
              QStringLiteral("%1: %2 (sessions %3, positions %4, highlights %5, statuses %6, "
                             "pulled %7)%8")
                      .arg(QLatin1String(triggerName(result.trigger)))
                      .arg(result.outcome == SyncOutcome::Succeeded
                                   ? QStringLiteral("done")
                                   : (result.outcome == SyncOutcome::Partial
                                              ? QStringLiteral("cut short")
                                              : QStringLiteral("failed")))
                      .arg(result.sessionsPushed)
                      .arg(result.positionsPushed)
                      .arg(result.highlightsPushed)
                      .arg(result.statusesPushed)
                      .arg(result.itemsPulled)
                      .arg(error == ErrorKind::None
                                   ? QString()
                                   : QStringLiteral(", error %1").arg(int(error))));

    const SyncTrigger trigger = result.trigger;
    const SyncResult copy = result;
    emit finished(copy);
    emit statusChanged();

    if (trigger == SyncTrigger::BeforeSleep)
        releaseSleepNow();

    if (!m_queue.isEmpty() && !m_queuedRun) {
        m_queuedRun = true;
        QMetaObject::invokeMethod(this, "runQueued", Qt::QueuedConnection);
    }
}

void Engine::restoreWifi()
{
    if (!m_wifiOurs)
        return;
    m_wifiOurs = false;
    m_restoringWifi = true;
    const Status s = m_d.nickel->setWifiEnabled(false);
    m_restoringWifi = false;
    if (s.ok())
        log::info(kLog, QStringLiteral("turned Wi-Fi off again"));
    else
        log::warn(kLog, QStringLiteral("can't turn Wi-Fi off again: %1").arg(s.message));
}

void Engine::abort()
{
    if (m_run.active) {
        log::info(kLog, QStringLiteral("sync aborted"));
        finish(ErrorKind::Cancelled);
    }
}

SyncStatus Engine::status() const
{
    SyncStatus st;
    st.running = m_run.active;
    st.online = m_d.nickel && m_d.nickel->networkConnected();
    st.consecutiveFailures = m_consecutiveFailures;
    st.lastError = m_lastError;
    st.lastSuccessUtc = m_lastSuccessUtc;
    st.waiting = m_lastWaiting;
    if (m_d.ledger && m_d.ledger->isOpen()) {
        const QDateTime stored = m_d.ledger->lastSyncUtc();
        if (stored.isValid() && (!st.lastSuccessUtc.isValid() || stored > st.lastSuccessUtc))
            st.lastSuccessUtc = stored;
        if (m_d.outbox)
            st.waiting = m_d.outbox->pendingCount();
    }
    return st;
}

// ---------------------------------------------------------------------------
// Step 1: page events (sessions), then the sweep
// ---------------------------------------------------------------------------

void Engine::pushPages()
{
    QStringList books;
    if (m_run.req.trigger == SyncTrigger::WhileReading
        || m_run.req.trigger == SyncTrigger::BookClosed) {
        if (m_d.ledger->booksWithPendingPageEvents().contains(m_run.req.contentId))
            books << m_run.req.contentId;
    } else {
        books = m_d.ledger->booksWithPendingPageEvents();
    }
    const qint64 before = m_run.req.trigger == SyncTrigger::WhileReading && m_d.recorder
            ? m_d.recorder->currentPageStartUnix()
            : 0;

    QVector<SessionUploadBook> upload;
    QSharedPointer<QHash<QString, QString>> hashToBook(new QHash<QString, QString>);
    // Per book: (start, session) of every event, to count sessions sent.
    QSharedPointer<QHash<QString, QVector<QPair<qint64, QString>>>> sessions(
            new QHash<QString, QVector<QPair<qint64, QString>>>);
    for (const QString &contentId : books) {
        BookMapEntry map;
        if (!m_d.ledger->bookMapByContentId(contentId, &map).ok() || map.partialMd5.isEmpty())
            continue;
        const QVector<PageEventRecord> events =
                m_d.ledger->pageEventsAfter(contentId, map.statsWatermark);
        SessionUploadBook b;
        b.hash = map.partialMd5.toLower();
        b.bookFileId = map.bookFileId;
        b.watermark = map.statsWatermark;
        QVector<QPair<qint64, QString>> list;
        for (const PageEventRecord &e : events) {
            const qint64 startTime = toUnix(e.startUtc);
            // Push while reading: only pages that start before the page on
            // screen's start second (ledger.h, same-second rule).
            if (m_run.req.trigger == SyncTrigger::WhileReading && startTime >= before)
                continue;
            PageEvent pe;
            pe.page = e.page;
            pe.startTime = startTime;
            pe.durationSeconds = e.durationSeconds;
            pe.totalPages = e.totalPages;
            b.events << pe;
            list << qMakePair(startTime, e.sessionUuid);
        }
        if (b.events.isEmpty())
            continue;
        hashToBook->insert(b.hash, contentId);
        sessions->insert(b.hash, list);
        upload << b;
    }
    if (upload.isEmpty()) {
        if (m_d.ledger->sweepOwed() && m_run.req.trigger != SyncTrigger::WhileReading)
            sendOwedSweep();
        else
            nextStep();
        return;
    }

    Ledger *ledger = m_d.ledger;
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    // A watermark is stored the moment the server acknowledges it, even if
    // this sync has been stopped since: the server has those events.
    auto onWatermark = [ledger, hashToBook](const QString &hash, qint64 watermark) {
        const QString contentId = hashToBook->value(hash.toLower());
        if (contentId.isEmpty() || !ledger->isOpen())
            return;
        const Status s = ledger->setStatsWatermark(contentId, watermark);
        if (!s.ok())
            log::warn(kLog, QStringLiteral("can't store the watermark: %1").arg(s.message));
    };
    auto done = [self, gen, ledger, hashToBook, sessions](const SessionUploadResult &r) {
        if (ledger->isOpen()) {
            // Rejected for good: move the watermark past the refused events
            // and log it; the events stay in the ledger.
            for (auto it = r.rejected.constBegin(); it != r.rejected.constEnd(); ++it) {
                const QString contentId = hashToBook->value(it.key());
                if (contentId.isEmpty())
                    continue;
                log::warn(kLog,
                          QStringLiteral("book %1: BookOrbit refused its page events for good; "
                                         "watermark moved past %2 so its position and later "
                                         "reading can go")
                                  .arg(contentId)
                                  .arg(it.value()));
                ledger->setStatsWatermark(contentId, it.value());
            }
            for (const QString &hash : r.unmatched)
                log::warn(kLog,
                          QStringLiteral("book %1 is unknown to BookOrbit; its reading waits")
                                  .arg(hashToBook->value(hash, hash)));
            if (r.sweepSent)
                ledger->setSweepOwed(false);
            else if (r.sweepOwed)
                ledger->setSweepOwed(true);
        }
        if (!self || !self->current(gen))
            return;
        Engine *e = self.data();
        // Sessions counted: those with at least one event the server now has.
        QSet<QString> sent;
        for (auto it = r.watermarks.constBegin(); it != r.watermarks.constEnd(); ++it) {
            const QVector<QPair<qint64, QString>> list = sessions->value(it.key());
            for (const auto &p : list)
                if (p.first <= it.value())
                    sent.insert(p.second);
        }
        e->m_run.result.sessionsPushed += sent.size();
        if (!r.watermarks.isEmpty() || !r.rejected.isEmpty())
            e->m_run.anyWork = true;
        if (!r.result.ok()) {
            e->finish(r.result.error);
            return;
        }
        if (r.sweepOwed && !r.sweepSent) {
            e->m_run.softError = r.sweepResult.error != ErrorKind::None ? r.sweepResult.error
                                                                        : ErrorKind::ServerError;
            if (stopsTheSync(r.sweepResult.error)) {
                e->finish(r.sweepResult.error);
                return;
            }
        }
        e->nextStep();
    };
    uploadSessions(m_d.client, upload, SessionUploadOptions(), onWatermark, done);
}

void Engine::sendOwedSweep()
{
    // The sweep tells BookOrbit this device measures its own sessions; a
    // sweep that didn't get through after an upload is sent again here.
    QJsonObject counts;
    counts.insert(QStringLiteral("booksMatched"), 0);
    counts.insert(QStringLiteral("pageStatsUploaded"), 0);
    counts.insert(QStringLiteral("annotationsUpserted"), 0);
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    Ledger *ledger = m_d.ledger;
    QSharedPointer<RequestId> id(new RequestId(0));
    *id = m_d.client->sendSweep(counts, [self, gen, ledger, id](const Reply<QJsonObject> &reply) {
        if (reply.result.ok() && ledger->isOpen()) {
            ledger->setSweepOwed(false);
            log::info(kLog, QStringLiteral("owed sweep sent"));
        }
        if (!self || !self->current(gen))
            return;
        self->untrack(*id);
        if (!reply.result.ok()) {
            self->finish(reply.result.error);
            return;
        }
        self->m_run.anyWork = true;
        self->nextStep();
    });
    if (current(gen))
        track(*id);
}

// ---------------------------------------------------------------------------
// Step 2: the outbox (positions, highlights, status), batches of 20
// ---------------------------------------------------------------------------

void Engine::pushOutbox()
{
    if (!m_run.active)
        return;
    if (++m_run.outboxBatches > kMaxOutboxBatchesPerSync) {
        nextStep();
        return;
    }
    const QString only =
            m_run.req.trigger == SyncTrigger::BookClosed ? m_run.req.contentId : QString();
    const QVector<OutboxItem> batch =
            m_d.outbox->nextBatch(m_clock->nowUtc(), kOutboxBatchSize, only);
    if (batch.isEmpty()) {
        nextStep();
        return;
    }
    switch (batch.first().kind) {
    case OutboxKind::Position:
        sendPosition(batch, 0);
        break;
    case OutboxKind::Annotation:
        sendAnnotation(batch, 0);
        break;
    case OutboxKind::Status:
        sendStatus(batch, 0);
        break;
    }
}

void Engine::itemFailed(const QVector<OutboxItem> &batch, int i, const HttpResult &r,
                        std::function<void()> next)
{
    m_d.outbox->markFailed(QVector<qint64>() << batch.at(i).id, m_clock->nowUtc());
    if (stopsTheSync(r.error)) {
        finish(r.error);
        return;
    }
    log::warn(kLog,
              QStringLiteral("outbox item %1 refused (%2): retried later with backoff")
                      .arg(batch.at(i).id)
                      .arg(r.message));
    m_run.softError = r.error;
    next();
}

void Engine::sendPosition(const QVector<OutboxItem> &batch, int i)
{
    if (i >= batch.size()) {
        pushOutbox();
        return;
    }
    RemoteProgress p;
    if (!parsePositionPayload(batch.at(i).payload, &p)) {
        log::warn(kLog, QStringLiteral("position item %1 is malformed").arg(batch.at(i).id));
        m_d.outbox->markFailed(QVector<qint64>() << batch.at(i).id, m_clock->nowUtc());
        sendPosition(batch, i + 1);
        return;
    }
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    QSharedPointer<RequestId> id(new RequestId(0));
    *id = m_d.client->putProgress(p, [self, gen, batch, i, id](const Reply<bool> &reply) {
        if (!self || !self->current(gen))
            return;
        Engine *e = self.data();
        e->untrack(*id);
        if (!reply.result.ok()) {
            e->itemFailed(batch, i, reply.result,
                          [e, batch, i]() { e->sendPosition(batch, i + 1); });
            return;
        }
        e->m_d.outbox->markConfirmed(QVector<qint64>() << batch.at(i).id);
        e->m_run.result.positionsPushed++;
        e->m_run.anyWork = true;
        e->sendPosition(batch, i + 1);
    });
    if (current(gen))
        track(*id);
}

void Engine::sendAnnotation(const QVector<OutboxItem> &batch, int i)
{
    if (i >= batch.size()) {
        pushOutbox();
        return;
    }
    AnnotationExchange x;
    if (!parseAnnotationPayload(batch.at(i).payload, &x)) {
        log::warn(kLog, QStringLiteral("annotation item %1 is malformed").arg(batch.at(i).id));
        m_d.outbox->markFailed(QVector<qint64>() << batch.at(i).id, m_clock->nowUtc());
        sendAnnotation(batch, i + 1);
        return;
    }
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    QSharedPointer<RequestId> id(new RequestId(0));
    const int changes = x.changes.size();
    *id = m_d.client->exchangeAnnotations(
            x.document, x.changes, x.keys, x.keysComplete, x.sidecarState,
            [self, gen, batch, i, id, changes](const Reply<QJsonObject> &reply) {
                if (!self || !self->current(gen))
                    return;
                Engine *e = self.data();
                e->untrack(*id);
                if (!reply.result.ok()) {
                    e->itemFailed(batch, i, reply.result,
                                  [e, batch, i]() { e->sendAnnotation(batch, i + 1); });
                    return;
                }
                e->m_d.outbox->markConfirmed(QVector<qint64>() << batch.at(i).id);
                e->m_run.result.highlightsPushed += changes;
                e->m_run.anyWork = true;
                e->sendAnnotation(batch, i + 1);
            });
    if (current(gen))
        track(*id);
}

void Engine::sendStatus(const QVector<OutboxItem> &batch, int i)
{
    if (i >= batch.size()) {
        pushOutbox();
        return;
    }
    const OutboxItem &item = batch.at(i);
    const QString contentId = item.contentId;
    BookStateRecord st;
    BookMapEntry map;
    if (!m_d.ledger->bookState(contentId, &st).ok()
        || !m_d.ledger->bookMapByContentId(contentId, &map).ok()) {
        // Nothing to send (no state, or the book was unlinked): done with it.
        m_d.outbox->markConfirmed(QVector<qint64>() << item.id);
        sendStatus(batch, i + 1);
        return;
    }
    // Newest change per field wins: send only fields whose local change is
    // newer than the last remote one (a remote value merged since the item
    // was queued is never overwritten).
    const bool sendStatusField = st.status != ReadStatus::Unknown
            && st.statusLocalUpdatedUtc.isValid()
            && (!st.statusRemoteUpdatedUtc.isValid()
                || st.statusLocalUpdatedUtc > st.statusRemoteUpdatedUtc);
    const bool sendRating = st.ratingLocalUpdatedUtc.isValid()
            && (!st.ratingRemoteUpdatedUtc.isValid()
                || st.ratingLocalUpdatedUtc > st.ratingRemoteUpdatedUtc);
    if (!sendStatusField) {
        sendStatusRating(batch, i, st, map.bookId, false, sendRating);
        return;
    }
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    QSharedPointer<RequestId> id(new RequestId(0));
    *id = m_d.client->setReadStatus(
            map.bookId, bookOrbitStatus(st.status),
            [self, gen, batch, i, id, st, map, sendRating](const Reply<bool> &reply) {
                if (!self || !self->current(gen))
                    return;
                Engine *e = self.data();
                e->untrack(*id);
                if (!reply.result.ok()) {
                    e->itemFailed(batch, i, reply.result,
                                  [e, batch, i]() { e->sendStatus(batch, i + 1); });
                    return;
                }
                // The rating follows (if it changed), then the item is confirmed.
                e->sendStatusRating(batch, i, st, map.bookId, true, sendRating);
            });
    if (current(gen))
        track(*id);
}

void Engine::sendStatusRating(const QVector<OutboxItem> &batch, int i, const BookStateRecord &st,
                              qint64 bookId, bool statusSent, bool sendRating)
{
    if (!sendRating) {
        statusItemDone(batch, i, st, statusSent, false);
        return;
    }
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    QSharedPointer<RequestId> id(new RequestId(0));
    *id = m_d.client->setRating(
            bookId, st.rating, [self, gen, batch, i, id, st, statusSent](const Reply<bool> &reply) {
                if (!self || !self->current(gen))
                    return;
                Engine *e = self.data();
                e->untrack(*id);
                if (!reply.result.ok()) {
                    e->itemFailed(batch, i, reply.result,
                                  [e, batch, i]() { e->sendStatus(batch, i + 1); });
                    return;
                }
                e->statusItemDone(batch, i, st, statusSent, true);
            });
    if (current(gen))
        track(*id);
}

void Engine::statusItemDone(const QVector<OutboxItem> &batch, int i, const BookStateRecord &st,
                            bool sentStatus, bool sentRating)
{
    const OutboxItem &item = batch.at(i);
    m_d.outbox->markConfirmed(QVector<qint64>() << item.id);
    if (sentStatus || sentRating) {
        m_run.result.statusesPushed++;
        m_run.anyWork = true;
        // What the server holds now, so the next pull doesn't take our own
        // change for a remote one, and what was pushed, so it isn't queued again.
        QJsonObject seen = cacheJson(cacheKey("remote-state", item.contentId));
        QJsonObject pushed = cacheJson(cacheKey("state-pushed", item.contentId));
        if (sentStatus) {
            seen.insert(QStringLiteral("status"), bookOrbitStatus(st.status));
            pushed.insert(QStringLiteral("statusAt"),
                          static_cast<double>(st.statusLocalUpdatedUtc.toMSecsSinceEpoch()));
        }
        if (sentRating) {
            seen.insert(QStringLiteral("rating"), st.rating);
            pushed.insert(QStringLiteral("ratingAt"),
                          static_cast<double>(st.ratingLocalUpdatedUtc.toMSecsSinceEpoch()));
        }
        putCacheJson(cacheKey("remote-state", item.contentId), seen);
        putCacheJson(cacheKey("state-pushed", item.contentId), pushed);
    }
    sendStatus(batch, i + 1);
}

// ---------------------------------------------------------------------------
// Step 3: pull (remote positions for the jump prompt; status and rating)
// ---------------------------------------------------------------------------

void Engine::pullNext()
{
    if (!m_run.active)
        return;
    const bool positionsOnly = m_run.positionsOnly;
    if (m_run.pullIndex >= m_run.pullBooks.size()) {
        if (m_d.ledger->isOpen()) {
            const QDateTime now = m_clock->nowUtc();
            // BookOrbit has no change feed (VERIFY-FINDINGS item 8): the
            // cursor is the time of the pull.
            m_d.ledger->setPullCursor(now.toString(Qt::ISODate), now);
        }
        nextStep();
        return;
    }
    const QString contentId = m_run.pullBooks.at(m_run.pullIndex++);
    BookMapEntry map;
    if (!m_d.ledger->bookMapByContentId(contentId, &map).ok() || map.partialMd5.isEmpty()) {
        pullNext();
        return;
    }
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    QSharedPointer<RequestId> id(new RequestId(0));
    *id = m_d.client->getProgress(
            map.partialMd5.toLower(),
            [self, gen, id, contentId, map, positionsOnly](const Reply<RemoteProgress> &reply) {
                if (!self || !self->current(gen))
                    return;
                Engine *e = self.data();
                e->untrack(*id);
                if (!reply.result.ok()) {
                    if (stopsTheSync(reply.result.error)) {
                        e->finish(reply.result.error);
                        return;
                    }
                } else if (!reply.value.document.isEmpty() && reply.value.timestamp > 0) {
                    const RemoteProgress &p = reply.value;
                    // Never prompt about this Kobo's own position.
                    if (p.deviceId != e->m_d.ledger->deviceId()) {
                        ProgressRecord known;
                        const bool have =
                                e->m_d.ledger->progress(contentId, &known).ok() && known.hasRemote;
                        const QDateTime at = fromUnix(p.timestamp);
                        if (!have || known.remoteUpdatedUtc != at
                            || known.remote.locator != p.progress) {
                            Position pos;
                            pos.locator = p.progress;
                            pos.percent = qBound(0.0, p.percentage * 100.0, 100.0);
                            // Stored for the jump prompt; never applied silently.
                            if (e->m_d.ledger->setRemoteProgress(contentId, pos, at, p.device)
                                        .ok()) {
                                e->m_run.result.itemsPulled++;
                                e->m_run.anyWork = true;
                            }
                        }
                    }
                }
                if (positionsOnly || map.bookId <= 0)
                    e->pullNext();
                else
                    e->pullStatus(contentId, map);
            });
    if (current(gen))
        track(*id);
}

void Engine::pullStatus(const QString &contentId, const BookMapEntry &map)
{
    const quint64 gen = m_run.gen;
    QPointer<Engine> self(this);
    QSharedPointer<RequestId> id(new RequestId(0));
    *id = m_d.client->book(map.bookId, [self, gen, id, contentId](const Reply<QJsonObject> &reply) {
        if (!self || !self->current(gen))
            return;
        Engine *e = self.data();
        e->untrack(*id);
        if (!reply.result.ok()) {
            if (stopsTheSync(reply.result.error)) {
                e->finish(reply.result.error);
                return;
            }
            e->pullNext();
            return;
        }
        // BookOrbit sends no change time for status and rating, so a change
        // is detected against the value seen last time: it happened after
        // the previous pull. The first sighting is the oldest possible
        // change, so any local change wins over it ("newest change wins").
        const QString key = cacheKey("remote-state", contentId);
        QJsonObject seen = e->cacheJson(key);
        const bool firstSight = seen.isEmpty();
        QDateTime changedAt = e->m_d.ledger->lastPullUtc();
        if (firstSight || !changedAt.isValid())
            changedAt = fromUnix(1);
        else
            changedAt = changedAt.addSecs(1);

        BookStateRecord remote;
        remote.contentId = contentId;
        bool any = false;
        const QJsonValue statusValue = reply.value.value(QStringLiteral("readStatus"));
        const ReadStatus status = statusFromBookOrbit(statusValue.toString());
        if (status != ReadStatus::Unknown
            && (firstSight
                || seen.value(QStringLiteral("status")).toString() != statusValue.toString())) {
            remote.status = status;
            remote.statusRemoteUpdatedUtc = changedAt;
            any = true;
        }
        const QJsonValue ratingValue = reply.value.value(QStringLiteral("rating"));
        const int rating = ratingValue.isDouble() ? qBound(0, ratingValue.toInt(), 5) : 0;
        if (reply.value.contains(QStringLiteral("rating"))
            && (firstSight || !seen.contains(QStringLiteral("rating"))
                || seen.value(QStringLiteral("rating")).toInt() != rating)) {
            remote.rating = rating;
            remote.ratingRemoteUpdatedUtc = changedAt;
            any = true;
        }
        if (any) {
            BookStateRecord before;
            const bool hadBefore = e->m_d.ledger->bookState(contentId, &before).ok();
            if (remote.status == ReadStatus::Unknown)
                remote.status = hadBefore ? before.status : ReadStatus::Unknown;
            if (!remote.ratingRemoteUpdatedUtc.isValid())
                remote.rating = hadBefore ? before.rating : 0;
            const Status s = e->m_d.ledger->mergeRemoteState(remote);
            if (s.ok()) {
                BookStateRecord after;
                if (e->m_d.ledger->bookState(contentId, &after).ok()
                    && (!hadBefore || after.status != before.status
                        || after.rating != before.rating)) {
                    e->m_run.result.itemsPulled++;
                    e->m_run.anyWork = true;
                    e->m_run.applyBooks.insert(contentId);
                }
            } else {
                log::warn(kLog, QStringLiteral("can't merge the remote state: %1").arg(s.message));
            }
        }
        if (status != ReadStatus::Unknown)
            seen.insert(QStringLiteral("status"), statusValue.toString());
        if (reply.value.contains(QStringLiteral("rating")))
            seen.insert(QStringLiteral("rating"), rating);
        e->putCacheJson(key, seen);
        e->pullNext();
    });
    if (current(gen))
        track(*id);
}

// ---------------------------------------------------------------------------
// Step 4: apply what was pulled, only to closed books
// ---------------------------------------------------------------------------

void Engine::applyStep()
{
    QStringList pending = loadApplyPending();
    for (const QString &c : m_run.applyBooks)
        if (!pending.contains(c))
            pending << c;
    QStringList still;
    for (const QString &c : pending)
        if (!applyToNickel(c))
            still << c;
    saveApplyPending(still);
    nextStep();
}

void Engine::applyPending(const QString &onlyContentId)
{
    QStringList pending = loadApplyPending();
    if (!pending.contains(onlyContentId))
        return;
    if (applyToNickel(onlyContentId)) {
        pending.removeAll(onlyContentId);
        saveApplyPending(pending);
    }
}

bool Engine::applyToNickel(const QString &contentId)
{
    // CLAUDE.md hard rule 4: never write to Nickel's database for an open book.
    if (contentId.isEmpty())
        return true;
    if (contentId == openBook())
        return false;
    BookStateRecord st;
    if (!m_d.ledger->bookState(contentId, &st).ok())
        return true;
    ContentRow row;
    const Status read = m_d.nickel->readContent(contentId, &row);
    if (read.code == StatusCode::NotFound)
        return true; // Not on this Kobo.
    if (!read.ok())
        return false;
    bool done = true;
    // Only values that came from BookOrbit: a change made on the Kobo is the
    // newer one there and is pushed instead (task 33 maps Nickel's own edits).
    const bool statusIsRemote = st.statusRemoteUpdatedUtc.isValid()
            && (!st.statusLocalUpdatedUtc.isValid()
                || st.statusRemoteUpdatedUtc > st.statusLocalUpdatedUtc);
    const int want = nickelStatus(st.status);
    if (statusIsRemote && want >= 0 && row.readStatus != want) {
        const Status s = m_d.nickel->writeReadStatus(contentId, want);
        if (!s.ok()) {
            done = false;
            log::info(kLog, QStringLiteral("status for %1 waits (%2)").arg(contentId, s.message));
        }
    }
    const bool ratingIsRemote = st.ratingRemoteUpdatedUtc.isValid()
            && (!st.ratingLocalUpdatedUtc.isValid()
                || st.ratingRemoteUpdatedUtc > st.ratingLocalUpdatedUtc);
    if (ratingIsRemote && row.rating != st.rating) {
        const Status s = m_d.nickel->writeRating(contentId, st.rating);
        if (!s.ok()) {
            done = false;
            log::info(kLog, QStringLiteral("rating for %1 waits (%2)").arg(contentId, s.message));
        }
    }
    return done;
}

QStringList Engine::loadApplyPending()
{
    QStringList out;
    QByteArray value;
    if (!m_d.ledger->cacheGet(cacheKey("apply-pending"), &value, nullptr).ok())
        return out;
    const QJsonArray a = QJsonDocument::fromJson(value).array();
    for (int i = 0; i < a.size(); ++i)
        if (!a.at(i).toString().isEmpty())
            out << a.at(i).toString();
    return out;
}

void Engine::saveApplyPending(const QStringList &books)
{
    QJsonArray a;
    for (const QString &b : books)
        a.append(b);
    m_d.ledger->cachePut(cacheKey("apply-pending"), QLatin1String(kCacheKind),
                         QJsonDocument(a).toJson(QJsonDocument::Compact), m_clock->nowUtc());
}

// ---------------------------------------------------------------------------
// Local bookkeeping: what to pull, and positions and statuses to queue
// ---------------------------------------------------------------------------

QStringList Engine::recentBooks(int max)
{
    QStringList out;
    const QString open = openBook();
    if (!open.isEmpty())
        out << open;
    const QDateTime now = m_clock->nowUtc();
    const QVector<SessionRecord> sessions =
            m_d.ledger->sessionsInRange(now.addDays(-kPullRecentDays), now.addDays(1));
    for (int i = sessions.size() - 1; i >= 0 && out.size() < max; --i)
        if (!out.contains(sessions.at(i).contentId))
            out << sessions.at(i).contentId;
    return out;
}

QStringList Engine::pullBooksFor(const SyncRequest &r)
{
    switch (r.trigger) {
    case SyncTrigger::WhileReading:
        return QStringList();
    case SyncTrigger::BookClosed:
        return QStringList() << r.contentId;
    case SyncTrigger::BeforeSleep:
        return recentBooks(kPreSleepPullMaxBooks);
    case SyncTrigger::Wake:
    case SyncTrigger::WifiAlreadyOn:
    case SyncTrigger::Manual:
        break;
    }
    return recentBooks(kPullMaxBooks);
}

void Engine::collectLocalChanges(const QStringList &books)
{
    if (!m_d.ledger || !m_d.ledger->isOpen())
        return;
    for (const QString &contentId : books) {
        if (contentId.isEmpty())
            continue;
        maybeEnqueuePosition(contentId);
        maybeEnqueueStatus(contentId);
    }
}

bool Engine::positionIsApproximate(const QString &contentId)
{
    // SPEC "Correcting a session": an approximate position isn't sent; the
    // recorder corrects it at the next wake, sleep or close.
    SessionRecord open;
    if (m_d.ledger->openSession(&open).ok() && open.contentId == contentId)
        return open.approximate;
    const QVector<SessionRecord> latest = m_d.ledger->recentSessionsForBook(contentId, 1);
    return !latest.isEmpty() && latest.first().approximate;
}

void Engine::maybeEnqueuePosition(const QString &contentId)
{
    if (contentId.isEmpty() || !m_d.ledger || !m_d.ledger->isOpen())
        return;
    BookMapEntry map;
    if (!m_d.ledger->bookMapByContentId(contentId, &map).ok() || map.partialMd5.isEmpty())
        return;
    ProgressRecord p;
    if (!m_d.ledger->progress(contentId, &p).ok() || !p.localUpdatedUtc.isValid())
        return;
    if (positionIsApproximate(contentId))
        return;
    const QString key = cacheKey("position-queued", contentId);
    const QJsonObject queued = cacheJson(key);
    const qint64 at = p.localUpdatedUtc.toMSecsSinceEpoch();
    if (static_cast<qint64>(queued.value(QStringLiteral("at")).toDouble()) >= at)
        return;
    const QString uuid = p.uuid.isEmpty() ? QStringLiteral("position:") + contentId : p.uuid;
    // The outbox holds it back while the book has unsent page events.
    const Status s = m_d.outbox->enqueue(
            OutboxKind::Position, uuid, contentId,
            positionPayload(map.partialMd5, p.local.percent, toUnix(p.localUpdatedUtc)));
    if (!s.ok()) {
        log::warn(kLog, QStringLiteral("can't queue the position: %1").arg(s.message));
        return;
    }
    QJsonObject o;
    o.insert(QStringLiteral("at"), static_cast<double>(at));
    putCacheJson(key, o);
}

void Engine::maybeEnqueueStatus(const QString &contentId)
{
    BookMapEntry map;
    BookStateRecord st;
    if (!m_d.ledger->bookMapByContentId(contentId, &map).ok() || map.bookId <= 0
        || !m_d.ledger->bookState(contentId, &st).ok())
        return;
    const QJsonObject pushed = cacheJson(cacheKey("state-pushed", contentId));
    auto newer = [](const QDateTime &local, const QDateTime &remote, double pushedAt) {
        return local.isValid() && (!remote.isValid() || local > remote)
                && local.toMSecsSinceEpoch() > static_cast<qint64>(pushedAt);
    };
    const bool status = st.status != ReadStatus::Unknown
            && newer(st.statusLocalUpdatedUtc, st.statusRemoteUpdatedUtc,
                     pushed.value(QStringLiteral("statusAt")).toDouble());
    const bool rating = newer(st.ratingLocalUpdatedUtc, st.ratingRemoteUpdatedUtc,
                              pushed.value(QStringLiteral("ratingAt")).toDouble());
    if (!status && !rating)
        return;
    const QString uuid = st.uuid.isEmpty() ? QStringLiteral("state:") + contentId : st.uuid;
    const Status s = m_d.outbox->enqueue(OutboxKind::Status, uuid, contentId,
                                         statusPayload(contentId, map.bookId));
    if (!s.ok())
        log::warn(kLog, QStringLiteral("can't queue the status: %1").arg(s.message));
}

QJsonObject Engine::cacheJson(const QString &key)
{
    QByteArray value;
    if (!m_d.ledger->cacheGet(key, &value, nullptr).ok())
        return QJsonObject();
    return QJsonDocument::fromJson(value).object();
}

void Engine::putCacheJson(const QString &key, const QJsonObject &o)
{
    m_d.ledger->cachePut(key, QLatin1String(kCacheKind),
                         QJsonDocument(o).toJson(QJsonDocument::Compact), m_clock->nowUtc());
}

} // namespace

SyncEngine *createSyncEngineWith(const EngineDeps &deps, QObject *parent)
{
    return new Engine(deps, parent);
}

} // namespace sync

SyncEngine *createSyncEngine(Ledger *ledger, Outbox *outbox, BookOrbitClient *client,
                             NickelBridge *nickel, Safety *safety, Recorder *recorder,
                             SettingsStore *settings, QObject *parent)
{
    sync::EngineDeps d;
    d.ledger = ledger;
    d.outbox = outbox;
    d.client = client;
    d.nickel = nickel;
    d.safety = safety;
    d.recorder = recorder;
    d.settings = settings;
    return sync::createSyncEngineWith(d, parent);
}

} // namespace smalt

#include "sync_engine.moc"
