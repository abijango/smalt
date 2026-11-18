// User settings (SPEC, "Settings screen"), apart from the BookOrbit login.
//
// The login (server URL, KOReader username and password) stays in
// Paths::configIni(), read by the BookOrbit client (task 11). Everything else
// the Settings screen changes lives in Paths::settingsIni(), so saving a
// setting never rewrites the file that holds the password.
//
// Implemented by task 00 in src/common/settings.cpp. Read by the recorder
// (20), the sync engine (21) and the screens (22), which also write it.
#pragma once

#include "smalt/ledger.h"
#include "smalt/paths.h"
#include "smalt/status.h"
#include "smalt/sync.h"

#include <QObject>
#include <QString>

#include <memory>

namespace smalt {

// "New session after sleep" (SPEC, "Sleeping inside a book"). Never = -1:
// waking always continues the open session.
constexpr int kNewSessionAfterSleepNever = -1;
constexpr int kNewSessionAfterSleepChoicesSeconds[6] = { 15 * 60,   60 * 60,
                                                         4 * 3600,  6 * 3600,
                                                         12 * 3600, kNewSessionAfterSleepNever };

struct Settings
{
    // Recording.
    int newSessionAfterSleepSeconds = kDefaultNewSessionAfterSleepSeconds;

    // Sync triggers (SPEC, "Triggers"): a switch for each.
    bool syncOnBookClose = true;
    bool syncBeforeSleep = true;
    bool syncOnWake = true;
    bool syncWhenWifiOn = true;
    int preSleepMinBatteryPercent = kPreSleepMinBatteryPercent;

    // Push while reading.
    LivePushMode livePushMode = kDefaultLivePushMode;
    // Interval in minutes, one of kLivePushChoicesMinutes. Changing the mode
    // in Settings sets the mode's default (5 for OnlyWhenConnected, 15 for
    // ConnectBriefly); the user can then pick another.
    int livePushMinutes = kDefaultLivePushMinutes;

    // Collections (SPEC, "Collections", "Status and ratings").
    bool mirrorCollections = true;
    bool abandonedCollection = false;
    bool wantToReadCollection = false;

    // Downloads (SPEC, "Downloads"). Empty = Paths::downloadsDir().
    QString downloadFolder;
};

class SettingsStore : public QObject
{
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr) : QObject(parent) { }

    ~SettingsStore() override = default;

    // Reads Paths::settingsIni(). A missing file gives the defaults (Ok); an
    // unreadable file gives the defaults and IoError; out-of-range values are
    // replaced by their defaults and logged. Never fails the caller otherwise.
    virtual Status load() = 0;

    virtual Settings current() const = 0;

    // Validates (out-of-range values refused with InvalidArgument, nothing
    // written), writes the file atomically, then emits changed(). The file is
    // a few hundred bytes; callers on the UI thread may call it from a tap.
    virtual Status save(const Settings &settings) = 0;

signals:
    void changed();
};

std::unique_ptr<SettingsStore> createSettingsStore(const Paths &paths);

} // namespace smalt
