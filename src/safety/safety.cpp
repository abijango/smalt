// Crash safety (SPEC, Crash safety, "The companion's safeguards"): the startup
// marker, two-strike recovery, switch files and the readiness timeout.
//
// How the two strikes are counted. The marker holds `strikes=N`: how many
// startups in a row before this one had already left the marker behind.
// evaluateStartup() reads a leftover marker (one more strike) and, when that
// is strike one, rewrites it at once with strikes=1, so a crash anywhere in
// this boot's startup still counts, even before writeStartupMarker().
// writeStartupMarker() writes it with the same count; clearStartupMarker()
// removes it once the startup finished (home drawn, Nickel's home shown with
// disable-home, readiness timeout, clean shutdown or power-off). So:
//   clean boot             no marker at the next boot          count 0
//   crash during startup   marker strikes=0 at the next boot   count 1, logged
//   crash again            marker strikes=1 at the next boot   count 2, recovery
// A boot whose startup ends on a clear path, with or without a takeover,
// resets the count: only startups in a row count, as with Kobalt.
//
// Two-strike recovery leaves the mod installed but inert: it writes
// `disabled` (with the installed library's identity, so a reinstall is
// noticed and retried) and RECOVERY.txt. The ledger, outbox, settings and
// logs are not touched (CLAUDE.md hard rule 3).
#include "safety_internal.h"

#include "smalt/log.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace smalt {
namespace safety {
namespace {

const char kModule[] = "safety";
const char kStrikesKey[] = "strikes=";
const char kLibraryKey[] = "library=";

QString isoUtc(const QDateTime &when)
{
    return when.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
}

QString reasonName(MarkerClearReason reason)
{
    switch (reason) {
    case MarkerClearReason::HomeDrawn:
        return QStringLiteral("home screen drawn");
    case MarkerClearReason::CleanShutdown:
        return QStringLiteral("clean shutdown");
    case MarkerClearReason::PowerOff:
        return QStringLiteral("power off");
    case MarkerClearReason::FallbackToNickelHome:
        return QStringLiteral("fallback to Nickel's home");
    }
    return QStringLiteral("unknown");
}

// Reads a small file. False if it can't be opened; never throws.
bool readSmallFile(const QString &path, QByteArray *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = file.read(64 * 1024);
    return true;
}

// The value after `key` on its own line, or an empty string.
QString valueOf(const QByteArray &content, const char *key)
{
    const QByteArray prefix(key);
    for (const QByteArray &rawLine : content.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith(prefix))
            return QString::fromUtf8(line.mid(prefix.size())).trimmed();
    }
    return QString();
}

Status writeFileAtomically(const QString &path, const QByteArray &data)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return Status::error(StatusCode::IoError, QStringLiteral("cannot create %1").arg(dir));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("cannot open %1: %2").arg(path, file.errorString()));
    if (file.write(data) != data.size()) {
        const QString error = file.errorString();
        file.cancelWriting();
        file.commit();
        return Status::error(StatusCode::IoError,
                             QStringLiteral("cannot write %1: %2").arg(path, error));
    }
    if (!file.commit())
        return Status::error(StatusCode::IoError,
                             QStringLiteral("cannot save %1: %2").arg(path, file.errorString()));
    return Status::success();
}

// Removes a file if it exists. Missing is success.
Status removeIfPresent(const QString &path)
{
    QFile file(path);
    if (!file.exists())
        return Status::success();
    if (file.remove())
        return Status::success();
    return Status::error(StatusCode::IoError,
                         QStringLiteral("cannot remove %1: %2").arg(path, file.errorString()));
}

class SafetyImpl : public Safety
{
public:
    SafetyImpl(const Paths &paths, std::shared_ptr<Clock> clock, LibraryIdentity libraryIdentity)
        : m_paths(paths), m_clock(std::move(clock)), m_libraryIdentity(std::move(libraryIdentity))
    {
    }

    ~SafetyImpl() override
    {
        if (m_readinessTimer)
            m_clock->cancelTimer(m_readinessTimer);
    }

    StartupDecision evaluateStartup() override
    {
        if (m_evaluated)
            return m_decision;
        m_evaluated = true;
        m_decision = decide();
        return m_decision;
    }

