// The recorder: turns Nickel's signals into sessions and page events in the
// ledger.
//
// Spec: On-device data model ("How sessions are captured", "Sleeping inside a
// book", "Correcting a session"), Crash safety. Findings: VERIFY-FINDINGS
// items 1, 2 and 5. Implemented by task 20 in src/recorder/.
//
// Ordering contract with the sync engine (task 21): the sync engine never
// reacts to NickelBridge's aboutToSleep() or bookClosed() directly. It reacts
// to the recorder's readyForSleep() and bookClosedRecorded(), which the
// recorder emits only after the session so far (and its finished page events)
// is in the ledger. So the pre-sleep sync always sees the reading up to the
// moment of sleep.
#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace smalt {

class Ledger;
class NickelBridge;
class SettingsStore;

class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr) : QObject(parent) { }

    ~Recorder() override = default;

    // Connects to NickelBridge's signals and recovers after a crash: an open
    // session left in the ledger (Ledger::openSession()) is finished at its
    // last observation before anything new starts. Call once, after the
    // ledger is open and Nickel is ready. Never blocks on the network.
    virtual void start() = 0;

    // Disconnects, writing the open session so far (clean shutdown, power-off).
    virtual void stop() = 0;

    // The book being read (empty when none, or asleep).
    virtual QString openContentId() const = 0;

    // For the push while reading (ledger.h, same-second rule): the start time,
    // in Unix seconds, of the page on screen; 0 when no book is being read.
    // The sync engine uploads only page events that start before this second.
    virtual qint64 currentPageStartUnix() const = 0;

    // True between usbConnected() and usbDisconnected(): the ledger is closed.
    virtual bool ledgerClosedForUsb() const = 0;

signals:
    // A page event was written for this book (the page you just left).
    void pageEventWritten(const QString &contentId);
    // The device is about to sleep and the session so far is in the ledger
    // (also emitted when no book is open). The sync engine's BeforeSleep
    // trigger starts here.
    void readyForSleep();
    // A book closed and its session is finished in the ledger. The sync
    // engine's BookClosed trigger starts here.
    void bookClosedRecorded(const QString &contentId);
    // A session finished (closed, or a sleep longer than "New session after
    // sleep" ended it). `uuid` is the ledger session.
    void sessionFinished(const QString &contentId, const QString &uuid);
    // The ledger was closed for USB / reopened after it.
    void ledgerClosed();
    void ledgerReopened();
};

// Defined by task 20 (src/recorder/). None of the arguments is owned.
std::unique_ptr<Recorder> createRecorder(Ledger *ledger, NickelBridge *nickel,
                                         SettingsStore *settings);

} // namespace smalt
