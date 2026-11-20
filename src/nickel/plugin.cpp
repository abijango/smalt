// The NickelHook entry point for libsmalt.so.
//
// Scaffold version (task 00): the mod loads, writes one startup line to
// .adds/bookorbit/logs/smalt.log, and does nothing else. The hook and dlsym
// tables below are spliced from each task's hooks_*.h (task 20: recorder,
// task 21: Wi-Fi and sleep hold), so tasks never edit this file in parallel.
// The orchestrator wires the modules together here at integration.
//
// Uninstall (SPEC, Crash safety, "Uninstall"): NickelHook removes the mod
// when .adds/bookorbit/uninstall is missing at startup (uninstall_xflag).
// install.c seeds that marker on the first load after an install, before this
// check runs, so a fresh install never removes itself. The ledger, its
// backups and the logs stay on the device.
//
// Rules here: no exceptions across this boundary, nothing that blocks
// Nickel's startup, nothing that waits on Wi-Fi or the server.
#include <NickelHook.h>

#include "smalt/log.h"
#include "smalt/paths.h"
#include "smalt/version.h"

#include "hooks_recorder.h"
#include "hooks_wifi.h"

#include <QString>

namespace {

int smaltInit()
{
    const smalt::Paths paths = smalt::Paths::device();
    smalt::log::init(paths.logsDir());
    smalt::log::info("nickel", QStringLiteral("Smalt %1 loaded").arg(QLatin1String(SMALT_VERSION)));
    nh_log("Smalt %s loaded", SMALT_VERSION);
    return 0;
}

bool smaltUninstall()
{
    // NickelHook has already removed the library. Keep reading data: the
    // ledger, backups and logs are never deleted (CLAUDE.md hard rule 3).
    const smalt::Paths paths = smalt::Paths::device();
    smalt::log::init(paths.logsDir());
    smalt::log::info("nickel",
                     QStringLiteral("uninstalled: %1 was removed; ledger and backups kept")
                             .arg(paths.uninstallMarker()));
    smalt::log::flush();
    return true;
}

struct nh_info SmaltInfo = {
    .name = "Smalt",
    .desc = "BookOrbit companion: reading sessions, sync and library",
    .uninstall_flag = nullptr,
    .uninstall_xflag = "/mnt/onboard/.adds/bookorbit/uninstall",
    .failsafe_delay = 10,
};

// clang-format off
struct nh_hook SmaltHook[] = {
    SMALT_RECORDER_HOOKS
    SMALT_WIFI_HOOKS
    { nullptr, nullptr, nullptr, nullptr, nullptr, false },
};

struct nh_dlsym SmaltDlsym[] = {
    SMALT_RECORDER_DLSYMS
    SMALT_WIFI_DLSYMS
    { nullptr, nullptr, nullptr, false },
};
// clang-format on

} // namespace

// The NickelHook() macro: formatting stays off to the end of the file.
// clang-format off
NickelHook(
    .init = &smaltInit,
    .info = &SmaltInfo,
    .hook = SmaltHook,
    .dlsym = SmaltDlsym,
    .uninstall = &smaltUninstall,
)