    // Allowed with `disable-home` too: the marker covers the mod's whole
    // startup (hooks, recorder, ledger), not only the home takeover, so a
    // crash there still counts towards recovery. Refused when the mod is
    // inert or the readiness timeout has already ended this boot's startup.
    Status writeStartupMarker() override
    {
        if (!evaluateStartup().modActive)
            return Status::error(StatusCode::Unavailable,
                                 QStringLiteral("mod inactive this boot; no takeover"));
        if (m_timedOut)
            return Status::error(StatusCode::Unavailable,
                                 QStringLiteral("readiness timed out; no takeover this boot"));
        const Status status = writeMarker();
        if (status.ok())
            log::info(kModule, QStringLiteral("startup marker written"));
        else
            log::error(kModule, QStringLiteral("startup marker: %1").arg(status.message));
        return status;
    }

    Status clearStartupMarker(MarkerClearReason reason) override
    {
        // The startup finished (or was abandoned on purpose): no strike.
        m_strikes = 0;
        const bool present = QFile::exists(m_paths.startupMarker());
        const Status status = removeIfPresent(m_paths.startupMarker());
        if (!status.ok())
            log::error(kModule, QStringLiteral("startup marker: %1").arg(status.message));
        else if (present)
            log::info(kModule,
                      QStringLiteral("startup marker cleared: %1").arg(reasonName(reason)));
        return status;
    }

    bool isSwitchOn(SafetySwitch which) const override { return QFile::exists(switchPath(which)); }

    void startReadinessTimer(std::function<void()> onTimeout) override
    {
        if (m_readinessStarted) {
            log::warn(kModule, QStringLiteral("readiness timer already started; ignored"));
            return;
        }
        m_readinessStarted = true;
        if (m_ready)
            return;
        m_onTimeout = std::move(onTimeout);
        m_readinessTimer =
                m_clock->startTimer(kReadinessTimeoutMs, [this]() { readinessTimeout(); });
    }

    void markReady() override
    {
        if (m_timedOut) {
            log::info(kModule,
                      QStringLiteral("ready after the readiness timeout; staying out this boot"));
            return;
        }
        if (m_ready)
            return;
        m_ready = true;
        if (m_readinessTimer) {
            m_clock->cancelTimer(m_readinessTimer);
            m_readinessTimer = 0;
        }
        log::info(kModule, QStringLiteral("ready"));
    }

    bool readinessTimedOut() const override { return m_timedOut; }

private:
    QString switchPath(SafetySwitch which) const
    {
        switch (which) {
        case SafetySwitch::Disabled:
            return m_paths.disabledSwitch();
        case SafetySwitch::DisableHome:
            return m_paths.disableHomeSwitch();
        case SafetySwitch::DisableSync:
            return m_paths.disableSyncSwitch();
        }
        return m_paths.disabledSwitch();
    }

    QString libraryIdentity() const { return m_libraryIdentity ? m_libraryIdentity() : QString(); }

    Status writeMarker()
    {
        const QByteArray content = QByteArray(kStrikesKey) + QByteArray::number(m_strikes) + '\n'
                + "started=" + isoUtc(m_clock->nowUtc()).toUtf8() + '\n';
        return writeFileAtomically(m_paths.startupMarker(), content);
    }

    void removeLeftoverMarker()
    {
        const Status removed = removeIfPresent(m_paths.startupMarker());
        if (!removed.ok())
            log::error(kModule, QStringLiteral("leftover marker: %1").arg(removed.message));
    }

    // Reads a leftover marker without removing it. Returns how many startups
    // in a row have now failed to finish (0 when there was no marker).
    int readLeftoverMarker() const
    {
        const QString path = m_paths.startupMarker();
        if (!QFile::exists(path))
            return 0;
        QByteArray content;
        int before = 0;
        if (readSmallFile(path, &content)) {
            bool ok = false;
            before = valueOf(content, kStrikesKey).toInt(&ok);
            if (!ok || before < 0)
                before = 0;
        }
        // Anything above two already means recovery; keep the count small.
        return qMin(before, 1) + 1;
    }

