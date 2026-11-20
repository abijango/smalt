// The composition root inside Nickel. See app.h. Task 00.
#include "app.h"

#include "bridge.h"
#include "wifi_watcher.h"

#include "bookorbit/client_config.h"
#include "smalt/bookorbit_client.h"
#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/outbox.h"
#include "smalt/paths.h"
#include "smalt/recorder.h"
#include "smalt/safety.h"
#include "smalt/settings.h"
#include "smalt/sync.h"
#include "smalt/version.h"

#include <QCoreApplication>
#include <QObject>

#include <memory>

namespace smalt {
namespace kobo {
namespace {

// Everything lives for the whole Nickel process; nothing is torn down, because
// Nickel exits without unloading the mod.
struct App
{
    Paths paths = Paths::device();
    std::unique_ptr<Safety> safety;
    StartupDecision decision;
    bool started = false;

    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<Ledger> ledger;
    std::unique_ptr<Outbox> outbox;
    std::unique_ptr<Recorder> recorder;
    std::unique_ptr<BookOrbitClient> client;
    SyncEngine *sync = nullptr; // Parented to the bridge.
};

App &app()
{
    static App *instance = new App;
    return *instance;
}

void clearMarker(MarkerClearReason reason)
{
    App &a = app();
    if (a.safety && a.decision.modActive)
        a.safety->clearStartupMarker(reason);
}

void startSync(App &a)
{
    if (!a.decision.syncAllowed) {
        log::info("app", QStringLiteral("sync off for this boot (disable-sync or recovery)"));
        return;
    }
    ClientConfig config;
    const Status read = readClientConfig(a.paths.configIni(), &config);
    if (!read.ok()) {
        log::info(
                "app",
                QStringLiteral("no usable BookOrbit login (%1); sync stays off").arg(read.message));
        return;
    }
    config.deviceId = a.ledger->deviceId();
    config.deviceModel = deviceProfile(KoboNickelBridge::instance()->deviceModel()).name;
    config.pluginVersion = QStringLiteral("smalt-") + QLatin1String(SMALT_VERSION);
    config.caBundlePath = a.paths.caBundle();
    a.client = createBookOrbitClient(config);
    a.sync = createSyncEngine(a.ledger.get(), a.outbox.get(), a.client.get(),
                              KoboNickelBridge::instance(), a.safety.get(), a.recorder.get(),
                              a.settings.get(), KoboNickelBridge::instance());
    WifiWatcher::instance()->ensureConnected();
    log::info("app", QStringLiteral("sync engine started"));
}

// On Nickel's UI thread, once Nickel is ready. Runs once.
void start()
{
    App &a = app();
    if (a.started || !a.decision.modActive)
        return;
    a.started = true;

    a.settings = createSettingsStore(a.paths);
    const Status settingsLoaded = a.settings->load();
    if (!settingsLoaded.ok())
        log::warn("app",
                  QStringLiteral("settings: %1; using defaults").arg(settingsLoaded.message));

    a.ledger = createLedger(a.paths);
    const Status opened = a.ledger->open();
    if (!opened.ok()) {
        // Without a ledger nothing can be recorded; Nickel carries on as usual.
        log::error("app", QStringLiteral("ledger didn't open: %1").arg(opened.message));
        clearMarker(MarkerClearReason::FallbackToNickelHome);
        return;
    }
    const Status backup = a.ledger->backupIfDue(QDateTime::currentDateTimeUtc());
    if (!backup.ok())
        log::warn("app", QStringLiteral("daily backup: %1").arg(backup.message));
    a.outbox = createOutbox(a.ledger.get());

    if (a.decision.recordingAllowed) {
        a.recorder = createRecorder(a.ledger.get(), KoboNickelBridge::instance(), a.settings.get());
        a.recorder->start();
        log::info("app", QStringLiteral("recorder started"));
    }
    if (a.recorder)
        startSync(a);

    // No home takeover yet (task 30): startup ends with Nickel's own home.
    a.safety->markReady();
    clearMarker(MarkerClearReason::FallbackToNickelHome);
}

void onPowerOff()
{
    App &a = app();
    if (a.recorder)
        a.recorder->stop();
    clearMarker(MarkerClearReason::PowerOff);
}

} // namespace

int appInit()
{
    App &a = app();
    log::init(a.paths.logsDir());
    log::info("nickel", QStringLiteral("Smalt %1 loaded").arg(QLatin1String(SMALT_VERSION)));

    a.safety = createSafety(a.paths);
    a.decision = a.safety->evaluateStartup();
    if (!a.decision.modActive) {
        log::info("app",
                  a.decision.recoveryTriggered
                          ? QStringLiteral("two startups in a row didn't finish: "
                                           "disabled, see RECOVERY.txt")
                          : QStringLiteral("disabled switch is on: staying inactive"));
        return 0; // Load, but do nothing: hooks pass straight through.
    }
    // Covers the whole startup (src/safety: the marker is written with
    // disable-home too). Cleared in start() or on fallback.
    const Status marker = a.safety->writeStartupMarker();
    if (!marker.ok())
        log::warn("app", QStringLiteral("startup marker: %1").arg(marker.message));

    KoboNickelBridge *bridge = KoboNickelBridge::instance();
    QObject::connect(bridge, &NickelBridge::nickelReady, bridge, &start);
    QObject::connect(bridge, &NickelBridge::aboutToPowerOff, bridge, &onPowerOff);
    // If Nickel was already ready when the mod loaded (the init hook can't
    // fire again), start from the event loop once it runs.
    if (QCoreApplication::instance() && bridge->isReady())
        QMetaObject::invokeMethod(bridge, "nickelReady", Qt::QueuedConnection);
    return 0;
}

bool appUninstall()
{
    const Paths paths = Paths::device();
    log::init(paths.logsDir());
    const Status cleaned = cleanupAfterUninstall(paths);
    log::info("nickel",
              QStringLiteral("uninstalled: %1 was removed; ledger and backups kept%2")
                      .arg(paths.uninstallMarker(),
                           cleaned.ok() ? QString()
                                        : QStringLiteral(" (cleanup: %1)").arg(cleaned.message)));
    log::flush();
    return true;
}

} // namespace kobo
} // namespace smalt
