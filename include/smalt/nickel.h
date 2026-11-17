// Everything the rest of Smalt needs from Nickel, behind one interface.
//
// Spec: Architecture, On-device data model ("How sessions are captured",
// "Sleeping inside a book"), Sync engine ("Wi-Fi control"), Feature design
// ("Home screen and library", "Downloads", "Reading progress", "Collections",
// "Status and ratings"). Findings: docs/VERIFY-FINDINGS.md items 1-3, 5.
//
// The real implementation lives in src/nickel/ (NickelHook; tasks 20, 21,
// 30, 31) and only builds for the Kobo. Desktop tests use the fake in
// tests/fakes/fake_nickel.h.
//
// Rules every implementation keeps:
// - Signals are delivered on Nickel's UI thread. Slots connected to them must
//   return quickly (CLAUDE.md hard rule 2).
// - Writes to Nickel's database for a book that is open are refused with
//   StatusCode::Busy (CLAUDE.md hard rule 4).
// - Nothing here touches Kobalt's database (hard rule 8).
#pragma once

#include "smalt/device.h"
#include "smalt/status.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace smalt {

// The parts of Nickel's `content` row Smalt reads (VERIFY-FINDINGS items 1, 2).
struct ContentRow
{
    QString contentId;
    QString title;
    QString author;
    QString filePath; // Path on the user partition, for partial MD5.
    QString mimeType; // EPUB/KEPUB, PDF, CBZ.
    QString chapterIdBookmarked; // Chapter path + element ID on firmware 4.
    double percentRead = 0.0; // ___PercentRead (stays at 99 while reading on fw 4).
    qint64 timeSpentReading = 0; // Seconds; Kobalt adds each interval's active time.
    QDateTime dateLastRead; // UTC.
    int readStatus = 0; // Nickel's ReadStatus value.
    int rating = 0; // 0 = none, 1..5.
};

// A Nickel collection (shelf).
struct NickelCollection
{
    QString name;
    QStringList contentIds;
};

class NickelBridge : public QObject
{
    Q_OBJECT

public:
    explicit NickelBridge(QObject *parent = nullptr) : QObject(parent) { }

    ~NickelBridge() override = default;

    // --- State ---
    // True once Nickel has finished starting. If the nickelReady() signal was
    // missed, this checks Nickel's state directly (Crash safety, "Wait for Nickel").
    virtual bool isReady() const = 0;
    virtual DeviceModel deviceModel() const = 0;
    virtual QString openContentId() const = 0; // Empty when no book is open.
    virtual int batteryPercent() const = 0; // -1 if unknown.

    // --- Books ---
    // Hands a book to Nickel's open-book step (EPUB -> Kobalt, PDF/CBZ ->
    // Kobo's reader). Smalt never opens books itself.
    virtual Status openBook(const QString &contentId) = 0;
    // Asks Nickel to import new files quietly, without the full import screen
    // where possible.
    virtual Status importFiles(const QStringList &paths) = 0;
    // Shows Nickel's own home and library ("Kobo home" in the More tab).
    virtual Status showNickelHome() = 0;

    // --- Nickel database: reads (read-only; allowed for open books too) ---
    virtual Status readContent(const QString &contentId, ContentRow *out) = 0;
    virtual Status listCollections(QVector<NickelCollection> *out) = 0;

    // --- Nickel database: writes (closed books only; Busy otherwise) ---
    // Writes through Nickel's own functions with a newer DateLastRead so
    // Kobalt picks the position up on the next open (VERIFY-FINDINGS item 3).
    virtual Status writePosition(const QString &contentId, const QString &chapterIdBookmarked,
                                 double percentRead, const QDateTime &dateLastReadUtc) = 0;
    virtual Status writeReadStatus(const QString &contentId, int readStatus) = 0;
    virtual Status writeRating(const QString &contentId, int rating) = 0;
    virtual Status writeCollection(const NickelCollection &collection) = 0;
    virtual Status removeCollection(const QString &name) = 0;
    virtual Status deleteBook(const QString &contentId) = 0; // "Remove from Kobo".

    // --- Wi-Fi (the same Nickel functions NickelDBus uses) ---
    virtual bool wifiEnabled() const = 0;
    virtual bool networkConnected() const = 0;
    virtual Status setWifiEnabled(bool enabled) = 0;
    virtual Status connectWifi() = 0; // Async; networkChanged() reports the result.

    // --- Sleep hold (pre-sleep sync; always paired with a SleepWatchdog) ---
    virtual Status holdSleep() = 0;
    virtual void releaseSleep() = 0;

    // --- UI hooks ---
    // A toast through Nickel (SPEC, "Where sync status shows").
    virtual void showToast(const QString &text) = 0;

signals:
    void nickelReady();
    void bookOpened(const QString &contentId);
    void bookClosed(const QString &contentId);
    // After Kobalt's pre-sleep position write (VERIFY-FINDINGS item 2).
    void aboutToSleep();
    void wokeUp();
    void usbConnected(); // Close the ledger now.
    void usbDisconnected(); // Reopen it.
    void networkChanged(bool connected);
    void pageTurned(const QString &contentId, int page, int totalPages);
    void aboutToPowerOff();
};

} // namespace smalt
