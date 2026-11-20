// NickelHook entries for task 20's part of the bridge. Owned by task 20; see
// bridge.h. plugin.cpp splices these into NickelHook's tables, so each macro
// expands to zero or more complete, comma-terminated entries.
//
// Every entry is optional: a missing symbol leaves that signal off and never
// stops the mod from loading (bridge.h rules). Every symbol and signature is
// taken from source that uses it on firmware 4.x (Qt 5); the citations are in
// recorder_hooks.cpp. Kobalt hooks several of the same symbols; NickelHook
// chains them (nh_dlhook returns the previous hook), and each hook here calls
// the next one first, so Kobalt's work (its pre-sleep and close writes) is
// done before Smalt looks.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace smalt {
namespace kobo {
namespace rec {

// --- Next in the hook chain (set by NickelHook) ---
extern void (*nextFinishInitialization)(void *application);
extern void (*nextShowReader)(void *mixin, const void *volume, const void *keys, void *library,
                              const void *bookmark);
extern void (*nextEventSetExtraData)(void *event, QVariantMap data);
extern void (*nextPowerOff)(void *manager, bool batteryLow);
extern void (*nextReboot)(void *manager);
extern void (*nextGoGadgetDevice)(void *plugManager);

// --- Resolved with dlsym (null when this firmware doesn't have them) ---
extern const int &(*eventType)(const void *event);
extern QString (*eventContentId)(const void *event);
extern QVariantMap (*volumeDbValues)(const void *volume);
extern QObject *(*powerWorkflowManager)();
extern QObject *(*plugWorkflowManager)();
extern void *(*currentDevice)();
extern bool (*deviceIsSpaKoboBW)(const void *device);
extern bool (*deviceIsSpaKobo)(const void *device);
extern bool (*deviceIsMonzaKobo)(const void *device);

} // namespace rec
} // namespace kobo
} // namespace smalt

extern "C" {
__attribute__((visibility("default"))) void smalt_finish_initialization(void *application);
__attribute__((visibility("default"))) void smalt_show_reader(void *mixin, const void *volume,
                                                              const void *keys, void *library,
                                                              const void *bookmark);
__attribute__((visibility("default"))) void smalt_event_set_extra_data(void *event,
                                                                       QVariantMap data);
__attribute__((visibility("default"))) void smalt_power_off(void *manager, bool batteryLow);
__attribute__((visibility("default"))) void smalt_reboot(void *manager);
__attribute__((visibility("default"))) void smalt_go_gadget_device(void *plugManager);
}

// clang-format off
#define SMALT_RECORDER_HOOKS \
    { "_ZN18Nickel3Application20finishInitializationEv", "smalt_finish_initialization", \
      "libnickel.so.1.0.0", nh_symoutptr(smalt::kobo::rec::nextFinishInitialization), \
      "Recorder: Nickel finished starting (nickelReady)", true }, \
    { "_ZN24AbstractReadingViewMixin10showReaderERK6VolumeRK7QVectorI10ContentKeyEP25AbstractLibraryControllerIS0_ERK8Bookmark", \
      "smalt_show_reader", "libnickel.so.1.0.0", nh_symoutptr(smalt::kobo::rec::nextShowReader), \
      "Recorder: which book is opening (bookOpened)", true }, \
    { "_ZN5Event12setExtraDataE4QMapI7QString8QVariantE", "smalt_event_set_extra_data", \
      "libnickel.so.1.0.0", nh_symoutptr(smalt::kobo::rec::nextEventSetExtraData), \
      "Recorder: page turns in Nickel's event engine (pageTurned)", true }, \
    { "_ZN22N3PowerWorkflowManager8powerOffEb", "smalt_power_off", "libnickel.so.1.0.0", \
      nh_symoutptr(smalt::kobo::rec::nextPowerOff), "Recorder: power-off (aboutToPowerOff)", true }, \
    { "_ZN22N3PowerWorkflowManager6rebootEv", "smalt_reboot", "libnickel.so.1.0.0", \
      nh_symoutptr(smalt::kobo::rec::nextReboot), "Recorder: reboot (aboutToPowerOff)", true }, \
    { "_ZN11PlugManager16goGoGadgetDeviceEv", "smalt_go_gadget_device", "libnickel.so.1.0.0", \
      nh_symoutptr(smalt::kobo::rec::nextGoGadgetDevice), \
      "Recorder: close the ledger before USB export (usbConnected)", true },

#define SMALT_RECORDER_DLSYMS \
    { "_ZNK5Event4typeEv", nh_symoutptr(smalt::kobo::rec::eventType), \
      "Recorder: event type (46 = page turns)", true }, \
    { "_ZNK5Event9contentIdEv", nh_symoutptr(smalt::kobo::rec::eventContentId), \
      "Recorder: event's book", true }, \
    { "_ZNK6Volume11getDbValuesEv", nh_symoutptr(smalt::kobo::rec::volumeDbValues), \
      "Recorder: opening book's ContentID", true }, \
    { "_ZN22N3PowerWorkflowManager14sharedInstanceEv", \
      nh_symoutptr(smalt::kobo::rec::powerWorkflowManager), "Recorder: sleep and wake signals", true }, \
    { "_ZN19PlugWorkflowManager14sharedInstanceEv", \
      nh_symoutptr(smalt::kobo::rec::plugWorkflowManager), "Recorder: USB signals", true }, \
    { "_ZN6Device16getCurrentDeviceEv", nh_symoutptr(smalt::kobo::rec::currentDevice), \
      "Recorder: device model", true }, \
    { "_ZNK6Device11isSpaKoboBWEv", nh_symoutptr(smalt::kobo::rec::deviceIsSpaKoboBW), \
      "Recorder: Clara BW", true }, \
    { "_ZNK6Device9isSpaKoboEv", nh_symoutptr(smalt::kobo::rec::deviceIsSpaKobo), \
      "Recorder: Clara Colour", true }, \
    { "_ZNK6Device11isMonzaKoboEv", nh_symoutptr(smalt::kobo::rec::deviceIsMonzaKobo), \
      "Recorder: Libra Colour", true },
// clang-format on
