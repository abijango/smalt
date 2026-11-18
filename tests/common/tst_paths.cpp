#include "smalt/paths.h"

#include <QtTest>

class TstPaths : public QObject
{
    Q_OBJECT

private slots:

    void devicePathsMatchTheSpec()
    {
        const smalt::Paths p = smalt::Paths::device();
        QCOMPARE(p.onboard(), QStringLiteral("/mnt/onboard"));
        QCOMPARE(p.addsDir(), QStringLiteral("/mnt/onboard/.adds/bookorbit"));
        QCOMPARE(p.configIni(), QStringLiteral("/mnt/onboard/.adds/bookorbit/config.ini"));
        QCOMPARE(p.ledger(), QStringLiteral("/mnt/onboard/.adds/bookorbit/ledger.sqlite"));
        QCOMPARE(p.settingsIni(), QStringLiteral("/mnt/onboard/.adds/bookorbit/settings.ini"));
        QCOMPARE(p.backupDir(), QStringLiteral("/mnt/onboard/.adds/bookorbit/backup"));
        QCOMPARE(p.logsDir(), QStringLiteral("/mnt/onboard/.adds/bookorbit/logs"));
        QCOMPARE(p.startupMarker(), QStringLiteral("/mnt/onboard/.adds/bookorbit/startup-session"));
        QCOMPARE(p.disabledSwitch(), QStringLiteral("/mnt/onboard/.adds/bookorbit/disabled"));
        QCOMPARE(p.disableHomeSwitch(),
                 QStringLiteral("/mnt/onboard/.adds/bookorbit/disable-home"));
        QCOMPARE(p.disableSyncSwitch(),
                 QStringLiteral("/mnt/onboard/.adds/bookorbit/disable-sync"));
        QCOMPARE(p.recoveryNotice(), QStringLiteral("/mnt/onboard/.adds/bookorbit/RECOVERY.txt"));
        QCOMPARE(p.uninstallMarker(), QStringLiteral("/mnt/onboard/.adds/bookorbit/uninstall"));
        QCOMPARE(p.caBundle(), QStringLiteral("/mnt/onboard/.adds/bookorbit/certs/ca-bundle.pem"));
        QCOMPARE(p.downloadsDir(), QStringLiteral("/mnt/onboard/BookOrbit"));
        QCOMPARE(p.nickelDatabase(), QStringLiteral("/mnt/onboard/.kobo/KoboReader.sqlite"));
        QCOMPARE(p.kobaltDir(), QStringLiteral("/mnt/onboard/.adds/kobalt"));
        QCOMPARE(smalt::Paths::installedLibrary(),
                 QStringLiteral("/usr/local/Kobo/imageformats/libsmalt.so"));
    }

    void testRootReplacesTheMountPoint()
    {
        const smalt::Paths p = smalt::Paths::forRoot(QStringLiteral("/tmp/fake-kobo/"));
        QCOMPARE(p.onboard(), QStringLiteral("/tmp/fake-kobo"));
        QCOMPARE(p.ledger(), QStringLiteral("/tmp/fake-kobo/.adds/bookorbit/ledger.sqlite"));
        QCOMPARE(p.downloadsDir(), QStringLiteral("/tmp/fake-kobo/BookOrbit"));
    }
};

QTEST_GUILESS_MAIN(TstPaths)
#include "tst_paths.moc"
