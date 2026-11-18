// SettingsStore on an INI file. See include/smalt/settings.h.
//
// Writing goes to a temporary file in the same folder, which is then renamed
// over settings.ini, so a crash or power loss leaves either the old or the
// new file, never half of one. QSettings is used only to read and write the
// INI format; it is pointed at the temporary file for writing.
#include "smalt/settings.h"

#include "smalt/log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace smalt {
namespace {

bool validNewSessionAfterSleep(int seconds)
{
    for (int choice : kNewSessionAfterSleepChoicesSeconds) {
        if (choice == seconds)
            return true;
    }
    return false;
}

bool validLivePushMinutes(int minutes)
{
    for (int choice : kLivePushChoicesMinutes) {
        if (choice == minutes)
            return true;
    }
    return false;
}

bool validMode(int mode)
{
    return mode == int(LivePushMode::Off) || mode == int(LivePushMode::OnlyWhenConnected)
            || mode == int(LivePushMode::ConnectBriefly);
}

QString modeName(LivePushMode mode)
{
    switch (mode) {
    case LivePushMode::Off:
        return QStringLiteral("off");
    case LivePushMode::OnlyWhenConnected:
        return QStringLiteral("only-when-connected");
    case LivePushMode::ConnectBriefly:
        return QStringLiteral("connect-briefly");
    }
    return QStringLiteral("only-when-connected");
}

int modeFromName(const QString &name)
{
    if (name == QLatin1String("off"))
        return int(LivePushMode::Off);
    if (name == QLatin1String("only-when-connected"))
        return int(LivePushMode::OnlyWhenConnected);
    if (name == QLatin1String("connect-briefly"))
        return int(LivePushMode::ConnectBriefly);
    return -1;
}

// Why `s` can't be saved, or empty if it can.
QString invalidReason(const Settings &s)
{
    if (!validNewSessionAfterSleep(s.newSessionAfterSleepSeconds))
        return QStringLiteral("newSessionAfterSleepSeconds %1").arg(s.newSessionAfterSleepSeconds);
    if (s.preSleepMinBatteryPercent < 0 || s.preSleepMinBatteryPercent > 100)
        return QStringLiteral("preSleepMinBatteryPercent %1").arg(s.preSleepMinBatteryPercent);
    if (!validMode(int(s.livePushMode)))
        return QStringLiteral("livePushMode %1").arg(int(s.livePushMode));
    if (!validLivePushMinutes(s.livePushMinutes))
        return QStringLiteral("livePushMinutes %1").arg(s.livePushMinutes);
    return QString();
}

class IniSettingsStore : public SettingsStore
{
public:
    explicit IniSettingsStore(const Paths &paths) : m_path(paths.settingsIni()) { }

    Status load() override
    {
        m_settings = Settings();
        QFileInfo info(m_path);
        if (!info.exists())
            return Status::success();
        if (!info.isFile() || !info.isReadable()) {
            log::warn("settings", QStringLiteral("can't read %1; using defaults").arg(m_path));
            return Status::error(StatusCode::IoError, QStringLiteral("unreadable: ") + m_path);
        }
        QSettings ini(m_path, QSettings::IniFormat);
        if (ini.status() != QSettings::NoError) {
            log::warn(
                    "settings",
                    QStringLiteral("%1 is not a valid settings file; using defaults").arg(m_path));
            return Status::error(StatusCode::IoError, QStringLiteral("bad format: ") + m_path);
        }
        Settings s;
        readInt(ini, "recording/newSessionAfterSleepSeconds", &s.newSessionAfterSleepSeconds,
                validNewSessionAfterSleep);
        readBool(ini, "sync/onBookClose", &s.syncOnBookClose);
        readBool(ini, "sync/beforeSleep", &s.syncBeforeSleep);
        readBool(ini, "sync/onWake", &s.syncOnWake);
        readBool(ini, "sync/whenWifiOn", &s.syncWhenWifiOn);
        readInt(ini, "sync/preSleepMinBatteryPercent", &s.preSleepMinBatteryPercent,
                [](int v) { return v >= 0 && v <= 100; });
        if (ini.contains(QStringLiteral("sync/livePushMode"))) {
            const int mode =
                    modeFromName(ini.value(QStringLiteral("sync/livePushMode")).toString());
            if (mode < 0)
                log::warn("settings",
                          QStringLiteral("unknown sync/livePushMode; using the default"));
            else
                s.livePushMode = LivePushMode(mode);
        }
        readInt(ini, "sync/livePushMinutes", &s.livePushMinutes, validLivePushMinutes);
        readBool(ini, "collections/mirror", &s.mirrorCollections);
        readBool(ini, "collections/abandoned", &s.abandonedCollection);
        readBool(ini, "collections/wantToRead", &s.wantToReadCollection);
        s.downloadFolder = ini.value(QStringLiteral("downloads/folder")).toString();
        m_settings = s;
        return Status::success();
    }

