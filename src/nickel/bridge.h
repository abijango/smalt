// KoboNickelBridge: the real NickelBridge (include/smalt/nickel.h), inside
// Nickel's process. Kobo target only.
//
// One class, with its methods defined in one file per owning task, so tasks
// that run in parallel never edit the same file (task 00 layout):
//
//   bridge.cpp           00: instance(), and placeholders for the methods
//                        later tasks implement (30 home and open book; 31
//                        position writes; 33 status, rating, collections,
//                        removals). Each placeholder returns NotImplemented.
//   bridge_recorder.cpp  20: readiness, device model, open book, content rows,
//                        and the hooks that emit nickelReady, bookOpened,
//                        bookClosed, aboutToSleep, wokeUp, usbConnected,
//                        usbDisconnected, pageTurned and aboutToPowerOff.
//   bridge_wifi.cpp      21: Wi-Fi state and control, the sleep hold, battery,
//                        and the hook that emits networkChanged.
//
// State lives in each file (file-local statics), not in this header: there is
// exactly one Nickel process and one bridge. Hook entry points go in the
// owning task's hooks_*.h, which plugin.cpp splices into NickelHook's tables.
//
// Rules for every method: never block Nickel's UI thread, never throw, never
// crash on a missing symbol (resolve with optional dlsyms and return
// Unavailable), and refuse writes for an open book (CLAUDE.md hard rule 4).
#pragma once

#include "smalt/nickel.h"

namespace smalt {
namespace kobo {

class KoboNickelBridge : public NickelBridge
{
public:
    // The one bridge, created on first use on Nickel's UI thread.
    static KoboNickelBridge *instance();

    // --- 20 (bridge_recorder.cpp) ---
    bool isReady() const override;
    DeviceModel deviceModel() const override;
    QString openContentId() const override;
    Status readContent(const QString &contentId, ContentRow *out) override;

    // --- 21 (bridge_wifi.cpp) ---
    int batteryPercent() const override;
    bool wifiEnabled() const override;
    bool networkConnected() const override;
    Status setWifiEnabled(bool enabled) override;
    Status connectWifi() override;
    Status holdSleep() override;
    void releaseSleep() override;

    // --- 30, 31, 33 (placeholders in bridge.cpp) ---
    Status openBook(const QString &contentId) override;
    Status importFiles(const QStringList &paths) override;
    Status showNickelHome() override;
    void showToast(const QString &text) override;
    Status listCollections(QVector<NickelCollection> *out) override;
    Status writePosition(const QString &contentId, const QString &chapterIdBookmarked,
                         double percentRead, const QDateTime &dateLastReadUtc) override;
    Status writeReadStatus(const QString &contentId, int readStatus) override;
    Status writeRating(const QString &contentId, int rating) override;
    Status writeCollection(const NickelCollection &collection) override;
    Status removeCollection(const QString &name) override;
    Status deleteBook(const QString &contentId) override;

    // Lets the hook files emit the base class's signals.
    using NickelBridge::aboutToPowerOff;
    using NickelBridge::aboutToSleep;
    using NickelBridge::bookClosed;
    using NickelBridge::bookOpened;
    using NickelBridge::networkChanged;
    using NickelBridge::nickelReady;
    using NickelBridge::pageTurned;
    using NickelBridge::usbConnected;
    using NickelBridge::usbDisconnected;
    using NickelBridge::wokeUp;

private:
    KoboNickelBridge() = default;
};

} // namespace kobo
} // namespace smalt
