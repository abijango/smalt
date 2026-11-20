#include "smalt/settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using smalt::LivePushMode;
using smalt::Settings;
using smalt::StatusCode;

class TstSettings : public QObject
{
    Q_OBJECT

private slots:

    void defaultsMatchTheSpec()
    {
        const Settings s;
        QCOMPARE(s.newSessionAfterSleepSeconds, 15 * 60);
        QCOMPARE(int(s.livePushMode), int(LivePushMode::OnlyWhenConnected));
        QCOMPARE(s.livePushMinutes, 5);
        QCOMPARE(s.preSleepMinBatteryPercent, 20);
        QVERIFY(s.syncOnBookClose && s.syncBeforeSleep && s.syncOnWake && s.syncWhenWifiOn);
        QVERIFY(s.mirrorCollections);
        QVERIFY(!s.abandonedCollection && !s.wantToReadCollection);
        QVERIFY(s.downloadFolder.isEmpty());
    }

    void missingFileGivesDefaults()
    {
        QTemporaryDir root;
        auto store = smalt::createSettingsStore(smalt::Paths::forRoot(root.path()));
        QVERIFY(store->load().ok());
        QCOMPARE(store->current().livePushMinutes, 5);
    }

    void saveAndLoadRoundTrip()
    {
        QTemporaryDir root;
        const smalt::Paths paths = smalt::Paths::forRoot(root.path());
        auto store = smalt::createSettingsStore(paths);
        QSignalSpy changed(store.get(), SIGNAL(changed()));
        Settings s;
        s.newSessionAfterSleepSeconds = smalt::kNewSessionAfterSleepNever;
        s.livePushMode = LivePushMode::ConnectBriefly;
        s.livePushMinutes = 15;
        s.syncOnWake = false;
        s.preSleepMinBatteryPercent = 35;
        s.wantToReadCollection = true;
        s.downloadFolder = QStringLiteral("/mnt/onboard/Books");
        QVERIFY(store->save(s).ok());
        QCOMPARE(changed.count(), 1);
        QVERIFY(QFile::exists(paths.settingsIni()));
        QVERIFY(!QFile::exists(paths.settingsIni() + QStringLiteral(".tmp")));

        auto again = smalt::createSettingsStore(paths);
        QVERIFY(again->load().ok());
        const Settings r = again->current();
        QCOMPARE(r.newSessionAfterSleepSeconds, smalt::kNewSessionAfterSleepNever);
        QCOMPARE(int(r.livePushMode), int(LivePushMode::ConnectBriefly));
        QCOMPARE(r.livePushMinutes, 15);
        QVERIFY(!r.syncOnWake);
        QCOMPARE(r.preSleepMinBatteryPercent, 35);
        QVERIFY(r.wantToReadCollection);
        QCOMPARE(r.downloadFolder, QStringLiteral("/mnt/onboard/Books"));
    }

    void invalidValuesAreRefusedAndNothingIsWritten()
    {
        QTemporaryDir root;
        const smalt::Paths paths = smalt::Paths::forRoot(root.path());
        auto store = smalt::createSettingsStore(paths);
        QSignalSpy changed(store.get(), SIGNAL(changed()));
        Settings s;
        s.livePushMinutes = 7;
        QCOMPARE(int(store->save(s).code), int(StatusCode::InvalidArgument));
        s = Settings();
        s.newSessionAfterSleepSeconds = 120;
        QCOMPARE(int(store->save(s).code), int(StatusCode::InvalidArgument));
        s = Settings();
        s.preSleepMinBatteryPercent = 101;
        QCOMPARE(int(store->save(s).code), int(StatusCode::InvalidArgument));
        QCOMPARE(changed.count(), 0);
        QVERIFY(!QFile::exists(paths.settingsIni()));
    }

    void badValuesInTheFileFallBackToDefaults()
    {
        QTemporaryDir root;
        const smalt::Paths paths = smalt::Paths::forRoot(root.path());
        QVERIFY(QDir().mkpath(paths.addsDir()));
        QFile f(paths.settingsIni());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[recording]\nnewSessionAfterSleepSeconds=99\n"
                "[sync]\nlivePushMode=sometimes\nlivePushMinutes=abc\nonWake=false\n");
        f.close();
        auto store = smalt::createSettingsStore(paths);
        QVERIFY(store->load().ok());
        const Settings r = store->current();
        QCOMPARE(r.newSessionAfterSleepSeconds, 15 * 60);
        QCOMPARE(int(r.livePushMode), int(LivePushMode::OnlyWhenConnected));
        QCOMPARE(r.livePushMinutes, 5);
        QVERIFY(!r.syncOnWake); // Valid values in the same file are kept.
    }

    void unwritableFolderIsAnIoError()
    {
        QTemporaryDir root;
        // A file where .adds should be: the settings folder can't be created.
        QFile blocker(root.path() + QStringLiteral("/.adds"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        auto store = smalt::createSettingsStore(smalt::Paths::forRoot(root.path()));
        QCOMPARE(int(store->save(Settings()).code), int(StatusCode::IoError));
        QCOMPARE(store->current().livePushMinutes, 5);
    }

    void loginStaysInConfigIni()
    {
        QTemporaryDir root;
        const smalt::Paths paths = smalt::Paths::forRoot(root.path());
        QVERIFY(paths.settingsIni() != paths.configIni());
        auto store = smalt::createSettingsStore(paths);
        QVERIFY(store->save(Settings()).ok());
        QFile f(paths.settingsIni());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(!text.contains("password"));
        QVERIFY(!text.contains("username"));
        QVERIFY(!QFile::exists(paths.configIni()));
    }
};

QTEST_GUILESS_MAIN(TstSettings)
#include "tst_settings.moc"
