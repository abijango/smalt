// Task 12 acceptance tests: boot sequences, two-strike recovery, RECOVERY.txt,
// switch files and the readiness timeout, on a fake clock and a temporary
// user partition (SPEC, Crash safety, "The companion's safeguards").
#include "fake_clock.h"
#include "safety/safety_internal.h"

#include "smalt/paths.h"
#include "smalt/safety.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using smalt::MarkerClearReason;
using smalt::Paths;
using smalt::Safety;
using smalt::SafetySwitch;
using smalt::StartupDecision;
using smalt::testing::FakeClock;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

} // namespace

class TstStartup : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Paths> m_paths;
    std::shared_ptr<FakeClock> m_clock;
    QString m_library;

    // One power-on of the Kobo: a fresh Safety on the same user partition.
    std::unique_ptr<Safety> boot(StartupDecision *decision)
    {
        m_clock->reboot();
        const QString *library = &m_library;
        std::unique_ptr<Safety> safety = smalt::safety::createSafetyWith(
                *m_paths, m_clock, [library]() { return *library; });
        *decision = safety->evaluateStartup();
        return safety;
    }

    // A boot whose home takeover crashes Nickel: the marker is written and the
    // process dies before anything clears it.
    StartupDecision crashedBoot()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        if (d.modActive)
            safety->writeStartupMarker();
        return d;
    }

    // A boot whose home screen draws.
    StartupDecision cleanBoot()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        if (d.modActive) {
            safety->writeStartupMarker();
            safety->clearStartupMarker(MarkerClearReason::HomeDrawn);
        }
        return d;
    }

    bool exists(const QString &path) const { return QFile::exists(path); }

