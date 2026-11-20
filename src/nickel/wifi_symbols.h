// Nickel functions for Wi-Fi, battery and the sleep hold. Task 21; Kobo only.
//
// Every symbol and signature is taken from source that calls it on firmware
// 4.x (SPEC "Wi-Fi control": the same Nickel functions NickelDBus uses):
//
//   NickelMenu  pgaskin/NickelMenu @ bc799ebe95a413a4857aa97a88e2b16557e6427f
//               src/action_cc.cc 640-686, NM_ACTION_(nickel_wifi), which
//               NickelDBus calls for its Wi-Fi methods
//   NickelDBus  shermp/NickelDBus @ ab7ac0c7fee308b776a1359bb90898a21edfb54d
//               src/ndb/NDBDbus.cc 103 and 163-175 (WirelessManager and its
//               signals), 805-841 (wfmConnectWireless*, ndbWireless ->
//               nm_action_nickel_wifi)
//   Kobalt      nicoverbruggen/kobalt @ be47258850ffdc3295257475afdf8b8f6cb53176
//               platforms/kobo/common/native_sync_network.cpp 14-29,
//               common/native_wifi.cpp 20-30, common/native_network_state.h
//               8-22, nickel-qt5/hooks.cpp 209-214, common/native_lifecycle.cpp
//               267-270 and 437-450, common/reader_coordinator.cpp 1797-1801,
//               nickel-qt5/native_ui.cpp 135-170, common/native_battery.cpp 16-30
//
// All are resolved by NickelHook as optional entries (hooks_wifi.h): a
// missing symbol leaves its feature off and never stops the mod loading.
// Without Wi-Fi control, sync runs only when Wi-Fi is already on; without the
// canSuspend hook, holdSleep() reports Unavailable and the pre-sleep sync runs
// only until the device sleeps.
#pragma once

namespace smalt {
namespace kobo {
namespace wifi {

// --- WirelessWorkflowManager (NickelMenu action_cc.cc 641-670; Kobalt
// native_sync_network.cpp 14-27) ---
// _ZN23WirelessWorkflowManager14sharedInstanceEv
extern void *(*WirelessWorkflowManager_sharedInstance)();
// _ZN23WirelessWorkflowManager14isAirplaneModeEv: true when Wi-Fi is switched off.
extern bool (*WirelessWorkflowManager_isAirplaneMode)(void *);
// _ZN23WirelessWorkflowManager15setAirplaneModeEb (NickelMenu "enable"/"disable").
extern void (*WirelessWorkflowManager_setAirplaneMode)(void *, bool);
// _ZN23WirelessWorkflowManager23connectWirelessSilentlyEv (NickelMenu
// "autoconnect_silent"; Kobalt's background connect).
extern void (*WirelessWorkflowManager_connectWirelessSilently)(void *);
// _ZN23WirelessWorkflowManager20isInternetAccessibleEv (Kobalt).
extern bool (*WirelessWorkflowManager_isInternetAccessible)(void *);

// --- WirelessManager (NickelDBus NDBDbus.cc 103; Kobalt native_wifi.cpp 20-22,
// native_sync_network.cpp 15-16 and 29) ---
// _ZN15WirelessManager14sharedInstanceEv
extern void *(*WirelessManager_sharedInstance)();
// _ZNK15WirelessManager2upEv: Nickel's cached radio state ("radio on").
extern bool (*WirelessManager_up)(void *);
// _ZN15WirelessManager3offEv: turns the radio off ("present in firmware 4.46").
extern void (*WirelessManager_off)(void *);

// --- Battery (Kobalt nickel-qt5/native_ui.cpp 135-170) ---
// _ZN15HardwareFactory14sharedInstanceEv
extern void *(*HardwareFactory_sharedInstance)();
// The exported getBatteryLevel implementations Kobalt checks the vtable slot
// against before calling it.
extern int (*MTK_getBatteryLevel)(const void *); // _ZNK3MTK15getBatteryLevelEv
extern int (*iMX508_getBatteryLevel)(const void *); // _ZNK6iMX50815getBatteryLevelEv
extern int (*AllWinner_getBatteryLevel)(const void *); // _ZNK9AllWinner15getBatteryLevelEv

// --- Sleep hold (Kobalt nickel-qt5/hooks.cpp 209-214) ---
// _ZN12PowerManager10canSuspendEv, hooked: the previous hook in the chain
// (Kobalt's, when present) or Nickel's own function.
extern bool (*PowerManager_canSuspend)(void *);

// The hold flag smalt_can_suspend() reads.
void setSleepHeld(bool held);
bool sleepHeld();
bool sleepHoldAvailable();

} // namespace wifi
} // namespace kobo
} // namespace smalt

// Replaces PowerManager::canSuspend() for calls from libnickel: false while
// the pre-sleep sync holds sleep (always under a 15 s SleepWatchdog), else
// whatever the next function in the chain answers.
extern "C" __attribute__((visibility("default"))) bool smalt_can_suspend(void *manager);
