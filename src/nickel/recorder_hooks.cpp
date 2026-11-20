// The recorder's hooks into Nickel (task 20): book open and close, sleep and
// wake, USB, page turns, power-off and readiness. Kobo target only.
//
// Sources for every symbol and signature (firmware 4.x, Qt 5):
//   [K]  Kobalt, https://github.com/nicoverbruggen/kobalt (MIT), commit
//        be47258850ffdc3295257475afdf8b8f6cb53176
//   [ND] NickelDBus, https://github.com/shermp/NickelDBus (MIT), commit
//        ab7ac0c7fee308b776a1359bb90898a21edfb54d
//
// Hooks (hooks_recorder.h), each optional:
// - Nickel3Application::finishInitialization(), void(void *):
//   [K] platforms/kobo/nickel-qt5/hooks.cpp (readerHooks) and
//   nickel-qt5/reader_controller.cpp (kobalt_finish_initialization), which
//   also calls the original first and treats its return as "Nickel ready".
// - AbstractReadingViewMixin::showReader(Volume const&, QVector<ContentKey>
//   const&, AbstractLibraryController<Volume>*, Bookmark const&),
//   void(void *, const void *, const void *, void *, const void *):
//   [K] nickel-qt5/hooks.cpp and nickel-qt5/reader_controller.cpp
//   (kobalt_show_reader). Its arguments are valid only during the call.
// - Event::setExtraData(QMap<QString, QVariant>), void(void *, QVariantMap):
//   [K] nickel-qt5/hooks.cpp and common/native_event_history.cpp
//   (kobalt_event_set_extra_data). Called on Nickel's event thread. Event
//   type 46 holds a book's page turns: "eventTimestamps" and "wordCounts",
//   one entry per turn, the page handler adding the word count last; type
//   1020 holds reading-session starts (same file, prune()).
// - N3PowerWorkflowManager::powerOff(bool), void(void *, bool), and
//   N3PowerWorkflowManager::reboot(), void(void *): [K] nickel-qt5/hooks.cpp
//   and common/native_lifecycle.cpp (kobalt_power_off, kobalt_reboot).
// - PlugManager::goGoGadgetDevice(), void(void *): [K] nickel-qt5/hooks.cpp
//   and common/native_lifecycle.cpp (kobalt_usb_connect, "Release Kobalt
//   storage before USB connection").
//
// Dlsyms (hooks_recorder.h), each optional:
// - Event::type() const, const int &(const void *), and Event::contentId()
//   const, QString(const void *): [K] common/native_event_history.cpp (Api).
// - Volume::getDbValues() const, QVariantMap(const void *), key "ContentID":
//   [K] nickel-qt5/native_progress.cpp and native_reading_session.cpp (Api).
// - N3PowerWorkflowManager::sharedInstance(), QObject *(): [K]
//   common/native_lifecycle.cpp (PowerApi, NativeLifecycle constructor).
// - PlugWorkflowManager::sharedInstance(), QObject *(): [K]
//   common/native_lifecycle.cpp; [ND] src/ndb/NDBDbus.cc.
// - Device::getCurrentDevice() and Device::isSpaKoboBW(), isSpaKobo(),
//   isMonzaKobo() const, bool(const void *): [K]
//   common/reader_host_services.cpp (syncDeviceModel).
//
// Qt signals (connected by name, as both projects do):
// - N3PowerWorkflowManager: aboutToSleep(), resumingFromSleep(),
//   resumedFromSleep(): [K] common/native_lifecycle.cpp. Kobalt saves its
//   position and reading time to Nickel in its (direct) aboutToSleep slot
//   (VERIFY-FINDINGS item 2), so Smalt reacts through a queued connection:
//   it runs after every direct slot, whatever the connection order.
// - PlugWorkflowManager: aboutToConnect(), doneProcessing(): [ND]
//   src/ndb/NDBDbus.cc (connectSignals); [K] common/native_lifecycle.cpp.
//
// Reader windows (no symbols): Kobalt's reader is a top-level window named
// "rrReader" with Qt::WA_DeleteOnClose ([K] src/app/reader/window/
// reader_build.cpp), so it is destroyed when the book closes. Nickel's own
// reader (PDF, CBZ) is a ReadingView ([K] common/home_sleep_screen.cpp,
// common/reader_coordinator.cpp; [ND] NDBDbus.cc ndbCurrentView).
#include "bridge.h"
#include "hooks_recorder.h"
#include "recorder_watch.h"