private slots:

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_paths.reset(new Paths(Paths::forRoot(m_dir->path())));
        QVERIFY(QDir().mkpath(m_paths->addsDir()));
        m_clock = std::make_shared<FakeClock>();
        m_library = QStringLiteral("123:1000:2000");
    }

    void specValues()
    {
        QCOMPARE(smalt::kReadinessTimeoutMs, 30000);
        QCOMPARE(smalt::kSleepWatchdogMs, 15000);
        const StartupDecision d;
        QVERIFY(!d.modActive); // Inert unless startup says otherwise.
    }

    // --- Boot sequences ---

    void cleanBoots()
    {
        for (int i = 0; i < 3; ++i) {
            const StartupDecision d = cleanBoot();
            QVERIFY(d.modActive);
            QVERIFY(d.homeTakeoverAllowed);
            QVERIFY(d.syncAllowed);
            QVERIFY(d.recordingAllowed);
            QVERIFY(!d.recoveryTriggered);
            QVERIFY(!d.previousStartupCrashed);
            QVERIFY(!exists(m_paths->startupMarker()));
        }
        QVERIFY(!exists(m_paths->disabledSwitch()));
        QVERIFY(!exists(m_paths->recoveryNotice()));
    }

    void firstBootWithoutConfigFolder()
    {
        QVERIFY(QDir(m_paths->addsDir()).removeRecursively());
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QVERIFY(d.modActive);
        QVERIFY(safety->writeStartupMarker().ok());
        QVERIFY(exists(m_paths->startupMarker()));
    }

    void markerExistsOnlyDuringStartup()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QVERIFY(!exists(m_paths->startupMarker()));
        QVERIFY(safety->writeStartupMarker().ok());
        QVERIFY(exists(m_paths->startupMarker()));
        QVERIFY(safety->clearStartupMarker(MarkerClearReason::HomeDrawn).ok());
        QVERIFY(!exists(m_paths->startupMarker()));
        // Clearing again (clean shutdown after the home drew) is fine.
        QVERIFY(safety->clearStartupMarker(MarkerClearReason::CleanShutdown).ok());
    }

    void everyClearReasonRemovesMarker_data()
    {
        QTest::addColumn<int>("reason");
        QTest::newRow("home drawn") << int(MarkerClearReason::HomeDrawn);
        QTest::newRow("clean shutdown") << int(MarkerClearReason::CleanShutdown);
        QTest::newRow("power off") << int(MarkerClearReason::PowerOff);
        QTest::newRow("fallback") << int(MarkerClearReason::FallbackToNickelHome);
    }

    void everyClearReasonRemovesMarker()
    {
        QFETCH(int, reason);
        StartupDecision d;
        {
            std::unique_ptr<Safety> safety = boot(&d);
            QVERIFY(safety->writeStartupMarker().ok());
            QVERIFY(safety->clearStartupMarker(MarkerClearReason(reason)).ok());
            QVERIFY(!exists(m_paths->startupMarker()));
        }
        // The next boot doesn't count it as a crash.
        boot(&d);
        QVERIFY(d.modActive);
        QVERIFY(!d.previousStartupCrashed);
    }

    void oneCrash()
    {
        QVERIFY(crashedBoot().modActive);
        QVERIFY(exists(m_paths->startupMarker()));

        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(d.homeTakeoverAllowed);
        QVERIFY(d.syncAllowed);
        QVERIFY(d.recordingAllowed);
        QVERIFY(d.previousStartupCrashed); // Logged only.
        QVERIFY(!d.recoveryTriggered);
        QVERIFY(!exists(m_paths->disabledSwitch()));
        QVERIFY(!exists(m_paths->recoveryNotice()));
    }

    void twoCrashes()
    {
        crashedBoot();
        const StartupDecision second = crashedBoot();
        QVERIFY(second.modActive);
        QVERIFY(second.previousStartupCrashed);

        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QVERIFY(d.recoveryTriggered);
        QVERIFY(!d.modActive);
        QVERIFY(!d.homeTakeoverAllowed);
        QVERIFY(!d.syncAllowed);
        QVERIFY(!d.recordingAllowed);
        QVERIFY(exists(m_paths->disabledSwitch()));
        QVERIFY(exists(m_paths->recoveryNotice()));
        QVERIFY(!exists(m_paths->startupMarker()));
        QVERIFY(safety->isSwitchOn(SafetySwitch::Disabled));
        // An inert mod never takes over, so it never writes the marker.
        QCOMPARE(safety->writeStartupMarker().code, smalt::StatusCode::Unavailable);
        QVERIFY(!exists(m_paths->startupMarker()));
    }

    void staysInertAfterRecovery()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);
        for (int i = 0; i < 3; ++i) {
            const StartupDecision d = cleanBoot();
            QVERIFY(!d.modActive);
            QVERIFY(!d.recordingAllowed);
            QVERIFY(!d.recoveryTriggered); // Triggered once, not on every boot.
        }
    }

    void crashThenClean()
    {
        crashedBoot();
        QVERIFY(cleanBoot().previousStartupCrashed);
        // The clean boot reset the count: another single crash is strike one.
        crashedBoot();
        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(d.previousStartupCrashed);
        QVERIFY(!d.recoveryTriggered);
        QVERIFY(!exists(m_paths->disabledSwitch()));

        const StartupDecision after = cleanBoot();
        QVERIFY(after.modActive);
        QVERIFY(!after.previousStartupCrashed);
    }

    void crashesSeparatedByCleanBootsNeverTrigger()
    {
        for (int i = 0; i < 4; ++i) {
            crashedBoot();
            const StartupDecision d = cleanBoot();
            QVERIFY(d.modActive);
            QVERIFY(!d.recoveryTriggered);
        }
        QVERIFY(!exists(m_paths->disabledSwitch()));
    }

    void bootWithoutTakeoverResetsCount()
    {
        // A crash, then a boot that never takes over (disable-home) but
        // finishes its startup when Nickel's home shows, then a crash: two
        // crashes, but not in a row.
        crashedBoot();
        {
            StartupDecision d;
            std::unique_ptr<Safety> safety = boot(&d);
            QVERIFY(d.previousStartupCrashed);
            QVERIFY(safety->clearStartupMarker(MarkerClearReason::FallbackToNickelHome).ok());
        }
        crashedBoot();
        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(d.previousStartupCrashed);
        QVERIFY(!d.recoveryTriggered);
    }

    void leftoverStrikeKeptDuringStartup()
    {
        crashedBoot();
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QVERIFY(d.previousStartupCrashed);
        // Strike one stays on disk from the start of this boot's startup.
        QVERIFY(exists(m_paths->startupMarker()));
        QVERIFY(readAll(m_paths->startupMarker()).contains("strikes=1"));
        QVERIFY(safety->clearStartupMarker(MarkerClearReason::HomeDrawn).ok());
        QVERIFY(!exists(m_paths->startupMarker()));
    }

    void crashBeforeMarkerWriteStillCounts()
    {
        crashedBoot();
        {
            // Dies after evaluateStartup(), before writeStartupMarker()
            // (for example while waiting for Nickel or opening the ledger).
            StartupDecision d;
            std::unique_ptr<Safety> safety = boot(&d);
            QVERIFY(d.modActive);
            QVERIFY(d.previousStartupCrashed);
        }
        StartupDecision d;
        boot(&d);
        QVERIFY(d.recoveryTriggered);
        QVERIFY(!d.modActive);
        QVERIFY(exists(m_paths->disabledSwitch()));
        QVERIFY(!exists(m_paths->startupMarker()));
    }

    void disableHomeStillWritesMarker()
    {
        // The marker covers the whole startup, so with disable-home it is
        // still written and crashes still count.
        QVERIFY(writeFile(m_paths->disableHomeSwitch(), QByteArray()));
        {
            StartupDecision d;
            std::unique_ptr<Safety> safety = boot(&d);
            QVERIFY(d.modActive);
            QVERIFY(!d.homeTakeoverAllowed);
            QVERIFY(safety->writeStartupMarker().ok());
            QVERIFY(exists(m_paths->startupMarker()));
        }
        QVERIFY(crashedBoot().previousStartupCrashed);
        StartupDecision d;
        boot(&d);
        QVERIFY(d.recoveryTriggered);
    }

    void noTakeoverBootEndedByReadinessTimeoutResets()
    {
        QVERIFY(writeFile(m_paths->disableHomeSwitch(), QByteArray()));
        crashedBoot();
        {
            StartupDecision d;
            std::unique_ptr<Safety> safety = boot(&d);
            QVERIFY(d.previousStartupCrashed);
            safety->startReadinessTimer(std::function<void()>());
            m_clock->advance(smalt::kReadinessTimeoutMs);
            QVERIFY(!exists(m_paths->startupMarker()));
        }
        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(!d.previousStartupCrashed);
    }

    void inertBootDropsLeftoverMarker()
    {
        QVERIFY(writeFile(m_paths->startupMarker(), QByteArray("strikes=0\n")));
        QVERIFY(writeFile(m_paths->disabledSwitch(), QByteArray()));
        StartupDecision d;
        boot(&d);
        QVERIFY(!d.modActive);
        QVERIFY(!exists(m_paths->startupMarker()));
    }

    void readinessTimeoutIsNotAStrike()
    {
        crashedBoot();
        {
            StartupDecision d;
            std::unique_ptr<Safety> safety = boot(&d);
            QVERIFY(safety->writeStartupMarker().ok());
            int timeouts = 0;
            safety->startReadinessTimer([&timeouts]() { ++timeouts; });
            m_clock->advance(smalt::kReadinessTimeoutMs);
            QCOMPARE(timeouts, 1);
            // Falling back to Nickel's home removed the marker.
            QVERIFY(!exists(m_paths->startupMarker()));
        }
        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(!d.previousStartupCrashed);
    }

    void unreadableMarkerCountsAsOneCrash()
    {
        QVERIFY(writeFile(m_paths->startupMarker(), QByteArray("\xff\xfe garbage")));
        const StartupDecision d = crashedBoot();
        QVERIFY(d.modActive);
        QVERIFY(d.previousStartupCrashed);
        StartupDecision third;
        boot(&third);
        QVERIFY(third.recoveryTriggered);
    }

    // --- RECOVERY.txt ---

    void recoveryNoticeContent()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);

        const QByteArray raw = readAll(m_paths->recoveryNotice());
        QVERIFY(!raw.isEmpty());
        const QString text = QString::fromUtf8(raw);
        // Plain text: printable ASCII and newlines, no markup, short lines.
        for (int i = 0; i < raw.size(); ++i) {
            const char c = raw.at(i);
            QVERIFY2(c == '\n' || (c >= 0x20 && c < 0x7f), "RECOVERY.txt is plain ASCII");
        }
        QVERIFY(!text.contains(QLatin1Char('<')));
        for (const QString &line : text.split(QLatin1Char('\n')))
            QVERIFY2(line.size() <= 78, qPrintable(line));
        // What happened, that reading data is safe, how to retry.
        QVERIFY(text.contains(QStringLiteral("two times in a row")));
        QVERIFY(text.contains(QStringLiteral("reading data is safe")));
        QVERIFY(text.contains(QStringLiteral("delete the file named \"disabled\"")));
        QVERIFY(text.contains(QStringLiteral("reinstall")));
        QVERIFY(text.contains(QStringLiteral("2026-11-17")));
        // The clock hasn't moved since that boot.
        QCOMPARE(text, smalt::safety::recoveryNoticeText(m_clock->nowUtc()));
    }

    void recoveryNoticeWrittenOnce()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);
        // Mark the notice: if a later boot rewrote it, the mark would vanish.
        const QByteArray original = readAll(m_paths->recoveryNotice());
        QVERIFY(writeFile(m_paths->recoveryNotice(), original + "marked\n"));
        for (int i = 0; i < 3; ++i)
            cleanBoot();
        QCOMPARE(readAll(m_paths->recoveryNotice()), original + "marked\n");
    }

    // --- Retrying after recovery ---

    void deletingDisabledRetries()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);
        QVERIFY(QFile::remove(m_paths->disabledSwitch()));

        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(!d.previousStartupCrashed);
        QVERIFY(!d.recoveryTriggered);
        // It takes two new crashes in a row to trigger recovery again.
        crashedBoot();
        QVERIFY(crashedBoot().modActive);
        QVERIFY(cleanBoot().recoveryTriggered);
    }

    void reinstallRetries()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);
        QVERIFY(!cleanBoot().modActive);

        m_library = QStringLiteral("123:1000:9999"); // KoboRoot.tgz extracted again.
        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(d.homeTakeoverAllowed);
        QVERIFY(!exists(m_paths->disabledSwitch()));
    }

    void unreadableLibraryKeepsRecovery()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);
        m_library.clear();
        QVERIFY(!cleanBoot().modActive);
        QVERIFY(exists(m_paths->disabledSwitch()));
    }

    void handMadeDisabledSurvivesReinstall()
    {
        QVERIFY(writeFile(m_paths->disabledSwitch(), QByteArray()));
        QVERIFY(!cleanBoot().modActive);
        m_library = QStringLiteral("456:1:2");
        const StartupDecision d = cleanBoot();
        QVERIFY(!d.modActive);
        QVERIFY(!d.recoveryTriggered);
        QVERIFY(exists(m_paths->disabledSwitch()));
    }

    // --- Switch files ---

    void switches_data()
    {
        QTest::addColumn<bool>("disabled");
        QTest::addColumn<bool>("disableHome");
        QTest::addColumn<bool>("disableSync");
        QTest::addColumn<bool>("active");
        QTest::addColumn<bool>("home");
        QTest::addColumn<bool>("sync");
        QTest::addColumn<bool>("recording");

        QTest::newRow("none") << false << false << false << true << true << true << true;
        QTest::newRow("disabled") << true << false << false << false << false << false << false;
        QTest::newRow("disable-home") << false << true << false << true << false << true << true;
        QTest::newRow("disable-sync") << false << false << true << true << true << false << true;
        QTest::newRow("disable-home+sync")
                << false << true << true << true << false << false << true;
        QTest::newRow("all") << true << true << true << false << false << false << false;
    }

    void switches()
    {
        QFETCH(bool, disabled);
        QFETCH(bool, disableHome);
        QFETCH(bool, disableSync);
        QFETCH(bool, active);
        QFETCH(bool, home);
        QFETCH(bool, sync);
        QFETCH(bool, recording);

        if (disabled)
            QVERIFY(writeFile(m_paths->disabledSwitch(), QByteArray()));
        if (disableHome)
            QVERIFY(writeFile(m_paths->disableHomeSwitch(), QByteArray()));
        if (disableSync)
            QVERIFY(writeFile(m_paths->disableSyncSwitch(), QByteArray()));

        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QCOMPARE(d.modActive, active);
        QCOMPARE(d.homeTakeoverAllowed, home);
        QCOMPARE(d.syncAllowed, sync);
        QCOMPARE(d.recordingAllowed, recording);
        QVERIFY(!d.recoveryTriggered);
        QVERIFY(!d.previousStartupCrashed);
        QCOMPARE(safety->isSwitchOn(SafetySwitch::Disabled), disabled);
        QCOMPARE(safety->isSwitchOn(SafetySwitch::DisableHome), disableHome);
        QCOMPARE(safety->isSwitchOn(SafetySwitch::DisableSync), disableSync);
        // Switch files are never removed by startup.
        QCOMPARE(exists(m_paths->disabledSwitch()), disabled);
        QCOMPARE(exists(m_paths->disableHomeSwitch()), disableHome);
        QCOMPARE(exists(m_paths->disableSyncSwitch()), disableSync);
    }

    void switchesAreReread()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QVERIFY(!safety->isSwitchOn(SafetySwitch::DisableSync));
        QVERIFY(writeFile(m_paths->disableSyncSwitch(), QByteArray("x")));
        QVERIFY(safety->isSwitchOn(SafetySwitch::DisableSync));
        QVERIFY(QFile::remove(m_paths->disableSyncSwitch()));
        QVERIFY(!safety->isSwitchOn(SafetySwitch::DisableSync));
    }

    void evaluateStartupIsDecidedOnce()
    {
        crashedBoot();
        StartupDecision first;
        std::unique_ptr<Safety> safety = boot(&first);
        QVERIFY(first.previousStartupCrashed);
        QVERIFY(writeFile(m_paths->disabledSwitch(), QByteArray()));
        const StartupDecision again = safety->evaluateStartup();
        QCOMPARE(again.modActive, first.modActive);
        QCOMPARE(again.previousStartupCrashed, first.previousStartupCrashed);
    }

    // --- Readiness timeout ---

    void readinessTimesOutAt30s()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        QVERIFY(safety->writeStartupMarker().ok());
        int timeouts = 0;
        safety->startReadinessTimer([&timeouts]() { ++timeouts; });

        m_clock->advance(smalt::kReadinessTimeoutMs - 1);
        QCOMPARE(timeouts, 0);
        QVERIFY(!safety->readinessTimedOut());

        m_clock->advance(1);
        QCOMPARE(timeouts, 1);
        QVERIFY(safety->readinessTimedOut());
        QVERIFY(!exists(m_paths->startupMarker()));
        // No takeover this boot; recording still runs.
        QVERIFY(d.recordingAllowed);
        QCOMPARE(safety->writeStartupMarker().code, smalt::StatusCode::Unavailable);
        QVERIFY(!exists(m_paths->startupMarker()));

        // Ready too late: still out of the way this boot, callback not rerun.
        safety->markReady();
        QVERIFY(safety->readinessTimedOut());
        m_clock->advance(10 * smalt::kReadinessTimeoutMs);
        QCOMPARE(timeouts, 1);

        // The next boot tries again.
        StartupDecision next;
        std::unique_ptr<Safety> later = boot(&next);
        QVERIFY(next.homeTakeoverAllowed);
        QVERIFY(!later->readinessTimedOut());
    }

    void readyInTimeCancelsTimeout()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        int timeouts = 0;
        safety->startReadinessTimer([&timeouts]() { ++timeouts; });
        m_clock->advance(12000);
        safety->markReady();
        m_clock->advance(10 * smalt::kReadinessTimeoutMs);
        QCOMPARE(timeouts, 0);
        QVERIFY(!safety->readinessTimedOut());
        QCOMPARE(m_clock->pendingTimers(), 0);
    }

    void readyBeforeTimerStarts()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        safety->markReady();
        int timeouts = 0;
        safety->startReadinessTimer([&timeouts]() { ++timeouts; });
        m_clock->advance(2 * smalt::kReadinessTimeoutMs);
        QCOMPARE(timeouts, 0);
        QVERIFY(!safety->readinessTimedOut());
    }

    void readinessTimerStartsOnce()
    {
        StartupDecision d;
        std::unique_ptr<Safety> safety = boot(&d);
        int first = 0;
        int second = 0;
        safety->startReadinessTimer([&first]() { ++first; });
        m_clock->advance(20000);
        safety->startReadinessTimer([&second]() { ++second; });
        m_clock->advance(10000);
        QCOMPARE(first, 1);
        QCOMPARE(second, 0);
        m_clock->advance(10 * smalt::kReadinessTimeoutMs);
        QCOMPARE(first + second, 1);
    }

    void destroyedSafetyCancelsReadinessTimer()
    {
        int timeouts = 0;
        {
            StartupDecision d;
            std::unique_ptr<Safety> safety = boot(&d);
            safety->startReadinessTimer([&timeouts]() { ++timeouts; });
        }
        QCOMPARE(m_clock->pendingTimers(), 0);
        m_clock->advance(2 * smalt::kReadinessTimeoutMs);
        QCOMPARE(timeouts, 0);
    }

    // --- Uninstall cleanup (the plugin's NickelHook uninstall callback) ---

    void uninstallCleanupKeepsReadingData()
    {
        const QStringList kept = {
            m_paths->ledger(),
            m_paths->ledger() + QStringLiteral(".corrupt-20261117T063000Z"),
            m_paths->backupDir() + QStringLiteral("/ledger-2026-11-16.sqlite"),
            m_paths->logsDir() + QStringLiteral("/smalt.log"),
            m_paths->configIni(),
            m_paths->configIniExample(),
            m_paths->caBundle(),
            m_paths->disableHomeSwitch(),
            m_paths->disableSyncSwitch(),
            m_paths->recoveryNotice(),
        };
        for (const QString &path : kept)
            QVERIFY(writeFile(path, QByteArray("data")));
        QVERIFY(writeFile(m_paths->startupMarker(), QByteArray("strikes=1\n")));
        QVERIFY(writeFile(m_paths->disabledSwitch(), QByteArray("library=x\n")));

        QVERIFY(smalt::safety::cleanupAfterUninstall(*m_paths).ok());

        for (const QString &path : kept) {
            QVERIFY2(exists(path), qPrintable(path));
            QCOMPARE(readAll(path), QByteArray("data"));
        }
        QVERIFY(!exists(m_paths->startupMarker()));
        QVERIFY(!exists(m_paths->disabledSwitch()));

        // Nothing left to clean is fine too.
        QVERIFY(smalt::safety::cleanupAfterUninstall(*m_paths).ok());
        QVERIFY(QDir(m_paths->addsDir()).removeRecursively());
        QVERIFY(smalt::safety::cleanupAfterUninstall(*m_paths).ok());
    }

    void reinstallAfterUninstallStartsClean()
    {
        crashedBoot();
        crashedBoot();
        QVERIFY(cleanBoot().recoveryTriggered);
        QVERIFY(smalt::safety::cleanupAfterUninstall(*m_paths).ok());
        const StartupDecision d = cleanBoot();
        QVERIFY(d.modActive);
        QVERIFY(!d.previousStartupCrashed);
    }

    void installedLibraryIdentityIsHarmlessOnDesktop()
    {
        // The real library path doesn't exist on the desktop; the identity is
        // empty, which disables reinstall detection but nothing else.
        if (!QFile::exists(Paths::installedLibrary()))
            QVERIFY(smalt::safety::installedLibraryIdentity().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstStartup)
#include "tst_startup.moc"
