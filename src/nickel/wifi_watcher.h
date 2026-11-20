// Watches Nickel's Wi-Fi and battery signals and turns them into the
// bridge's networkChanged(bool). Task 21; Kobo only.
//
// Signals connected (by name, so a firmware without them leaves the watcher
// quiet): WirelessManager networkConnected(), networkDisconnected() and
// wifiEnabled(bool) (NickelDBus NDBDbus.cc 165-173; Kobalt native_wifi.cpp
// 27-30), WirelessWorkflowManager internetIsAccessible() (Kobalt
// native_sync_network.cpp 99), HardwareFactory battery_level(int) (Kobalt
// native_battery.cpp 22-23).
#pragma once

#include <QObject>
#include <QPointer>

namespace smalt {
namespace kobo {

class WifiWatcher : public QObject
{
    Q_OBJECT

public:
    // Created once, on Nickel's UI thread, after Nickel is ready.
    static WifiWatcher *instance();

    // Connects to Nickel's objects if that hasn't worked yet. Safe to call often.
    void ensureConnected();

    // The last battery level Nickel reported through battery_level(int); -1 if none.
    int lastBatteryLevel() const { return m_battery; }

private slots:
    void onNetworkMaybeChanged();
    void onWifiEnabled(bool enabled);
    void onBatteryLevel(int level);

private:
    WifiWatcher() = default;

    QPointer<QObject> m_wireless;
    QPointer<QObject> m_workflow;
    QPointer<QObject> m_hardware;
    int m_lastConnected = -1; // -1 unknown, 0/1.
    int m_battery = -1;
};

// True when Nickel's radio is up and a network is usable (Kobalt's test:
// WirelessManager::up() and isInternetAccessible(), or a usable local IPv4
// address for a LAN-only server; native_sync_network.cpp 42-53).
bool nickelNetworkConnected();

} // namespace kobo
} // namespace smalt