    // True when `disabled` was written by recovery for a library that has
    // since been replaced (KoboRoot.tgz installed again).
    bool reinstalledSinceRecovery() const
    {
        QByteArray content;
        if (!readSmallFile(m_paths.disabledSwitch(), &content))
            return false;
        const QString recorded = valueOf(content, kLibraryKey);
        const QString current = libraryIdentity();
        return !recorded.isEmpty() && !current.isEmpty() && recorded != current;
    }

    StartupDecision decide()
    {
        StartupDecision d;
        const int failedInRow = readLeftoverMarker();

        if (QFile::exists(m_paths.disabledSwitch())) {
            if (!reinstalledSinceRecovery()) {
                // An inert mod has no startup to protect.
                removeLeftoverMarker();
                log::info(kModule, QStringLiteral("`disabled` is present: staying inactive"));
                return d;
            }
            const Status removed = removeIfPresent(m_paths.disabledSwitch());
            if (!removed.ok()) {
                log::error(kModule,
                           QStringLiteral("reinstall seen, but %1; staying inactive")
                                   .arg(removed.message));
                return d;
            }
            log::info(kModule, QStringLiteral("reinstalled after recovery: trying again"));
        }

        if (failedInRow >= 2) {
            removeLeftoverMarker();
            triggerRecovery();
            d.recoveryTriggered = true;
            return d;
        }

        m_strikes = failedInRow;
        if (failedInRow == 1) {
            // Keep strike one on disk for the whole of this startup.
            const Status kept = writeMarker();
            if (!kept.ok())
                log::error(kModule, QStringLiteral("leftover marker: %1").arg(kept.message));
        }
        d.modActive = true;
        d.recordingAllowed = true;
        d.previousStartupCrashed = failedInRow == 1;
        d.homeTakeoverAllowed = !isSwitchOn(SafetySwitch::DisableHome);
        d.syncAllowed = !isSwitchOn(SafetySwitch::DisableSync);
        if (d.previousStartupCrashed)
            log::warn(kModule,
                      QStringLiteral("the last startup didn't finish (one strike; logged only)"));
        if (!d.homeTakeoverAllowed)
            log::info(kModule, QStringLiteral("`disable-home` is present: no home takeover"));
        if (!d.syncAllowed)
            log::info(kModule, QStringLiteral("`disable-sync` is present: no network"));
        return d;
    }

    void triggerRecovery()
    {
        const QDateTime now = m_clock->nowUtc();
        log::error(kModule,
                   QStringLiteral("two startups in a row didn't finish: disabling Smalt "
                                  "(reading data kept)"));
        const QByteArray disabled =
                QByteArray("# Written by Smalt after two startups in a row didn't finish.\n"
                           "# See RECOVERY.txt. Delete this file to try again.\n")
                + "reason=two-strike-recovery\n" + "when=" + isoUtc(now).toUtf8() + '\n'
                + kLibraryKey + libraryIdentity().toUtf8() + '\n';
        const Status wrote = writeFileAtomically(m_paths.disabledSwitch(), disabled);
        if (!wrote.ok()) {
            // Stay inactive this boot anyway, and leave the count behind so
            // the next boot tries recovery again.
            log::error(kModule, QStringLiteral("recovery: %1").arg(wrote.message));
            writeFileAtomically(m_paths.startupMarker(), QByteArray(kStrikesKey) + "1\n");
        }
        const Status notice =
                writeFileAtomically(m_paths.recoveryNotice(), recoveryNoticeText(now).toUtf8());
        if (!notice.ok())
            log::error(kModule, QStringLiteral("recovery notice: %1").arg(notice.message));
        log::flush();
    }

    void readinessTimeout()
    {
        m_readinessTimer = 0;
        if (m_ready || m_timedOut)
            return;
        m_timedOut = true;
        log::warn(kModule,
                  QStringLiteral("not ready %1 s after startup: Nickel's home this boot; "
                                 "recording still runs")
                          .arg(kReadinessTimeoutMs / 1000));
        // Falling back to Nickel's home ends the startup (SPEC: the marker
        // is removed on a fallback to Nickel's home).
        clearStartupMarker(MarkerClearReason::FallbackToNickelHome);
        std::function<void()> onTimeout;
        onTimeout.swap(m_onTimeout);
        if (onTimeout)
            onTimeout();
    }

