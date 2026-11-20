// FakeRecorder: a Recorder (include/smalt/recorder.h) the sync tests drive
// by hand. Task 21. The tests write sessions and page events to the real
// ledger themselves, then emit the recorder's signals the way task 20's
// recorder does (after the ledger has the reading).
#pragma once

#include "smalt/recorder.h"

namespace smalt {
namespace testing {

class FakeRecorder : public Recorder
{
    Q_OBJECT

public:
    explicit FakeRecorder(QObject *parent = nullptr) : Recorder(parent) { }

    void start() override { }

    void stop() override { }

    QString openContentId() const override { return open; }

    qint64 currentPageStartUnix() const override { return pageStart; }

    bool ledgerClosedForUsb() const override { return usb; }

    // Emit the recorder's signals (they are public in Qt 5).
    void fireReadyForSleep() { emit readyForSleep(); }

    void fireBookClosed(const QString &contentId) { emit bookClosedRecorded(contentId); }

    void fireSessionFinished(const QString &contentId, const QString &uuid)
    {
        emit sessionFinished(contentId, uuid);
    }

    void firePageEventWritten(const QString &contentId) { emit pageEventWritten(contentId); }

    QString open;
    qint64 pageStart = 0;
    bool usb = false;
};

} // namespace testing
} // namespace smalt
