// Every user-visible string the screens show, in one place.
//
// Strings that docs/SPEC.md gives ("Copy", "Where sync status shows", the
// Settings screen) are used exactly. Everything else is chosen here in plain,
// sentence-case wording and listed in the task 22 report as a choice. Tests
// check this file against the spec's Copy table.
#pragma once

#include "smalt/bookorbit_client.h"
#include "smalt/ledger.h"
#include "smalt/sync.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace smalt {
namespace screens {
namespace copy {

inline QString plural(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}

// --- From the spec's Copy table (exact) ---

// "Manual sync done": Synced: 2 sessions, 1 highlight, 1 position
inline QString manualSyncDone(const SyncResult &r)
{
    QStringList parts;
    if (r.sessionsPushed > 0)
        parts << plural(r.sessionsPushed, "session", "sessions");
    if (r.highlightsPushed > 0)
        parts << plural(r.highlightsPushed, "highlight", "highlights");
    if (r.positionsPushed > 0)
        parts << plural(r.positionsPushed, "position", "positions");
    if (r.statusesPushed > 0)
        parts << plural(r.statusesPushed, "status", "statuses");
    if (parts.isEmpty())
        return QStringLiteral("Synced: nothing new to send");
    return QStringLiteral("Synced: ") + parts.join(QStringLiteral(", "));
}

inline QString repeatedFailure()
{
    return QStringLiteral("Couldn't reach BookOrbit. Your reading is saved and will sync later.");
}

inline QString keyRejected()
{
    return QStringLiteral("BookOrbit didn't accept your KOReader login. Check it in BookOrbit's "
                          "Settings > KOReader.");
}

// "Removal banner": 3 books were removed from BookOrbit. **Review**
inline QString removalBanner(int n)
{
    if (n == 1)
        return QStringLiteral("1 book was removed from BookOrbit.");
    return QStringLiteral("%1 books were removed from BookOrbit.").arg(n);
}

inline QString removalReview()
{
    return QStringLiteral("Review");
}

// "Where sync status shows": "Synced 10:42 · 3 waiting", or "Offline · 3 waiting".
inline QString statusLine(const SyncStatus &s, bool haveEngine)
{
    QString head;
    if (!haveEngine)
        head = QStringLiteral("Not syncing");
    else if (s.running)
        head = QStringLiteral("Syncing");
    else if (!s.online)
        head = QStringLiteral("Offline");
    else if (s.lastSuccessUtc.isValid())
        head = QStringLiteral("Synced ")
                + s.lastSuccessUtc.toLocalTime().toString(QStringLiteral("HH:mm"));
    else
        head = QStringLiteral("Not synced yet");
    if (s.waiting > 0)
        head += QStringLiteral(" · %1 waiting").arg(s.waiting);
    return head;
}

// --- Chosen here (not in the spec) ---

inline QString errorText(ErrorKind e)
{
    switch (e) {
    case ErrorKind::AuthRejected:
        return keyRejected();
    case ErrorKind::Offline:
    case ErrorKind::Timeout:
        return QStringLiteral("Couldn't reach BookOrbit.");
    case ErrorKind::TlsError:
        return QStringLiteral("BookOrbit's certificate wasn't accepted.");
    case ErrorKind::None:
        return QString();
    default:
        return QStringLiteral("BookOrbit didn't answer as expected.");
    }
}

inline QString downloadingOf(int n, int total)
{
    return QStringLiteral("Downloading %1 of %2").arg(n).arg(total);
}

inline QString offlineSearchNote()
{
    return QStringLiteral("You're offline. Searching only the books on this Kobo.");
}

inline QString nothingCachedYet()
{
    return QStringLiteral("Nothing to show yet. Connect to Wi-Fi to load your library.");
}

inline QString loadingLibrary()
{
    return QStringLiteral("Loading your library.");
}

inline QString emptyLibrary()
{
    return QStringLiteral("There are no books in BookOrbit yet.");
}

inline QString nothingInProgress()
{
    return QStringLiteral("Nothing in progress on this Kobo.");
}

inline QString noMatches()
{
    return QStringLiteral("No books match.");
}

inline QString noResults()
{
    return QStringLiteral("No results.");
}

inline QString unavailableOffline()
{
    return QStringLiteral("Not available offline");
}

inline QString showingOf(int shown, int total)
{
    return QStringLiteral("Showing %1 of %2 books.").arg(shown).arg(total);
}

inline QString clearFilters()
{
    return QStringLiteral("Clear");
}

inline QString statusLabel(ReadStatus s)
{
    switch (s) {
    case ReadStatus::WantToRead:
        return QStringLiteral("Want to read");
    case ReadStatus::Reading:
        return QStringLiteral("Reading");
    case ReadStatus::OnHold:
        return QStringLiteral("On hold");
    case ReadStatus::Read:
        return QStringLiteral("Read");
    case ReadStatus::Abandoned:
        return QStringLiteral("Abandoned");
    case ReadStatus::Unread:
    case ReadStatus::Unknown:
    default:
        return QStringLiteral("Unread");
    }
}

inline QString openFailedNotReady()
{
    return QStringLiteral("Opening books from here isn't available on this build yet.");
}

inline QString openFailedNotFound()
{
    return QStringLiteral("Nickel doesn't know this book yet. It may still be importing. Try "
                          "again in a moment.");
}

inline QString openFailed()
{
    return QStringLiteral("Couldn't open this book.");
}

inline QString importNotReady()
{
    return QStringLiteral("Saved to the Kobo. Adding it to the library isn't available on this "
                          "build yet.");
}

inline QString offlineChange()
{
    return QStringLiteral("You're offline. Connect to Wi-Fi to change this for a book that "
                          "isn't on the Kobo.");
}

inline QString offlineDownload()
{
    return QStringLiteral("You're offline. Connect to Wi-Fi to download books.");
}

inline QString koboHomeNotReady()
{
    return QStringLiteral("Kobo's home isn't available from here on this build yet.");
}

inline QString syncNotAvailable()
{
    return QStringLiteral("Syncing isn't available on this build.");
}

inline QString reviewNotReady()
{
    return QStringLiteral("The review screen isn't available yet.");
}

inline QString downloadFailed(ErrorKind e)
{
    if (e == ErrorKind::LocalIo)
        return QStringLiteral("Couldn't save the book. Is the Kobo's storage full?");
    if (e == ErrorKind::AuthRejected)
        return keyRejected();
    if (e == ErrorKind::Cancelled)
        return QStringLiteral("Download cancelled.");
    return QStringLiteral("Download stopped. Tap Download to continue where it left off.");
}

inline QString testConnectionOk(const QString &serverVersion)
{
    if (serverVersion.isEmpty())
        return QStringLiteral("Connected to BookOrbit.");
    return QStringLiteral("Connected to BookOrbit %1.").arg(serverVersion);
}

inline QString badServerUrl()
{
    return QStringLiteral("Enter the full address, starting with http:// or https://");
}

inline QString exportLogDone(const QString &where)
{
    return QStringLiteral("Log saved to %1").arg(where);
}

inline QString exportLogFailed()
{
    return QStringLiteral("Couldn't save the log.");
}

} // namespace copy
} // namespace screens
} // namespace smalt
