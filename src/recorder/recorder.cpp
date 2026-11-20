// The recorder (task 20): Nickel's signals in, sessions and page events out.
//
// Spec: On-device data model ("How sessions are captured", "How BookOrbit
// builds sessions", "Sleeping inside a book", "Correcting a session"), Crash
// safety. Findings: VERIFY-FINDINGS items 1, 2, 5 and 10.
//
// How it works:
// - A session begins when a recordable book opens (bookOpened). It is made of
//   stretches of reading: open or wake to sleep, USB connect or close.
// - Within a stretch, every pageTurned ends the page on screen: one page event
//   for the page you left, with its start time from the device clock and its
//   duration from the monotonic clock. The page on screen ends at sleep, USB
//   connect and close. A session's active seconds are the sum of its page
//   events, so sleep is never counted.
// - Sleep pauses the session (the session so far is written, then
//   readyForSleep()). At wake, a sleep up to "New session after sleep"
//   continues it; a longer one finishes it at the moment of sleep and the
//   reading after wake starts a new session. How long the Kobo slept comes
//   from CLOCK_BOOTTIME, so a clock jump while asleep changes nothing.
// - The end position comes from Nickel's content row. If Kobalt hasn't saved
//   since the stretch began (TimeSpentReading and the position both
//   unchanged), the last saved position is recorded as approximate and
//   corrected at the next wake, sleep or close.
// - TimeSpentReading is the cross-check: its change over a session is logged
//   against the measured active time. When no page turn is observed in a
//   whole session, each stretch's page events are built from its start, its
//   active time and its start and end percentage, and the session is marked
//   estimated.
// - USB connect pauses the session like sleep and closes the ledger; USB
//   disconnect reopens it and resumes (same threshold as sleep).
// - start() finishes a session a crash left open, at its last page event.
//
// Everything runs on Nickel's UI thread. Ledger calls are single short
// statements; nothing here waits on the network or on Nickel.
#include "recorder_internal.h"

#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/nickel.h"
#include "smalt/settings.h"

#include <QVector>

namespace smalt {
namespace recorder {
namespace {

const char kLog[] = "recorder";

qint64 unixSeconds(const QDateTime &t)
{
    return t.isValid() ? t.toMSecsSinceEpoch() / 1000 : 0;
}

// Milliseconds to whole seconds, rounded to nearest.
int roundedSeconds(qint64 ms)
{
    return ms <= 0 ? 0 : int((ms + 500) / 1000);
}

// What the recorder needs from Nickel's content row at one moment.
struct RowSnapshot
{
    bool valid = false;
    Position position;
    qint64 timeSpentReading = 0;
    QDateTime dateLastRead;
};

// One instant on all three clocks.
struct Moment
{
    QDateTime wall;
    qint64 mono = 0;
    qint64 boot = -1;
};

// A finished session whose end position wasn't fresh, waiting for Kobalt's
// write (SPEC, "Correcting a session").
struct Correction
{
    QString uuid;
    QString contentId;
    RowSnapshot base; // The row the approximate end came from.
};

int percentPage(const RowSnapshot &snap)
{
    if (!snap.valid)
        return 0;
    return qBound(0, int(snap.position.percent), kPercentScalePages);
}

// Kobalt saved since `base`: it adds the interval's reading time and writes
// the position in one transaction (VERIFY-FINDINGS items 1 and 2). Its open
// write is status only, so DateLastRead alone isn't a sign.
bool savedSince(const RowSnapshot &now, const RowSnapshot &base)
{
    if (!now.valid)
        return false;
    if (!base.valid)
        return true;
    return now.timeSpentReading > base.timeSpentReading
            || now.position.locator != base.position.locator
            || !qFuzzyCompare(1.0 + now.position.percent, 1.0 + base.position.percent);
}

class RecorderImpl : public Recorder
{
    Q_OBJECT

public:
    RecorderImpl(Ledger *ledger, NickelBridge *nickel, SettingsStore *settings,
                 std::shared_ptr<RecorderClock> clock)
        : m_ledger(ledger), m_nickel(nickel), m_settings(settings), m_clock(std::move(clock))
    {
    }

