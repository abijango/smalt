// Task 21; see wifi_watcher.h.
#include "wifi_watcher.h"

#include "bridge.h"
#include "wifi_symbols.h"

#include "smalt/log.h"

#include <QHostAddress>
#include <QNetworkInterface>

namespace smalt {
namespace kobo {
namespace {

const char kLog[] = "wifi";

// Kobalt platforms/kobo/common/native_network_state.h 8-22: an interface
// that is up, running, not loopback, with an IPv4 address outside 169.254/16.
bool usableLocalNetwork()
{
    const QList<QNetworkInterface> all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : all) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isNull()
                && ip != QHostAddress(QHostAddress::LocalHost)
                && (ip.toIPv4Address() & 0xffff0000u) != 0xa9fe0000u)
                return true;
        }
    }
    return false;
}

} // namespace

bool nickelNetworkConnected()
{
    using namespace wifi;
    if (!WirelessManager_sharedInstance || !WirelessManager_up)
        return false;
    void *wireless = WirelessManager_sharedInstance();
    if (!wireless || !WirelessManager_up(wireless))
        return false;
    if (WirelessWorkflowManager_sharedInstance && WirelessWorkflowManager_isInternetAccessible) {
        void *workflow = WirelessWorkflowManager_sharedInstance();
        if (workflow && WirelessWorkflowManager_isInternetAccessible(workflow))
            return true;
    }
    // A BookOrbit server on the LAN can be reachable when Nickel's public
    // internet check fails; the HTTPS request decides.
    return usableLocalNetwork();
}

WifiWatcher *WifiWatcher::instance()
{
    // Never destroyed: Nickel exits without unloading the mod.
    static WifiWatcher *watcher = new WifiWatcher;
    return watcher;
}

void WifiWatcher::ensureConnected()
{
    using namespace wifi;
    if (!m_wireless && WirelessManager_sharedInstance) {
        QObject *o = static_cast<QObject *>(WirelessManager_sharedInstance());
        if (o) {
            m_wireless = o;
            connect(o, SIGNAL(networkConnected()), this, SLOT(onNetworkMaybeChanged()));
            connect(o, SIGNAL(networkDisconnected()), this, SLOT(onNetworkMaybeChanged()));
            connect(o, SIGNAL(wifiEnabled(bool)), this, SLOT(onWifiEnabled(bool)));
            log::info(kLog, QStringLiteral("watching WirelessManager"));
        }
    }
    if (!m_workflow && WirelessWorkflowManager_sharedInstance) {
        QObject *o = static_cast<QObject *>(WirelessWorkflowManager_sharedInstance());
        if (o) {
            m_workflow = o;
            connect(o, SIGNAL(internetIsAccessible()), this, SLOT(onNetworkMaybeChanged()));
        }
    }
    if (!m_hardware && HardwareFactory_sharedInstance) {
        QObject *o = static_cast<QObject *>(HardwareFactory_sharedInstance());
        if (o) {
            m_hardware = o;
            connect(o, SIGNAL(battery_level(int)), this, SLOT(onBatteryLevel(int)));
        }
    }
    if (m_lastConnected < 0 && m_wireless)
        m_lastConnected = nickelNetworkConnected() ? 1 : 0;
}

void WifiWatcher::onNetworkMaybeChanged()
{
    const int now = nickelNetworkConnected() ? 1 : 0;
    if (now == m_lastConnected)
        return;
    m_lastConnected = now;
    log::info(kLog, now ? QStringLiteral("network connected") : QStringLiteral("network lost"));
    KoboNickelBridge *bridge = KoboNickelBridge::instance();
    emit bridge->networkChanged(now == 1);
}

void WifiWatcher::onWifiEnabled(bool)
{
    onNetworkMaybeChanged();
}

void WifiWatcher::onBatteryLevel(int level)
{
    if (level >= 0 && level <= 100)
        m_battery = level;
}

} // namespace kobo
} // namespace smalt