    const Paths m_paths;
    std::shared_ptr<Clock> m_clock;
    LibraryIdentity m_libraryIdentity;

    bool m_evaluated = false;
    StartupDecision m_decision;
    int m_strikes = 0;

    bool m_readinessStarted = false;
    bool m_ready = false;
    bool m_timedOut = false;
    int m_readinessTimer = 0;
    std::function<void()> m_onTimeout;
};

} // namespace

QString installedLibraryIdentity()
{
    const QFileInfo info(Paths::installedLibrary());
    if (!info.exists())
        return QString();
        // The inode change time: QFileInfo::created() on Linux with Qt 5.2,
        // renamed metadataChangeTime() in Qt 5.10.
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    const QDateTime changed = info.metadataChangeTime();
#else
    const QDateTime changed = info.created();
#endif
    return QStringLiteral("%1:%2:%3")
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch())
            .arg(changed.toMSecsSinceEpoch());
}

QString recoveryNoticeText(const QDateTime &when)
{
    const QString date = when.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QStringList lines;
    lines << QStringLiteral("Smalt has turned itself off")
          << QStringLiteral("===========================") << QString()
          << QStringLiteral("What happened") << QString()
          << QStringLiteral("Your Kobo didn't finish starting up two times in a row while")
          << QStringLiteral("Smalt (the BookOrbit companion) was starting. To make sure your")
          << QStringLiteral("Kobo keeps working, Smalt turned itself off on %1 UTC.").arg(date)
          << QStringLiteral("The Kobo now starts with its own home screen, and Smalt does")
          << QStringLiteral("nothing until you ask it to try again.") << QString()
          << QStringLiteral("Your reading data is safe") << QString()
          << QStringLiteral("Your books, your reading sessions and everything still waiting")
          << QStringLiteral("to sync with BookOrbit are kept. Nothing was deleted. Smalt")
          << QStringLiteral("picks up where it left off when it runs again.") << QString()
          << QStringLiteral("How to try again") << QString()
          << QStringLiteral("Either of these turns Smalt back on:") << QString()
          << QStringLiteral("1. Connect your Kobo to a computer, open the .adds/bookorbit")
          << QStringLiteral("   folder, delete the file named \"disabled\", eject the Kobo")
          << QStringLiteral("   and restart it.")
          << QStringLiteral("2. Or reinstall Smalt (copy KoboRoot.tgz into the .kobo folder")
          << QStringLiteral("   again) and restart.") << QString()
          << QStringLiteral("If it happens again, Smalt turns itself off again the same way.")
          << QStringLiteral("The files in .adds/bookorbit/logs/ say what went wrong; please")
          << QStringLiteral("include them if you report the problem.");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

std::unique_ptr<Safety> createSafetyWith(const Paths &paths, std::shared_ptr<Clock> clock,
                                         LibraryIdentity libraryIdentity)
{
    if (!clock)
        clock = createQtClock();
    return std::unique_ptr<Safety>(
            new SafetyImpl(paths, std::move(clock), std::move(libraryIdentity)));
}

} // namespace safety

// Cleanup for the plugin's NickelHook `uninstall` callback, which runs after
// NickelHook has already removed libsmalt.so. Removes only Smalt's transient
// state, so a later reinstall starts clean. Missing files are fine.
Status cleanupAfterUninstall(const Paths &paths)
{
    Status result;
    const QString removable[] = { paths.startupMarker(), paths.disabledSwitch() };
    for (const QString &path : removable) {
        const Status status = safety::removeIfPresent(path);
        if (!status.ok()) {
            log::error(safety::kModule, QStringLiteral("uninstall: %1").arg(status.message));
            result = status;
        }
    }
    log::info(safety::kModule,
              QStringLiteral("uninstall cleanup done; kept the ledger, backups, logs "
                             "and settings in %1")
                      .arg(paths.addsDir()));
    return result;
}

std::unique_ptr<Safety> createSafety(const Paths &paths)
{
    return safety::createSafetyWith(paths, safety::createQtClock(),
                                    &safety::installedLibraryIdentity);
}

} // namespace smalt