#include "smalt/log.h"

#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <utility>

namespace smalt {
namespace kobo {
namespace rec {

void (*nextFinishInitialization)(void *) = nullptr;
void (*nextShowReader)(void *, const void *, const void *, void *, const void *) = nullptr;
void (*nextEventSetExtraData)(void *, QVariantMap) = nullptr;
void (*nextPowerOff)(void *, bool) = nullptr;
void (*nextReboot)(void *) = nullptr;
void (*nextGoGadgetDevice)(void *) = nullptr;

const int &(*eventType)(const void *) = nullptr;
QString (*eventContentId)(const void *) = nullptr;
QVariantMap (*volumeDbValues)(const void *) = nullptr;
QObject *(*powerWorkflowManager)() = nullptr;
QObject *(*plugWorkflowManager)() = nullptr;
void *(*currentDevice)() = nullptr;
bool (*deviceIsSpaKoboBW)(const void *) = nullptr;
bool (*deviceIsSpaKobo)(const void *) = nullptr;
bool (*deviceIsMonzaKobo)(const void *) = nullptr;

} // namespace rec

namespace {

const char kLog[] = "nickel";

const int kPageTurnEvent = 46; // [K] native_event_history.cpp, native_reading_session.cpp
const int kSessionStartEvent = 1020; // [K] native_event_history.cpp

// Set on the UI thread once the watch exists; read on Nickel's event thread.
std::atomic<RecorderWatch *> g_watch(nullptr);

// Page-turn lists seen per book, on Nickel's event thread.
struct TurnState
{
    int size = 0;
    uint last = 0;
};

QMutex g_turnMutex;
QHash<QString, TurnState> g_turns;

// One page turn is one new entry in the book's paired lists. Kobalt trims the
// oldest entries of long lists (same last entry, shorter list): not a turn. A
// new session resets the lists: its first entry is a turn.
bool newPageTurn(const QString &contentId, const QVariantMap &data)
{
    const QVariant timesValue = data.value(QStringLiteral("eventTimestamps"));
    const QVariant wordsValue = data.value(QStringLiteral("wordCounts"));
    if (timesValue.type() != QVariant::List || wordsValue.type() != QVariant::List)
        return false;
    const QVariantList times = timesValue.toList();
    if (times.isEmpty() || times.size() != wordsValue.toList().size())
        return false; // The intermediate update, before the word count.
    const uint last = times.last().toUInt();
    QMutexLocker lock(&g_turnMutex);
    TurnState &state = g_turns[contentId];
    const bool turned = last != state.last || times.size() > state.size;
    state.size = times.size();
    state.last = last;
    return turned;
}

void observeEvent(void *event, const QVariantMap &data)
{
    RecorderWatch *watch = g_watch.load();
    if (!watch || !event || !rec::eventType || !rec::eventContentId)
        return;
    const int type = rec::eventType(event);
    if (type != kPageTurnEvent && type != kSessionStartEvent)
        return;
    const QString contentId = rec::eventContentId(event);
    if (contentId.isEmpty())
        return;
    if (type == kSessionStartEvent) {
        QMetaObject::invokeMethod(watch, "sessionStartMarker", Qt::QueuedConnection,
                                  Q_ARG(QString, contentId));
    } else if (newPageTurn(contentId, data)) {
        QMetaObject::invokeMethod(watch, "pageTurnObserved", Qt::QueuedConnection,
                                  Q_ARG(QString, contentId));
    }
}

KoboNickelBridge *bridge()
{
    return KoboNickelBridge::instance();
}

} // namespace

// --- RecorderWatch ---

RecorderWatch::RecorderWatch(QObject *parent) : QObject(parent) { }

RecorderWatch *RecorderWatch::instance()
{
    // Never destroyed: Nickel exits without unloading the mod.
    static RecorderWatch *watch = nullptr;
    if (!watch) {
        watch = new RecorderWatch;
        g_watch.store(watch);
    }
    return watch;
}

void RecorderWatch::install()
{
    if (!m_filterInstalled && qApp) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
    if (!m_powerConnected && rec::powerWorkflowManager) {
        if (QObject *power = rec::powerWorkflowManager()) {
            const bool direct = connect(power, SIGNAL(aboutToSleep()), this,
                                        SLOT(onAboutToSleepDirect()), Qt::DirectConnection);
            const bool queued = connect(power, SIGNAL(aboutToSleep()), this,
                                        SLOT(onAboutToSleepQueued()), Qt::QueuedConnection);
            const bool resuming = connect(power, SIGNAL(resumingFromSleep()), this,
                                          SLOT(onResumed()), Qt::QueuedConnection);
            const bool resumed = connect(power, SIGNAL(resumedFromSleep()), this, SLOT(onResumed()),
                                         Qt::QueuedConnection);
            m_powerConnected = direct && queued;
            log::info(kLog,
                      QStringLiteral("recorder: sleep signals %1, wake signals %2/%3")
                              .arg(m_powerConnected ? "on" : "off")
                              .arg(resuming ? "on" : "off")
                              .arg(resumed ? "on" : "off"));
        }
    }
    if (!m_plugConnected && rec::plugWorkflowManager) {
        if (QObject *plug = rec::plugWorkflowManager()) {
            const bool connectSignal = connect(plug, SIGNAL(aboutToConnect()), this,
                                               SLOT(onUsbAboutToConnect()), Qt::DirectConnection);
            const bool doneSignal = connect(plug, SIGNAL(doneProcessing()), this,
                                            SLOT(onUsbDoneProcessing()), Qt::QueuedConnection);
            m_plugConnected = connectSignal && doneSignal;
            log::info(
                    kLog,
                    QStringLiteral("recorder: USB signals %1").arg(m_plugConnected ? "on" : "off"));
        }
    }
    log::info(kLog,
              QStringLiteral("recorder hooks: ready %1, open %2, page turns %3, power-off "
                             "%4, USB export %5")
                      .arg(rec::nextFinishInitialization ? "on" : "off")
                      .arg(rec::nextShowReader && rec::volumeDbValues ? "on" : "off")
                      .arg(rec::nextEventSetExtraData && rec::eventType && rec::eventContentId
                                   ? "on"
                                   : "off")
                      .arg(rec::nextPowerOff ? "on" : "off")
                      .arg(rec::nextGoGadgetDevice ? "on" : "off"));
}

void RecorderWatch::bookOpening(const QString &contentId)
{
    m_pending = contentId;
}

void RecorderWatch::usbAboutToExport()
{
    if (m_usb)
        return;
    m_usb = true;
    // Direct: the recorder closes the ledger before Nickel exports the
    // partition.
    emit bridge() -> usbConnected();
}

void RecorderWatch::poweringOff()
{
    if (m_poweringOff)
        return; // Kobalt's shutdown path can call the hook again.
    m_poweringOff = true;
    emit bridge() -> aboutToPowerOff();
}

void RecorderWatch::sessionStartMarker(const QString &contentId)
{
    m_lastMarker = contentId;
    if (m_waitingForId && m_reader)
        open(contentId);
}

void RecorderWatch::pageTurnObserved(const QString &contentId)
{
    // The event says a page turned, not which page: -1, and the recorder
    // numbers it on the percentage scale.
    emit bridge() -> pageTurned(contentId, -1, 0);
}

bool RecorderWatch::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::Show && watched->isWidgetType()) {
        QWidget *widget = static_cast<QWidget *>(watched);
        if (widget->objectName() == QLatin1String("rrReader"))
            readerShown(widget, true);
        else if (widget->inherits("ReadingView"))
            readerShown(widget, false);
    } else if (type == QEvent::Hide && watched == m_reader.data() && !m_readerIsKobalt) {
        QTimer::singleShot(0, this, SLOT(checkNativeReaderHidden()));
    }
    return false;
}

