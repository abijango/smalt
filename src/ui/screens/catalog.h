// The screens' view of the BookOrbit library.
//
// The whole library is kept as a light index (one BookItem per book) that is
// loaded from the ledger's remote_cache at start, so every screen works at once
// and offline, and refreshed from the API in the background (100 books per
// request, each page cached). Library filters, Series, Up next, Reading now and
// offline search are all computed from the index; nothing here blocks the UI
// thread (the client is asynchronous, the ledger calls are short).
//
// Spec: Feature design ("Home screen and library", "Large-library UX": pages
// are cached with their covers), Status and ratings.
#pragma once

#include "smalt/bookorbit_client.h"
#include "smalt/ledger.h"
#include "smalt/screens.h"
#include "smalt/ui_kit.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>

namespace smalt {
namespace screens {

// One book as the catalog lists it (KoreaderCatalogBookListItem).
struct BookItem
{
    qint64 id = 0;
    QString title;
    QStringList authors;
    qint64 seriesId = 0;
    QString seriesName;
    double seriesIndex = -1;
    double progress = -1; // 0..1 on the server, negative when unknown.
    QString lastReadAt; // ISO 8601, may be empty.
    ReadStatus status = ReadStatus::Unknown; // The server's status.
    QStringList formats; // Lower case: epub, cbz, pdf ...
    QString addedAt;
    bool hasCover = true;

    QString authorLine() const { return authors.join(QStringLiteral(", ")); }

    QString firstAuthor() const { return authors.isEmpty() ? QString() : authors.first(); }

    bool isComic() const;
    bool isPdf() const;
    bool isAudio() const; // Audiobooks are left out of the library.
    // "CBZ" / "PDF" for comics and PDFs, empty for books (the cover label).
    QString formatLabel() const;
    QString extension() const; // Preferred file extension, lower case.
    QDateTime lastRead() const;
    QDateTime added() const;
};

BookItem parseBookItem(const QJsonObject &o);
// BookOrbit's wire value ("want_to_read") to the ledger's enum, and back.
ReadStatus parseReadStatus(const QString &wire);
QString readStatusWire(ReadStatus s);

enum class SortOrder { Title, Author, RecentlyAdded, RecentlyRead };

QString sortLabel(SortOrder s);

// Library chips: groups OR inside, AND between (SPEC, "Large-library UX").
struct LibraryFilter
{
    bool books = false, comics = false, pdfs = false; // Format.
    bool unread = false, reading = false, finished = false; // Status.
    bool onKobo = false;
    SortOrder sort = SortOrder::Title;

    bool active() const
    {
        return books || comics || pdfs || unread || reading || finished || onKobo;
    }
};

// A series, derived from the index.
struct SeriesItem
{
    qint64 id = 0;
    QString name;
    QVector<BookItem> books; // In series order.
    int read = 0;
    int reading = 0;
    bool allComics = false;
    QDateTime lastRead;
    QDateTime added;
};

class Catalog : public QObject
{
    Q_OBJECT

public:
    Catalog(const ShellDeps &deps, QObject *parent);

    // --- Index ---
    const QVector<BookItem> &books() const { return m_books; }

    const BookItem *find(qint64 bookId) const;

    bool indexEmpty() const { return m_books.isEmpty(); }

    // Reads the cached pages (remote_cache). Done at start.
    void loadFromCache();
    // Starts a background refresh from the API. No-op without a client or while one runs.
    void refresh();

    bool refreshing() const { return m_refreshing; }

    // --- Connectivity as the screens see it ---
    // False after a request failed with Offline or Timeout, or when Nickel says
    // there is no network; true after any answer from the server.
    bool online() const { return m_online; }

    ErrorKind lastError() const { return m_lastError; }

    // --- On Kobo and local state ---
    bool onKobo(qint64 bookId) const;
    bool bookMap(qint64 bookId, BookMapEntry *out) const;
    // Drops the cached book_map lookups (after downloads, sync, removals).
    void invalidateLocal();
    // The ledger's status when this Kobo has one for the book, else the server's.
    ReadStatus statusOf(const BookItem &b) const;
    // 0..1 from this Kobo's own position when the book is here, else the server's; negative = none.
    double progressOf(const BookItem &b) const;
    QDateTime localLastRead(const BookItem &b) const;

    // --- Derived lists ---
    QVector<BookItem> filtered(const LibraryFilter &f) const;
    QVector<SeriesItem> series() const;
    // Books in progress on this Kobo, newest first.
    QVector<BookItem> readingNow() const;
    // The next unread book in each series being read.
    QVector<BookItem> upNext() const;
    // In progress elsewhere, not on this Kobo, newest first.
    QVector<BookItem> fromOtherDevices() const;
    QVector<BookItem> recentlyAdded(const LibraryFilter &formatChips) const;
    // Local search over the whole index, or only the books on this Kobo.
    QVector<BookItem> searchLocal(const QString &query, bool onKoboOnly) const;

    // --- Covers ---
    // The cached thumbnail, or a null pixmap while it loads (coversChanged()
    // fires when new ones arrived).
    QPixmap cover(qint64 bookId);
    ui::CoverSpec coverSpec(const BookItem &b, bool withProgress);

    // --- Book detail and search ---
    // Calls back at once with the cached detail (fromCache true) if there is
    // one, then again after the API answered. `error` is None on success.
    using DetailCallback =
            std::function<void(const QJsonObject &detail, bool fromCache, ErrorKind error)>;
    void detail(qint64 bookId, DetailCallback done);
    // The server's detail when it can be reached, else the cached one (a download
    // needs the current file ID). Calls back once.
    void detailFresh(qint64 bookId, std::function<void(const QJsonObject &, ErrorKind)> done);
    void searchOnline(const QString &query,
                      std::function<void(const QVector<BookItem> &, ErrorKind)> done);

    void setOnline(bool online, ErrorKind error);
    // After the server accepted a status change for a book that isn't on this Kobo.
    void setServerStatus(qint64 bookId, ReadStatus status);

signals:
    void indexChanged();
    void coversChanged();
    void onlineChanged();

private slots:
    void emitCoversChanged();

private:
    void fetchPage(int page);
    void finishRefresh(bool ok);
    void publish(const QVector<BookItem> &items);
    void cachePage(int page, const QJsonArray &items);
    QPixmap decodeCover(const QByteArray &bytes) const;
    void ensureLocalCache() const;

    ShellDeps m_deps;
    QVector<BookItem> m_books;
    QHash<qint64, int> m_byId;
    QVector<BookItem> m_scan;
    int m_scanPages = 0;
    bool m_refreshing = false;
    bool m_online = true;
    ErrorKind m_lastError = ErrorKind::None;
    mutable bool m_localValid = false;
    mutable QHash<qint64, BookMapEntry> m_maps;
    QHash<qint64, QPixmap> m_covers;
    QList<qint64> m_coverOrder;
    QSet<qint64> m_coverPending;
    QSet<qint64> m_coverFailed;
    QTimer m_coverTimer;
};

// Applies a LibraryFilter to a list. `onKobo` says whether a book is on this
// Kobo; `status` its effective status. Pure, for tests.
QVector<BookItem> applyFilter(const QVector<BookItem> &in, const LibraryFilter &f,
                              const std::function<bool(const BookItem &)> &onKobo,
                              const std::function<ReadStatus(const BookItem &)> &status);

// Builds the series list from books (any order).
QVector<SeriesItem> buildSeries(const QVector<BookItem> &books,
                                const std::function<ReadStatus(const BookItem &)> &status);

} // namespace screens
} // namespace smalt