    void start() override;
    void stop() override;

    QString openContentId() const override
    {
        return m_hasSession && m_phase == Phase::Reading ? m_session.contentId : QString();
    }

    qint64 currentPageStartUnix() const override
    {
        return m_hasSession && m_phase == Phase::Reading ? unixSeconds(m_pageStart.wall) : 0;
    }

    bool ledgerClosedForUsb() const override { return m_usbClosed; }

private slots:
    void onBookOpened(const QString &contentId);
    void onBookClosed(const QString &contentId);
    void onAboutToSleep();
    void onWokeUp();
    void onUsbConnected();
    void onUsbDisconnected();
    void onPageTurned(const QString &contentId, int page, int totalPages);
    void onAboutToPowerOff();

private:
    enum class Phase { None, Reading, Asleep, UsbPaused };

    Moment now() const;
    RowSnapshot readRow(const QString &contentId);
    bool recordable(const QString &contentId);
    int newSessionAfterSleepSeconds() const;

    void recoverOpenSession();
    void beginSession(const Moment &m, bool keepPage = false);
    void startStretch(const Moment &m, const RowSnapshot &snap);
    void endStretch(const Moment &m, const RowSnapshot &endSnap);
    void synthesiseStretch(const Moment &m, const RowSnapshot &endSnap);
    void pauseSession(const Moment &m, Phase phase);
    void resumeAfterPause(const Moment &m);
    void closeSession(const Moment &m);
    void finishSession(const QDateTime &endUtc, const RowSnapshot &latest);

    void resolveOnScreenPage(int nextPage, int nextTotal, int *page, int *total) const;
    void writePageEvent(int page, int total, const QDateTime &startUtc, int durationSeconds);
    void setEndFrom(const RowSnapshot &snap, bool fresh);
    void correctPausedEnd(const RowSnapshot &snap);
    void writeOpenSession();
    void maybeWriteOpenSession(const Moment &m);
    void storeProgress(const QString &contentId, const RowSnapshot &snap);
    void applyCorrections(const QString &onlyContentId = QString());
    QDateTime clampedEnd(const QDateTime &t) const;

    Ledger *m_ledger;
    NickelBridge *m_nickel;
    SettingsStore *m_settings;
    std::shared_ptr<RecorderClock> m_clock;

    bool m_started = false;
    bool m_usbClosed = false;
    QString m_bookOpen; // The book Nickel has open (recordable or not).
    QStringList m_closedWhileUsb; // Books closed while the ledger was closed.

    // The session (valid while m_hasSession).
    bool m_hasSession = false;
    Phase m_phase = Phase::None;
    SessionRecord m_session;
    qint64 m_sessionStartTsr = 0;
    bool m_sessionStartTsrValid = false;
    int m_turnsInSession = 0;
    bool m_turnsSeen = false; // Page turns observed since start(): the hook works.

    // The stretch being read and the page on screen.
    Moment m_stretchStart;
    RowSnapshot m_stretchSnap;
    Moment m_pageStart;
    int m_pageNumber = -1; // -1: not known.
    int m_pageTotal = 0;
    qint64 m_lastWriteMono = 0;

    // The pause (sleep or USB).
    Moment m_pause;
    RowSnapshot m_pauseSnap;

