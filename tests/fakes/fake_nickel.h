// FakeNickel: an in-memory NickelBridge for desktop tests.
//
// Holds content rows and collections in memory, records what was asked of it,
// and lets a test emit Nickel's signals (open, close, sleep, wake, USB, Wi-Fi).
// It enforces the same rule as the real bridge: writes for an open book are
// refused with StatusCode::Busy (CLAUDE.md hard rule 4).
//
// Owned by task 00. Tasks 20, 21, 30 and 31 may extend it (propose the change
// in the PR if it alters behaviour other tests rely on).
#pragma once

#include "smalt/nickel.h"

#include <QHash>
#include <QStringList>

namespace smalt {
namespace testing {

class FakeNickel : public NickelBridge
{
    Q_OBJECT

public:
    explicit FakeNickel(QObject *parent = nullptr);

    // --- Test setup ---
    void setReady(bool ready) { m_ready = ready; }

    void setDeviceModel(DeviceModel model) { m_model = model; }

    void setBatteryPercent(int percent) { m_battery = percent; }

    void putContent(const ContentRow &row) { m_content.insert(row.contentId, row); }

    ContentRow content(const QString &contentId) const { return m_content.value(contentId); }

    // Simulates Kobalt adding an interval's active seconds and its position write.
    void addReadingTime(const QString &contentId, qint64 seconds);
    void setPosition(const QString &contentId, const QString &chapterId, double percent);

    void setFailNextWrite(bool fail) { m_failNextWrite = fail; }

    // --- Driving signals (each also updates the fake's state) ---
    void simulateNickelReady();
    void simulateBookOpened(const QString &contentId);
    void simulateBookClosed();
    void simulateSleep();
    void simulateWake();
    void simulateUsbConnected();
    void simulateUsbDisconnected();
    void simulateNetwork(bool connected);
    void simulatePageTurn(int page, int totalPages);

    // --- What was asked of the fake ---
    QStringList openedBooks; // openBook() calls.
    QStringList importedFiles; // importFiles() calls.
    QStringList toasts; // showToast() calls.
    int nickelHomeShown = 0;
    int sleepHolds = 0; // Currently held.
    int wifiEnableCalls = 0;
    int wifiDisableCalls = 0;
    bool usbConnectedNow = false;

    // --- NickelBridge ---
    bool isReady() const override { return m_ready; }

    DeviceModel deviceModel() const override { return m_model; }

    QString openContentId() const override { return m_open; }

    int batteryPercent() const override { return m_battery; }

    Status openBook(const QString &contentId) override;
    Status importFiles(const QStringList &paths) override;
    Status showNickelHome() override;

    Status readContent(const QString &contentId, ContentRow *out) override;
    Status listCollections(QVector<NickelCollection> *out) override;

    Status writePosition(const QString &contentId, const QString &chapterIdBookmarked,
                         double percentRead, const QDateTime &dateLastReadUtc) override;
    Status writeReadStatus(const QString &contentId, int readStatus) override;
    Status writeRating(const QString &contentId, int rating) override;
    Status writeCollection(const NickelCollection &collection) override;
    Status removeCollection(const QString &name) override;
    Status deleteBook(const QString &contentId) override;

    bool wifiEnabled() const override { return m_wifi; }

    bool networkConnected() const override { return m_connected; }

    Status setWifiEnabled(bool enabled) override;
    Status connectWifi() override;

    Status holdSleep() override;
    void releaseSleep() override;

    void showToast(const QString &text) override { toasts.append(text); }

    // For tests: also controls whether connectWifi() succeeds.
    void setWifiConnectSucceeds(bool succeeds) { m_connectSucceeds = succeeds; }

private:
    Status checkWritable(const QString &contentId);

    bool m_ready = false;
    DeviceModel m_model = DeviceModel::ClaraColour;
    int m_battery = 80;
    QString m_open;
    bool m_wifi = false;
    bool m_connected = false;
    bool m_connectSucceeds = true;
    bool m_failNextWrite = false;
    QHash<QString, ContentRow> m_content;
    QVector<NickelCollection> m_collections;
};

} // namespace testing
} // namespace smalt
