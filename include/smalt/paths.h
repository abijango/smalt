// Every on-device path from docs/SPEC.md, in one place.
//
// Code never builds an on-device path by hand: it asks a Paths value. On the
// Kobo, Paths::device() roots everything at /mnt/onboard. Desktop tests use
// Paths::forRoot(<temporary dir>) so nothing touches the real filesystem.
//
// Spec sections: Crash safety ("The companion's safeguards"), On-device data
// model ("Where it lives"), Sync engine ("Networking"), Feature design
// ("Downloads"), VERIFY-FINDINGS item 1 (Kobalt's profile).
#pragma once

#include <QString>

namespace smalt {

class Paths
{
public:
    // The user partition of a real Kobo: /mnt/onboard.
    static Paths device();

    // A fake user partition at `onboardRoot`, for tests and tools.
    static Paths forRoot(const QString &onboardRoot);

    // The user partition root (/mnt/onboard on the device).
    QString onboard() const { return m_onboard; }

    // .adds/bookorbit/: everything Smalt owns on the user partition.
    QString addsDir() const;

    // --- Configuration (Sync engine, "Networking") ---
    QString configIni() const; // .adds/bookorbit/config.ini (credentials; never in the repo)
    QString configIniExample() const; // .adds/bookorbit/config.ini.example (shipped default)
    QString caBundle() const; // .adds/bookorbit/certs/ca-bundle.pem (shipped CA certificates)

    // --- Ledger (On-device data model, "Where it lives") ---
    QString ledger() const; // .adds/bookorbit/ledger.sqlite
    QString backupDir() const; // .adds/bookorbit/backup/ (daily copies)

    // --- Logs (CLAUDE.md, "Code style") ---
    QString logsDir() const; // .adds/bookorbit/logs/

    // --- Crash safety ("The companion's safeguards") ---
    QString startupMarker() const; // .adds/bookorbit/startup-session
    QString disabledSwitch() const; // .adds/bookorbit/disabled
    QString disableHomeSwitch() const; // .adds/bookorbit/disable-home
    QString disableSyncSwitch() const; // .adds/bookorbit/disable-sync
    QString recoveryNotice() const; // .adds/bookorbit/RECOVERY.txt
    QString uninstallMarker() const; // .adds/bookorbit/uninstall (deleting it uninstalls)

    // --- Downloads (Feature design, "Downloads") ---
    QString downloadsDir() const; // BookOrbit/ (books saved as <Author>/<Title>.<ext>)

    // --- Nickel and Kobalt (read with care; see CLAUDE.md hard rules 4 and 8) ---
    QString nickelDatabase() const; // .kobo/KoboReader.sqlite
    // Kobalt's profile folder. kobalt.sqlite sits directly in the profile
    // directory (Kobalt doc/storage.md); task 32 confirms the exact subpath on
    // the device before reading it. Never open it while Kobalt has it open.
    QString kobaltDir() const; // .adds/kobalt/

    // --- System partition (not under the onboard root) ---
    // Where KoboRoot.tgz installs the mod library. Absolute on every target.
    static QString installedLibrary(); // /usr/local/Kobo/imageformats/libsmalt.so

private:
    explicit Paths(const QString &onboardRoot);
    QString adds(const char *name) const;

    QString m_onboard;
};

} // namespace smalt