    QVector<Correction> m_corrections;
};

Moment RecorderImpl::now() const
{
    Moment m;
    m.wall = m_clock->nowUtc();
    m.mono = m_clock->monotonicMs();
    m.boot = m_clock->sinceBootMs();
    return m;
}

RowSnapshot RecorderImpl::readRow(const QString &contentId)
{
    RowSnapshot snap;
    ContentRow row;
    const Status s = m_nickel->readContent(contentId, &row);
    if (!s.ok()) {
        log::warn(kLog,
                  QStringLiteral("content row of %1 unreadable: %2").arg(contentId, s.message));
        return snap;
    }
    snap.valid = true;
    snap.position.locator = row.chapterIdBookmarked;
    snap.position.percent = qBound(0.0, row.percentRead, 100.0);
    snap.timeSpentReading = row.timeSpentReading;
    snap.dateLastRead = row.dateLastRead;
    return snap;
}

// Books whose ContentID is a file on the user partition (sideloaded or
// downloaded by Smalt: EPUB, PDF, CBZ), plus books already in book_map (a
// migrated Kobo-sync copy keeps its UUID ContentID). Kobo store books (DRM,
// UUID ContentIDs) aren't recorded. Reading of a file that isn't matched yet
// is kept: the ledger makes it pending once the book is matched (ledger.h).
bool RecorderImpl::recordable(const QString &contentId)
{
    if (contentId.startsWith(QLatin1String("file://")))
        return true;
    if (!m_ledger->isOpen())
        return false;
    BookMapEntry entry;
    return m_ledger->bookMapByContentId(contentId, &entry).ok();
}

int RecorderImpl::newSessionAfterSleepSeconds() const
{
    return m_settings ? m_settings->current().newSessionAfterSleepSeconds
                      : kDefaultNewSessionAfterSleepSeconds;
}

QDateTime RecorderImpl::clampedEnd(const QDateTime &t) const
{
    // A clock that went backwards while asleep must not end a session before
    // it started (the ledger refuses that).
    return t < m_session.startUtc ? m_session.startUtc : t;
}

// --- Lifecycle ---

void RecorderImpl::start()
{
    if (m_started)
        return;
    m_started = true;
    connect(m_nickel, &NickelBridge::bookOpened, this, &RecorderImpl::onBookOpened);
    connect(m_nickel, &NickelBridge::bookClosed, this, &RecorderImpl::onBookClosed);
    connect(m_nickel, &NickelBridge::aboutToSleep, this, &RecorderImpl::onAboutToSleep);
    connect(m_nickel, &NickelBridge::wokeUp, this, &RecorderImpl::onWokeUp);
    connect(m_nickel, &NickelBridge::usbConnected, this, &RecorderImpl::onUsbConnected);
    connect(m_nickel, &NickelBridge::usbDisconnected, this, &RecorderImpl::onUsbDisconnected);
    connect(m_nickel, &NickelBridge::pageTurned, this, &RecorderImpl::onPageTurned);
    connect(m_nickel, &NickelBridge::aboutToPowerOff, this, &RecorderImpl::onAboutToPowerOff);

    recoverOpenSession();

    const QString open = m_nickel->openContentId();
    if (!open.isEmpty())
        onBookOpened(open);
}

void RecorderImpl::stop()
{
    if (!m_started)
        return;
    if (m_hasSession && m_phase == Phase::Reading && !m_usbClosed) {
        const Moment m = now();
        const RowSnapshot snap = readRow(m_session.contentId);
        endStretch(m, snap);
        setEndFrom(snap, savedSince(snap, m_stretchSnap));
        writeOpenSession();
    }
    // A session left open is finished by recovery at the next start().
    m_hasSession = false;
    m_phase = Phase::None;
    disconnect(m_nickel, nullptr, this, nullptr);
    m_started = false;
}

// Crash safety: a session a crash left open ends at its last observation. Page
// events are written at every page you leave, but the session row only every
// kOpenSessionUpdateMs, so the events give the latest end and active time.
void RecorderImpl::recoverOpenSession()
{
    if (!m_ledger->isOpen())
        return;
    SessionRecord s;
    if (!m_ledger->openSession(&s).ok())
        return;
    const QVector<PageEventRecord> events =
            m_ledger->pageEventsAfter(s.contentId, unixSeconds(s.startUtc) - 1);
    qint64 sum = 0;
    int count = 0;
    QDateTime lastEnd;
    for (const PageEventRecord &e : events) {
        if (e.sessionUuid != s.uuid)
            continue;
        ++count;
        sum += e.durationSeconds;
        const QDateTime end = e.startUtc.addSecs(e.durationSeconds);
        if (!lastEnd.isValid() || end > lastEnd)
            lastEnd = end;
        if (s.startPage < 0)
            s.startPage = e.page;
        s.endPage = e.page;
        s.totalPages = e.totalPages;
    }
    if (count > 0) {
        s.activeSeconds = qMax(s.activeSeconds, sum);
        if (lastEnd > s.endUtc)
            s.endUtc = lastEnd;
    }
    if (s.endUtc < s.startUtc)
        s.endUtc = s.startUtc;
    const Status st = m_ledger->finishSession(s);
    if (!st.ok()) {
        log::error(
                kLog,
                QStringLiteral("couldn't finish session %1 left open: %2").arg(s.uuid, st.message));
        return;
    }
    log::info(kLog,
              QStringLiteral("recovered session %1 of %2 left open: ended %3, %4 s read")
                      .arg(s.uuid, s.contentId, s.endUtc.toString(Qt::ISODate))
                      .arg(s.activeSeconds));
    emit sessionFinished(s.contentId, s.uuid);
}

// --- Sessions and stretches ---

void RecorderImpl::beginSession(const Moment &m, bool keepPage)
{
    const QString contentId = m_bookOpen;
    const RowSnapshot snap = readRow(contentId);
    // An approximate end of this book's last session is corrected from the
    // row now, if Kobalt's write landed; after this the row is the new
    // session's, so an uncorrected one stays approximate.
    applyCorrections(contentId);
    for (int i = m_corrections.size() - 1; i >= 0; --i)
        if (m_corrections.at(i).contentId == contentId)
            m_corrections.remove(i);

    SessionRecord r;
    r.contentId = contentId;
    r.startUtc = m.wall;
    r.endUtc = m.wall;
    r.start = snap.position;
    r.end = snap.position;
    Status st = m_ledger->beginSession(&r);
    if (!st.ok() && st.code == StatusCode::InvalidArgument) {
        // Another session is still open in the ledger (missed signal): finish
        // it at its last observation and try once more.
        recoverOpenSession();
        r.uuid.clear();
        st = m_ledger->beginSession(&r);
    }
    if (!st.ok()) {
        log::error(
                kLog,
                QStringLiteral("couldn't begin a session for %1: %2").arg(contentId, st.message));
        m_hasSession = false;
        m_phase = Phase::None;
        return;
    }
    m_session = r;
    m_hasSession = true;
    m_sessionStartTsr = snap.timeSpentReading;
    m_sessionStartTsrValid = snap.valid;
    m_turnsInSession = 0;
    if (!keepPage) {
        m_pageNumber = -1;
        m_pageTotal = 0;
    }
    log::info(kLog,
              QStringLiteral("session %1 began: %2 at %3%")
                      .arg(r.uuid, contentId)
                      .arg(snap.position.percent));
    startStretch(m, snap);
}

void RecorderImpl::startStretch(const Moment &m, const RowSnapshot &snap)
{
    m_phase = Phase::Reading;
    m_stretchStart = m;
    m_stretchSnap = snap;
    // The page on screen starts again now (after a wake, the same page).
    m_pageStart = m;
    m_lastWriteMono = m.mono;
}

// Ends the page on screen at `m`. When page turns can't be observed (none in
// this session, none since start(), and the page number isn't known), builds
// the stretch's page events from its percentages instead.
void RecorderImpl::endStretch(const Moment &m, const RowSnapshot &endSnap)
{
    if (m_turnsInSession == 0 && m_pageNumber < 0 && !m_turnsSeen) {
        synthesiseStretch(m, endSnap);
    } else {
        int page = 0;
        int total = 0;
        resolveOnScreenPage(-1, 0, &page, &total);
        writePageEvent(page, total, m_pageStart.wall, roundedSeconds(m.mono - m_pageStart.mono));
    }
    m_session.endUtc = clampedEnd(m.wall);
}

// Page turns couldn't be observed: the stretch's active time spread evenly
// over the pages from its start percentage to its end percentage, back to
// back from its start (SPEC, "How sessions are captured", source 3).
void RecorderImpl::synthesiseStretch(const Moment &m, const RowSnapshot &endSnap)
{
    const int seconds = roundedSeconds(m.mono - m_stretchStart.mono);
    const int first = percentPage(m_stretchSnap);
    const int last = endSnap.valid ? percentPage(endSnap) : first;
    int pages = qAbs(last - first) + 1;
    if (pages > seconds)
        pages = qMax(1, seconds); // At least a second per page.
    int offset = 0;
    for (int i = 0; i < pages; ++i) {
        const int page = pages == 1 ? last : first + (last - first) * i / (pages - 1);
        const int duration = seconds / pages + (i == pages - 1 ? seconds % pages : 0);
        writePageEvent(page, kPercentScalePages, m_stretchStart.wall.addSecs(offset), duration);
        offset += duration;
    }
    m_session.pagesTurned += pages - 1;
    m_session.estimated = true;
}

void RecorderImpl::resolveOnScreenPage(int nextPage, int nextTotal, int *page, int *total) const
{
    if (m_pageNumber >= 0) {
        *page = m_pageNumber;
        *total = m_pageTotal > 0 ? m_pageTotal : nextTotal;
    } else if (nextPage >= 0) {
        // The first page of a stretch isn't reported; the turn says where it went.
        *page = qMax(0, nextPage - 1);
        *total = nextTotal;
    } else {
        *page = percentPage(m_stretchSnap);
        *total = kPercentScalePages;
    }
    *total = qMax(*total, qMax(1, *page));
}

void RecorderImpl::writePageEvent(int page, int total, const QDateTime &startUtc,
                                  int durationSeconds)
{
    PageEventRecord e;
    e.sessionUuid = m_session.uuid;
    e.contentId = m_session.contentId;
    e.page = page;
    e.totalPages = total;
    e.startUtc = startUtc;
    e.durationSeconds = durationSeconds;
    const Status st = m_ledger->appendPageEvent(e);
    // Counted even if the write failed: the session row stays the record of
    // what was read on this device.
    m_session.activeSeconds += durationSeconds;
    if (m_session.startPage < 0)
        m_session.startPage = page;
    m_session.endPage = page;
    m_session.totalPages = total;
    if (!st.ok()) {
        log::warn(kLog,
                  QStringLiteral("page event %1 of %2 not written: %3")
                          .arg(page)
                          .arg(m_session.contentId, st.message));
        return;
    }
    emit pageEventWritten(m_session.contentId);
}

void RecorderImpl::setEndFrom(const RowSnapshot &snap, bool fresh)
{
    // Not fresh: the last saved position, marked approximate (SPEC,
    // "Sleeping inside a book").
    if (snap.valid)
        m_session.end = snap.position;
    m_session.approximate = !fresh;
    if (fresh)
        storeProgress(m_session.contentId, snap);
}

void RecorderImpl::correctPausedEnd(const RowSnapshot &snap)
{
    if (!m_session.approximate || !savedSince(snap, m_pauseSnap))
        return;
    m_session.end = snap.position;
    m_session.approximate = false;
    const Status st = m_ledger->correctSessionEnd(m_session.uuid, snap.position, false);
    if (!st.ok())
        log::warn(
                kLog,
                QStringLiteral("couldn't correct session %1: %2").arg(m_session.uuid, st.message));
    else
        log::info(kLog,
                  QStringLiteral("session %1 end corrected to %2%")
                          .arg(m_session.uuid)
                          .arg(snap.position.percent));
    storeProgress(m_session.contentId, snap);
}

void RecorderImpl::writeOpenSession()
{
    const Status st = m_ledger->updateOpenSession(m_session);
    if (!st.ok())
        log::warn(kLog,
                  QStringLiteral("couldn't write session %1 so far: %2")
                          .arg(m_session.uuid, st.message));
}

void RecorderImpl::maybeWriteOpenSession(const Moment &m)
{
    if (m.mono - m_lastWriteMono < kOpenSessionUpdateMs)
        return;
    m_lastWriteMono = m.mono;
    m_session.endUtc = clampedEnd(m.wall);
    writeOpenSession();
}

void RecorderImpl::storeProgress(const QString &contentId, const RowSnapshot &snap)
{
    if (!snap.valid)
        return;
    const QDateTime at =
            snap.dateLastRead.isValid() ? snap.dateLastRead.toUTC() : m_clock->nowUtc();
    const Status st = m_ledger->setLocalProgress(contentId, snap.position, at);
    if (!st.ok())
        log::warn(kLog,
                  QStringLiteral("couldn't store position of %1: %2").arg(contentId, st.message));
}

void RecorderImpl::applyCorrections(const QString &onlyContentId)
{
    for (int i = m_corrections.size() - 1; i >= 0; --i) {
        const Correction c = m_corrections.at(i);
        if (!onlyContentId.isEmpty() && c.contentId != onlyContentId)
            continue;
        if (onlyContentId.isEmpty() && m_hasSession && m_session.contentId == c.contentId)
            continue; // That book is being read again; handled at its begin.
        const RowSnapshot snap = readRow(c.contentId);
        if (!savedSince(snap, c.base))
            continue;
        const Status st = m_ledger->correctSessionEnd(c.uuid, snap.position, false);
        if (!st.ok()) {
            log::warn(kLog,
                      QStringLiteral("couldn't correct session %1: %2").arg(c.uuid, st.message));
            if (st.code == StatusCode::NotFound)
                m_corrections.remove(i);
            continue;
        }
        log::info(kLog,
                  QStringLiteral("session %1 end corrected to %2%")
                          .arg(c.uuid)
                          .arg(snap.position.percent));
        storeProgress(c.contentId, snap);
        m_corrections.remove(i);
    }
}

void RecorderImpl::pauseSession(const Moment &m, Phase phase)
{
    const RowSnapshot snap = readRow(m_session.contentId);
    endStretch(m, snap);
    setEndFrom(snap, savedSince(snap, m_stretchSnap));
    m_phase = phase;
    m_pause = m;
    m_pauseSnap = snap;
    writeOpenSession();
}

void RecorderImpl::resumeAfterPause(const Moment &m)
{
    qint64 paused;
    if (m.boot >= 0 && m_pause.boot >= 0)
        paused = (m.boot - m_pause.boot) / 1000;
    else
        paused = m_pause.wall.secsTo(m.wall);
    paused = qMax<qint64>(0, paused);

    const RowSnapshot snap = readRow(m_session.contentId);
    correctPausedEnd(snap);
    const bool bookOpen = m_bookOpen == m_session.contentId;
    const int threshold = newSessionAfterSleepSeconds();
    if (bookOpen && (threshold == kNewSessionAfterSleepNever || paused <= threshold)) {
        log::info(kLog,
                  QStringLiteral("session %1 continues after %2 s paused")
                          .arg(m_session.uuid)
                          .arg(paused));
        startStretch(m, snap);
        return;
    }
    log::info(kLog,
              QStringLiteral("session %1 ends at the pause (%2 s paused)")
                      .arg(m_session.uuid)
                      .arg(paused));
    finishSession(m_pause.wall, snap);
    // The page on screen after the pause is the one before it.
    if (bookOpen && recordable(m_bookOpen))
        beginSession(m, true);
}

void RecorderImpl::closeSession(const Moment &m)
{
    const RowSnapshot snap = readRow(m_session.contentId);
    if (m_phase == Phase::Reading) {
        endStretch(m, snap);
        setEndFrom(snap, savedSince(snap, m_stretchSnap));
        finishSession(m.wall, snap);
    } else {
        // Closed while paused: the session ended at the pause.
        correctPausedEnd(snap);
        finishSession(m_pause.wall, snap);
    }
}

void RecorderImpl::finishSession(const QDateTime &endUtc, const RowSnapshot &latest)
{
    m_session.endUtc = clampedEnd(endUtc);

    // Cross-check (SPEC, "How sessions are captured", source 3).
    if (m_sessionStartTsrValid && latest.valid) {
        const qint64 delta = latest.timeSpentReading - m_sessionStartTsr;
        const qint64 active = m_session.activeSeconds;
        if (delta <= 0)
            log::info(kLog,
                      QStringLiteral("session %1: %2 s read; TimeSpentReading unchanged")
                              .arg(m_session.uuid)
                              .arg(active));
        else if (qAbs(delta - active) > qMax<qint64>(60, active / 5))
            log::warn(kLog,
                      QStringLiteral("session %1: %2 s read, TimeSpentReading +%3 s")
                              .arg(m_session.uuid)
                              .arg(active)
                              .arg(delta));
    }

    const Status st = m_ledger->finishSession(m_session);
    const QString uuid = m_session.uuid;
    const QString contentId = m_session.contentId;
    m_hasSession = false;
    m_phase = Phase::None;
    if (!st.ok()) {
        // Left open in the ledger; recovery finishes it at the next start().
        log::error(kLog, QStringLiteral("couldn't finish session %1: %2").arg(uuid, st.message));
        return;
    }
    if (m_session.approximate) {
        Correction c;
        c.uuid = uuid;
        c.contentId = contentId;
        c.base = latest.valid ? latest : m_pauseSnap;
        m_corrections.append(c);
    }
    log::info(
            kLog,
            QStringLiteral("session %1 finished: %2 s read, %3 to %4%5")
                    .arg(uuid)
                    .arg(m_session.activeSeconds)
                    .arg(m_session.startUtc.toString(Qt::ISODate),
                         m_session.endUtc.toString(Qt::ISODate),
                         m_session.approximate ? QStringLiteral(" (approximate end)") : QString()));
    emit sessionFinished(contentId, uuid);
}

// --- Nickel's signals ---

void RecorderImpl::onBookOpened(const QString &contentId)
{
    if (contentId.isEmpty())
        return;
    if (m_hasSession) {
        if (m_session.contentId == contentId && m_phase == Phase::Reading)
            return; // Repeated signal.
        if (m_usbClosed) {
            m_bookOpen = contentId;
            return;
        }
        // The previous book's close was missed.
        log::warn(kLog,
                  QStringLiteral("%1 opened while %2 was still open")
                          .arg(contentId, m_session.contentId));
        closeSession(now());
    }
    m_bookOpen = contentId;
    if (m_usbClosed)
        return; // Begins when the ledger reopens.
    if (!recordable(contentId)) {
        log::info(kLog,
                  QStringLiteral("not recording %1 (not a file on the user partition)")
                          .arg(contentId));
        return;
    }
    beginSession(now());
}

void RecorderImpl::onBookClosed(const QString &contentId)
{
    const QString closing = contentId.isEmpty() ? m_bookOpen : contentId;
    if (closing.isEmpty())
        return;
    if (closing == m_bookOpen)
        m_bookOpen.clear();
    if (m_usbClosed) {
        m_closedWhileUsb.append(closing);
        return;
    }
    bool recorded = false;
    if (m_hasSession && m_session.contentId == closing) {
        closeSession(now());
        recorded = true;
    }
    applyCorrections();
    if (recorded || recordable(closing))
        emit bookClosedRecorded(closing);
}

void RecorderImpl::onAboutToSleep()
{
    if (m_hasSession && m_phase == Phase::Reading && !m_usbClosed)
        pauseSession(now(), Phase::Asleep);
    if (!m_usbClosed)
        applyCorrections();
    emit readyForSleep();
}

void RecorderImpl::onWokeUp()
{
    if (m_usbClosed)
        return;
    applyCorrections();
    if (m_hasSession && m_phase == Phase::Asleep)
        resumeAfterPause(now());
}

void RecorderImpl::onUsbConnected()
{
    if (m_usbClosed)
        return;
    if (m_hasSession && m_phase == Phase::Reading)
        pauseSession(now(), Phase::UsbPaused);
    m_ledger->close();
    m_usbClosed = true;
    log::info(kLog, QStringLiteral("ledger closed for USB"));
    emit ledgerClosed();
}

void RecorderImpl::onUsbDisconnected()
{
    if (!m_usbClosed)
        return;
    const Status st = m_ledger->open();
    m_usbClosed = false;
    if (!st.ok())
        log::error(kLog,
                   QStringLiteral("couldn't reopen the ledger after USB: %1").arg(st.message));
    else
        log::info(kLog, QStringLiteral("ledger reopened after USB"));
    emit ledgerReopened();

    const Moment m = now();
    const QStringList closed = m_closedWhileUsb;
    m_closedWhileUsb.clear();
    if (m_hasSession) {
        if (m_phase == Phase::UsbPaused || m_phase == Phase::Asleep) {
            resumeAfterPause(m);
        } else if (m_bookOpen != m_session.contentId) {
            closeSession(m);
        }
    }
    for (const QString &id : closed)
        if (recordable(id))
            emit bookClosedRecorded(id);
    if (!m_hasSession && !m_bookOpen.isEmpty() && recordable(m_bookOpen))
        beginSession(m);
}

void RecorderImpl::onPageTurned(const QString &contentId, int page, int totalPages)
{
    if (!m_hasSession || m_phase != Phase::Reading || m_usbClosed
        || contentId != m_session.contentId)
        return;
    const Moment m = now();
    ++m_turnsInSession;
    m_turnsSeen = true;
    ++m_session.pagesTurned;
    if (unixSeconds(m.wall) <= unixSeconds(m_pageStart.wall)) {
        // Left within the second it started: one event per start second (the
        // ledger and BookOrbit key events on page and start second), so this
        // page's moment joins the next page.
        if (page >= 0) {
            m_pageNumber = page;
            if (totalPages > 0)
                m_pageTotal = totalPages;
        }
        return;
    }
    int leftPage = 0;
    int leftTotal = 0;
    resolveOnScreenPage(page, totalPages, &leftPage, &leftTotal);
    writePageEvent(leftPage, leftTotal, m_pageStart.wall,
                   roundedSeconds(m.mono - m_pageStart.mono));
    m_pageStart = m;
    m_pageNumber = page >= 0 ? page : -1;
    if (page >= 0 && totalPages > 0)
        m_pageTotal = totalPages;
    m_session.endUtc = clampedEnd(m.wall);
    maybeWriteOpenSession(m);
}

void RecorderImpl::onAboutToPowerOff()
{
    if (m_hasSession && !m_usbClosed) {
        log::info(kLog, QStringLiteral("power-off: finishing session %1").arg(m_session.uuid));
        closeSession(now());
    }
}

} // namespace

std::unique_ptr<Recorder> createRecorderWith(Ledger *ledger, NickelBridge *nickel,
                                             SettingsStore *settings,
                                             std::shared_ptr<RecorderClock> clock)
{
    if (!clock)
        clock = createSystemRecorderClock();
    return std::unique_ptr<Recorder>(new RecorderImpl(ledger, nickel, settings, clock));
}

} // namespace recorder

std::unique_ptr<Recorder> createRecorder(Ledger *ledger, NickelBridge *nickel,
                                         SettingsStore *settings)
{
    return recorder::createRecorderWith(ledger, nickel, settings, nullptr);
}

} // namespace smalt

#include "recorder.moc"
