// Nickel symbols for Wi-Fi, battery and the sleep hold. Task 21; see
// wifi_symbols.h for where each one comes from.
#include "wifi_symbols.h"

#include <atomic>

namespace smalt {
namespace kobo {
namespace wifi {

void *(*WirelessWorkflowManager_sharedInstance)() = nullptr;
bool (*WirelessWorkflowManager_isAirplaneMode)(void *) = nullptr;
void (*WirelessWorkflowManager_setAirplaneMode)(void *, bool) = nullptr;
void (*WirelessWorkflowManager_connectWirelessSilently)(void *) = nullptr;
bool (*WirelessWorkflowManager_isInternetAccessible)(void *) = nullptr;

void *(*WirelessManager_sharedInstance)() = nullptr;
bool (*WirelessManager_up)(void *) = nullptr;
void (*WirelessManager_off)(void *) = nullptr;

void *(*HardwareFactory_sharedInstance)() = nullptr;
int (*MTK_getBatteryLevel)(const void *) = nullptr;
int (*iMX508_getBatteryLevel)(const void *) = nullptr;
int (*AllWinner_getBatteryLevel)(const void *) = nullptr;

bool (*PowerManager_canSuspend)(void *) = nullptr;

namespace {
std::atomic<bool> g_held(false);
} // namespace

void setSleepHeld(bool held)
{
    g_held.store(held);
}

bool sleepHeld()
{
    return g_held.load();
}

bool sleepHoldAvailable()
{
    // NickelHook sets the original only when the hook was installed.
    return PowerManager_canSuspend != nullptr;
}

} // namespace wifi
} // namespace kobo
} // namespace smalt

extern "C" __attribute__((visibility("default"))) bool smalt_can_suspend(void *manager)
{
    using namespace smalt::kobo::wifi;
    // Ask the rest of the chain first, as Kobalt's kobalt_can_suspend() does
    // (native_lifecycle.cpp 267-270), so Kobalt's own reasons still apply.
    const bool others = PowerManager_canSuspend ? PowerManager_canSuspend(manager) : true;
    // Nickel asks canSuspend before each standby, so a refusal keeps the
    // device awake (Kobalt reader_coordinator.cpp 1797-1801). The hold is
    // always paired with the 15 s SleepWatchdog in the sync engine.
    return others && !sleepHeld();
}