void RecorderWatch::readerShown(QWidget *reader, bool kobalt)
{
    if (reader == m_reader.data() && !m_open.isEmpty())
        return; // Shown again (after a dialog or the sleep screen).
    if (reader != m_reader.data()) {
        if (m_reader)
            disconnect(m_reader.data(), nullptr, this, nullptr);
        m_reader = reader;
        m_readerIsKobalt = kobalt;
        connect(reader, SIGNAL(destroyed()), this, SLOT(onReaderDestroyed()));
    }
    const QString contentId = !m_pending.isEmpty() ? m_pending : m_lastMarker;
    m_pending.clear();
    if (contentId.isEmpty()) {
        m_waitingForId = true; // The session-start event will say.
        return;
    }
    open(contentId);
}

void RecorderWatch::open(const QString &contentId)
{
    if (m_open == contentId)
        return;
    if (!m_open.isEmpty())
        close();
    m_open = contentId;
    m_waitingForId = false;
    m_lastMarker.clear();
    emit bridge() -> bookOpened(contentId);
}

void RecorderWatch::close()
{
    m_waitingForId = false;
    m_lastMarker.clear();
    if (m_open.isEmpty())
        return;
    const QString contentId = m_open;
    m_open.clear();
    emit bridge() -> bookClosed(contentId);
}

