// KoboNickelBridge, Wi-Fi, battery and sleep-hold part. Owned by task 21; see
// bridge.h. Symbols and their sources: wifi_symbols.h.
//
// SPEC "Wi-Fi control": Wi-Fi goes through the same Nickel functions
// NickelDBus uses (NickelMenu's nickel_wifi action), so Nickel's status bar
// and settings stay consistent. Turning Wi-Fi on clears Nickel's Wi-Fi switch
// ("airplane mode") only if it was set, and turning it off sets it again only
// in that case; otherwise "off" drops the radio as Kobalt does
// (WirelessManager::off()). The sync engine turns Wi-Fi off only if it turned
// it on.
#include "bridge.h"
#include "wifi_symbols.h"
#include "wifi_watcher.h"

#include "smalt/log.h"

namespace smalt {
namespace kobo {
namespace {

const char kLog[] = "wifi";

// True when setWifiEnabled(true) cleared Nickel's Wi-Fi switch, so
// setWifiEnabled(false) puts the switch back instead of only dropping the radio.
bool g_clearedAirplaneMode = false;

Status unavailable(const char *what)
{
    return Status::error(
            StatusCode::Unavailable,
            QStringLiteral("%1: this firmware lacks the Nickel function").arg(QLatin1String(what)));
}

void *workflow()
{
    return wifi::WirelessWorkflowManager_sharedInstance
            ? wifi::WirelessWorkflowManager_sharedInstance()
            : nullptr;
}

void *wireless()
{
    return wifi::WirelessManager_sharedInstance ? wifi::WirelessManager_sharedInstance() : nullptr;
}

// Kobalt platforms/kobo/nickel-qt5/native_ui.cpp 135-170: Nickel's
// N3BatteryStatusLabel calls the hardware's getBatteryLevel through a vtable
// slot (0x84 on 4.38.23697, 0x90 on 4.46.23836). The slot is called only when
// it is one of the exported implementations, never the base class's.
int batteryFromHardware()
{
    using namespace wifi;
    if (!HardwareFactory_sharedInstance)
        return -1;
    void *device = HardwareFactory_sharedInstance();
    if (!device)
        return -1;
    typedef int (*LevelFn)(const void *);
    const LevelFn known[] = { MTK_getBatteryLevel, iMX508_getBatteryLevel,
                              AllWinner_getBatteryLevel };
    const int offsets[] = { 0x84, 0x90 };
    for (int offset : offsets) {
        const LevelFn level = reinterpret_cast<LevelFn>(
                (*static_cast<void ***>(device))[offset / sizeof(void *)]);
        if (!level)
            continue;
        for (LevelFn k : known) {
            if (k && level == k) {
                const int percent = level(device);
                return percent >= 0 && percent <= 100 ? percent : -1;
            }
        }
    }
    return -1;
}

} // namespace

int KoboNickelBridge::batteryPercent() const
{
    WifiWatcher::instance()->ensureConnected();
    const int direct = batteryFromHardware();
    return direct >= 0 ? direct : WifiWatcher::instance()->lastBatteryLevel();
}

bool KoboNickelBridge::wifiEnabled() const
{
    WifiWatcher::instance()->ensureConnected();
    // WirelessManager::up(): Nickel's cached radio state (Kobalt
    // native_sync_network.cpp 42-45, native_wifi.cpp 25).
    void *w = wireless();
    if (w && wifi::WirelessManager_up)
        return wifi::WirelessManager_up(w);
    return false;
}

bool KoboNickelBridge::networkConnected() const
{
    WifiWatcher::instance()->ensureConnected();
    return nickelNetworkConnected();
}

Status KoboNickelBridge::setWifiEnabled(bool enabled)
{
    WifiWatcher::instance()->ensureConnected();
    void *wf = workflow();
    if (enabled) {
        if (!wf || !wifi::WirelessWorkflowManager_isAirplaneMode
            || !wifi::WirelessWorkflowManager_setAirplaneMode)
            return unavailable("setWifiEnabled(true)");
        // NickelMenu nickel_wifi "enable": clear the switch if it is set.
        if (wifi::WirelessWorkflowManager_isAirplaneMode(wf)) {
            wifi::WirelessWorkflowManager_setAirplaneMode(wf, false);
            g_clearedAirplaneMode = true;
            log::info(kLog, QStringLiteral("Wi-Fi switched on"));
        }
        // The radio itself comes up with connectWifi().
        return Status::success();
    }
    if (g_clearedAirplaneMode && wf && wifi::WirelessWorkflowManager_setAirplaneMode) {
        // NickelMenu nickel_wifi "disable": put the user's switch back.
        g_clearedAirplaneMode = false;
        wifi::WirelessWorkflowManager_setAirplaneMode(wf, true);
        log::info(kLog, QStringLiteral("Wi-Fi switched off again"));
        return Status::success();
    }
    void *w = wireless();
    if (!w || !wifi::WirelessManager_off)
        return unavailable("setWifiEnabled(false)");
    // Kobalt native_sync_network.cpp 28-29 and 66-76: off() runs `killall
    // dhcpcd` and waits for it on this thread (about 87 ms on a Clara BW).
    wifi::WirelessManager_off(w);
    log::info(kLog, QStringLiteral("Wi-Fi radio off"));
    return Status::success();
}

Status KoboNickelBridge::connectWifi()
{
    WifiWatcher::instance()->ensureConnected();
    void *wf = workflow();
    if (!wf || !wifi::WirelessWorkflowManager_connectWirelessSilently)
        return unavailable("connectWifi");
    // NickelMenu nickel_wifi "autoconnect_silent": connect to a known network
    // without Nickel's dialogs. networkChanged() reports the result.
    wifi::WirelessWorkflowManager_connectWirelessSilently(wf);
    return Status::success();
}

Status KoboNickelBridge::holdSleep()
{
    if (!wifi::sleepHoldAvailable())
        return unavailable("holdSleep (PowerManager::canSuspend hook)");
    wifi::setSleepHeld(true);
    log::info(kLog, QStringLiteral("holding sleep for the pre-sleep sync"));
    return Status::success();
}

void KoboNickelBridge::releaseSleep()
{
    if (wifi::sleepHeld())
        log::info(kLog, QStringLiteral("sleep released"));
    wifi::setSleepHeld(false);
}

} // namespace kobo
} // namespace smalt
