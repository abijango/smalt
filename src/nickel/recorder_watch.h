// The recorder's view of Nickel on the Kobo (task 20): one QObject on Nickel's
// UI thread that turns Nickel's own signals, the reader windows' lifetimes and
// the hooks in recorder_hooks.cpp into KoboNickelBridge's signals.
//
// Internal to src/nickel/ (task 20). Kobo target only.
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

namespace smalt {
namespace kobo {

class RecorderWatch : public QObject
{
    Q_OBJECT

public:
    // Created on first use, on Nickel's UI thread.
    static RecorderWatch *instance();

    // Connects to Nickel's power and USB managers and watches reader windows.
    // Safe to call more than once; later calls only retry what failed.
    void install();

    bool ready() const { return m_ready; }

    void setReady() { m_ready = true; }

    QString openContentId() const { return m_open; }

    bool usbConnected() const { return m_usb; }

    // From the hooks (UI thread).
    void bookOpening(const QString &contentId);
    void usbAboutToExport();
    void poweringOff();

public slots:
    // Queued from Nickel's event thread (Event::setExtraData).
    void sessionStartMarker(const QString &contentId);
    void pageTurnObserved(const QString &contentId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onAboutToSleepDirect();
    void onAboutToSleepQueued();
    void onResumed();
    void onUsbAboutToConnect();
    void onUsbDoneProcessing();
    void onReaderDestroyed();
    void checkNativeReaderHidden();

private:
    explicit RecorderWatch(QObject *parent = nullptr);

    void readerShown(QWidget *reader, bool kobalt);
    void open(const QString &contentId);
    void close();

    bool m_ready = false;
    bool m_filterInstalled = false;
    bool m_powerConnected = false;
    bool m_plugConnected = false;
    bool m_asleep = false;
    bool m_usb = false;
    bool m_poweringOff = false;
    QString m_pending; // From showReader: the book Nickel is opening.
    QString m_lastMarker; // From the last session-start event.
    QString m_open; // The book whose reader is showing.
    QPointer<QWidget> m_reader;
    bool m_readerIsKobalt = false;
    bool m_waitingForId = false; // A reader showed before we knew its book.
};

} // namespace kobo
} // namespace smalt
