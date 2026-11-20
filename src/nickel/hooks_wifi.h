// NickelHook entries for task 21's part of the bridge (Wi-Fi, battery and the
// sleep hold). plugin.cpp splices these into NickelHook's tables. Every entry
// is optional: a missing symbol leaves its feature off and never stops the
// mod from loading. Sources for each symbol: wifi_symbols.h.
#pragma once

#include "wifi_symbols.h"

// clang-format off
#define SMALT_WIFI_HOOKS \
    { "_ZN12PowerManager10canSuspendEv", "smalt_can_suspend", "libnickel.so.1.0.0", \
      nh_symoutptr(smalt::kobo::wifi::PowerManager_canSuspend), \
      "Hold sleep for the pre-sleep sync (15 s watchdog)", true },

#define SMALT_WIFI_DLSYMS \
    { "_ZN23WirelessWorkflowManager14sharedInstanceEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessWorkflowManager_sharedInstance), "Wi-Fi control", true }, \
    { "_ZN23WirelessWorkflowManager14isAirplaneModeEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessWorkflowManager_isAirplaneMode), "Wi-Fi switch state", true }, \
    { "_ZN23WirelessWorkflowManager15setAirplaneModeEb", \
      nh_symoutptr(smalt::kobo::wifi::WirelessWorkflowManager_setAirplaneMode), "Wi-Fi switch", true }, \
    { "_ZN23WirelessWorkflowManager23connectWirelessSilentlyEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessWorkflowManager_connectWirelessSilently), "Wi-Fi connect", true }, \
    { "_ZN23WirelessWorkflowManager20isInternetAccessibleEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessWorkflowManager_isInternetAccessible), "network state", true }, \
    { "_ZN15WirelessManager14sharedInstanceEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessManager_sharedInstance), "Wi-Fi radio", true }, \
    { "_ZNK15WirelessManager2upEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessManager_up), "Wi-Fi radio state", true }, \
    { "_ZN15WirelessManager3offEv", \
      nh_symoutptr(smalt::kobo::wifi::WirelessManager_off), "Wi-Fi radio off", true }, \
    { "_ZN15HardwareFactory14sharedInstanceEv", \
      nh_symoutptr(smalt::kobo::wifi::HardwareFactory_sharedInstance), "battery", true }, \
    { "_ZNK3MTK15getBatteryLevelEv", \
      nh_symoutptr(smalt::kobo::wifi::MTK_getBatteryLevel), "battery (MTK)", true }, \
    { "_ZNK6iMX50815getBatteryLevelEv", \
      nh_symoutptr(smalt::kobo::wifi::iMX508_getBatteryLevel), "battery (i.MX)", true }, \
    { "_ZNK9AllWinner15getBatteryLevelEv", \
      nh_symoutptr(smalt::kobo::wifi::AllWinner_getBatteryLevel), "battery (AllWinner)", true },
// clang-format on
