// KoboNickelBridge, Wi-Fi and sleep-hold part. Owned by task 21; see bridge.h.
// Scaffold placeholder (task 00): reports Wi-Fi off and refuses control until
// task 21 resolves Nickel's symbols and installs the hooks in hooks_wifi.h.
#include "bridge.h"

namespace smalt {
namespace kobo {

int KoboNickelBridge::batteryPercent() const
{
    return -1;
}

bool KoboNickelBridge::wifiEnabled() const
{
    return false;
}

bool KoboNickelBridge::networkConnected() const
{
    return false;
}

Status KoboNickelBridge::setWifiEnabled(bool)
{
    return Status::error(StatusCode::NotImplemented, QStringLiteral("setWifiEnabled (task 21)"));
}

Status KoboNickelBridge::connectWifi()
{
    return Status::error(StatusCode::NotImplemented, QStringLiteral("connectWifi (task 21)"));
}

Status KoboNickelBridge::holdSleep()
{
    return Status::error(StatusCode::NotImplemented, QStringLiteral("holdSleep (task 21)"));
}

void KoboNickelBridge::releaseSleep() { }

} // namespace kobo
} // namespace smalt