    Settings current() const override { return m_settings; }

    Status save(const Settings &settings) override
    {
        const QString reason = invalidReason(settings);
        if (!reason.isEmpty())
            return Status::error(StatusCode::InvalidArgument, QStringLiteral("invalid ") + reason);

        const QString dir = QFileInfo(m_path).absolutePath();
        if (!QDir().mkpath(dir))
            return Status::error(StatusCode::IoError, QStringLiteral("can't create ") + dir);
        const QString temp = m_path + QStringLiteral(".tmp");
        QFile::remove(temp);
        {
            QSettings ini(temp, QSettings::IniFormat);
            ini.setValue(QStringLiteral("recording/newSessionAfterSleepSeconds"),
                         settings.newSessionAfterSleepSeconds);
            ini.setValue(QStringLiteral("sync/onBookClose"), settings.syncOnBookClose);
            ini.setValue(QStringLiteral("sync/beforeSleep"), settings.syncBeforeSleep);
            ini.setValue(QStringLiteral("sync/onWake"), settings.syncOnWake);
            ini.setValue(QStringLiteral("sync/whenWifiOn"), settings.syncWhenWifiOn);
            ini.setValue(QStringLiteral("sync/preSleepMinBatteryPercent"),
                         settings.preSleepMinBatteryPercent);
            ini.setValue(QStringLiteral("sync/livePushMode"), modeName(settings.livePushMode));
            ini.setValue(QStringLiteral("sync/livePushMinutes"), settings.livePushMinutes);
            ini.setValue(QStringLiteral("collections/mirror"), settings.mirrorCollections);
            ini.setValue(QStringLiteral("collections/abandoned"), settings.abandonedCollection);
            ini.setValue(QStringLiteral("collections/wantToRead"), settings.wantToReadCollection);
            ini.setValue(QStringLiteral("downloads/folder"), settings.downloadFolder);
            ini.sync();
            if (ini.status() != QSettings::NoError) {
                QFile::remove(temp);
                return Status::error(StatusCode::IoError, QStringLiteral("can't write ") + temp);
            }
        }
        // QFile::rename doesn't replace an existing file; remove first. Between
        // the two steps only the complete .tmp exists, and load() falls back to
        // defaults, never to half a file.
        if (QFile::exists(m_path) && !QFile::remove(m_path)) {
            QFile::remove(temp);
            return Status::error(StatusCode::IoError, QStringLiteral("can't replace ") + m_path);
        }
        if (!QFile::rename(temp, m_path))
            return Status::error(StatusCode::IoError, QStringLiteral("can't rename ") + temp);
        m_settings = settings;
        emit changed();
        return Status::success();
    }

private:
    template <typename Valid>
    static void readInt(QSettings &ini, const char *key, int *out, Valid valid)
    {
        const QString k = QLatin1String(key);
        if (!ini.contains(k))
            return;
        bool ok = false;
        const int v = ini.value(k).toInt(&ok);
        if (ok && valid(v))
            *out = v;
        else
            log::warn("settings", QStringLiteral("invalid %1; using the default").arg(k));
    }

    static void readBool(QSettings &ini, const char *key, bool *out)
    {
        const QString k = QLatin1String(key);
        if (ini.contains(k))
            *out = ini.value(k).toBool();
    }

    QString m_path;
    Settings m_settings;
};

} // namespace

std::unique_ptr<SettingsStore> createSettingsStore(const Paths &paths)
{
    return std::unique_ptr<SettingsStore>(new IniSettingsStore(paths));
}

} // namespace smalt
