#include "smalt/paths.h"

#include <QDir>

namespace smalt {

Paths::Paths(const QString &onboardRoot) : m_onboard(QDir::cleanPath(onboardRoot)) { }

Paths Paths::device()
{
    return Paths(QStringLiteral("/mnt/onboard"));
}

Paths Paths::forRoot(const QString &onboardRoot)
{
    return Paths(onboardRoot);
}

QString Paths::adds(const char *name) const
{
    return addsDir() + QLatin1Char('/') + QLatin1String(name);
}

QString Paths::addsDir() const
{
    return m_onboard + QStringLiteral("/.adds/bookorbit");
}

QString Paths::configIni() const
{
    return adds("config.ini");
}

QString Paths::settingsIni() const
{
    return adds("settings.ini");
}

QString Paths::configIniExample() const
{
    return adds("config.ini.example");
}

QString Paths::caBundle() const
{
    return adds("certs/ca-bundle.pem");
}

QString Paths::ledger() const
{
    return adds("ledger.sqlite");
}

QString Paths::backupDir() const
{
    return adds("backup");
}

QString Paths::logsDir() const
{
    return adds("logs");
}

QString Paths::startupMarker() const
{
    return adds("startup-session");
}

QString Paths::disabledSwitch() const
{
    return adds("disabled");
}

QString Paths::disableHomeSwitch() const
{
    return adds("disable-home");
}

QString Paths::disableSyncSwitch() const
{
    return adds("disable-sync");
}

QString Paths::recoveryNotice() const
{
    return adds("RECOVERY.txt");
}

QString Paths::uninstallMarker() const
{
    return adds("uninstall");
}

QString Paths::downloadsDir() const
{
    return m_onboard + QStringLiteral("/BookOrbit");
}

QString Paths::nickelDatabase() const
{
    return m_onboard + QStringLiteral("/.kobo/KoboReader.sqlite");
}

QString Paths::kobaltDir() const
{
    return m_onboard + QStringLiteral("/.adds/kobalt");
}

QString Paths::installedLibrary()
{
    return QStringLiteral("/usr/local/Kobo/imageformats/libsmalt.so");
}

} // namespace smalt