void RecorderWatch::onAboutToSleepDirect()
{
    m_asleep = true;
}

void RecorderWatch::onAboutToSleepQueued()
{
    emit bridge() -> aboutToSleep();
}

void RecorderWatch::onResumed()
{
    if (!m_asleep)
        return; // resumingFromSleep and resumedFromSleep: the first one counts.
    m_asleep = false;
    emit bridge() -> wokeUp();
}

void RecorderWatch::onUsbAboutToConnect()
{
    usbAboutToExport();
}

void RecorderWatch::onUsbDoneProcessing()
{
    if (!m_usb)
        return; // Also emitted after a rescan without USB ([ND]).
    m_usb = false;
    emit bridge() -> usbDisconnected();
}

void RecorderWatch::onReaderDestroyed()
{
    close();
}

void RecorderWatch::checkNativeReaderHidden()
{
    // Nickel's reader hidden while awake and not on USB: the book was left.
    if (m_reader && !m_readerIsKobalt && !m_reader->isVisible() && !m_asleep && !m_usb)
        close();
}

} // namespace kobo
} // namespace smalt

// --- Hook entry points ---

using namespace smalt::kobo;

extern "C" __attribute__((visibility("default"))) void
smalt_finish_initialization(void *application)
{
    if (rec::nextFinishInitialization)
        rec::nextFinishInitialization(application);
    RecorderWatch *watch = RecorderWatch::instance();
    watch->install();
    watch->setReady();
    emit KoboNickelBridge::instance() -> nickelReady();
}

extern "C" __attribute__((visibility("default"))) void
smalt_show_reader(void *mixin, const void *volume, const void *keys, void *library,
                  const void *bookmark)
{
    // Read the book before passing it on: the arguments are valid only
    // during this call, and Kobalt's hook may not return to Nickel's.
    QString contentId;
    if (rec::volumeDbValues && volume)
        contentId = rec::volumeDbValues(volume).value(QStringLiteral("ContentID")).toString();
    RecorderWatch::instance()->bookOpening(contentId);
    if (rec::nextShowReader)
        rec::nextShowReader(mixin, volume, keys, library, bookmark);
}

extern "C" __attribute__((visibility("default"))) void smalt_event_set_extra_data(void *event,
                                                                                  QVariantMap data)
{
    const QVariantMap seen = data; // Implicitly shared: no copy unless changed.
    if (rec::nextEventSetExtraData)
        rec::nextEventSetExtraData(event, std::move(data));
    observeEvent(event, seen);
}

extern "C" __attribute__((visibility("default"))) void smalt_power_off(void *manager,
                                                                       bool batteryLow)
{
    RecorderWatch::instance()->poweringOff();
    if (rec::nextPowerOff)
        rec::nextPowerOff(manager, batteryLow);
}

extern "C" __attribute__((visibility("default"))) void smalt_reboot(void *manager)
{
    RecorderWatch::instance()->poweringOff();
    if (rec::nextReboot)
        rec::nextReboot(manager);
}

extern "C" __attribute__((visibility("default"))) void smalt_go_gadget_device(void *plugManager)
{
    RecorderWatch::instance()->usbAboutToExport();
    if (rec::nextGoGadgetDevice)
        rec::nextGoGadgetDevice(plugManager);
}
