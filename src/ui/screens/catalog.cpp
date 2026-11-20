#include "catalog.h"

#include "smalt/log.h"
#include "smalt/nickel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QtAlgorithms>

#include <algorithm>

namespace smalt {
namespace screens {

namespace {

constexpr int kServerPageSize = 100;
constexpr int kMaxPages = 1000;
constexpr int kMaxCoversInMemory = 80;
constexpr int kCoverMaxHeightPx = 540;
const char *const kKindCatalog = "catalog";
const char *const kKindCover = "cover";
const char *const kKindBook = "book";

QString pageKey(int page)
{
    return QStringLiteral("catalog:books:%1").arg(page);
}

QString metaKey()
{
    return QStringLiteral("catalog:books:meta");
}

bool isComicExt(const QString &e)
{
    return e == QLatin1String("cbz") || e == QLatin1String("cbr") || e == QLatin1String("cb7");
}

bool isAudioExt(const QString &e)
{
    static const QStringList audio = QStringList()
            << QStringLiteral("m4b") << QStringLiteral("mp3") << QStringLiteral("m4a")
            << QStringLiteral("opus") << QStringLiteral("flac") << QStringLiteral("aac")
            << QStringLiteral("ogg") << QStringLiteral("audiobook");
    return audio.contains(e);
}

// An answer from the server (even a refusal) means the network works.
bool reachable(ErrorKind e)
{
    return e != ErrorKind::Offline && e != ErrorKind::Timeout && e != ErrorKind::TlsError
            && e != ErrorKind::Cancelled;
}

int compareText(const QString &a, const QString &b)
{
    return QString::compare(a, b, Qt::CaseInsensitive);
}

bool statusIsUnread(ReadStatus s)
{
    return s == ReadStatus::Unread || s == ReadStatus::WantToRead || s == ReadStatus::Unknown;
}

bool statusIsReading(ReadStatus s)
{
    return s == ReadStatus::Reading || s == ReadStatus::OnHold;
}

void sortBooks(QVector<BookItem> &v, SortOrder order)
{
    std::stable_sort(v.begin(), v.end(), [order](const BookItem &a, const BookItem &b) {
        switch (order) {
        case SortOrder::Author: {
            const int c = compareText(a.firstAuthor(), b.firstAuthor());
            if (c != 0)
                return c < 0;
            break;
        }
        case SortOrder::RecentlyAdded: {
            const QDateTime x = a.added(), y = b.added();
            if (x != y)
                return x > y;
            break;
        }
        case SortOrder::RecentlyRead: {
            const QDateTime x = a.lastRead(), y = b.lastRead();
            if (x.isValid() != y.isValid())
                return x.isValid();
            if (x != y)
                return x > y;
            break;
        }
        case SortOrder::Title:
            break;
        }
        const int c = compareText(a.title, b.title);
        if (c != 0)
            return c < 0;
        return a.id < b.id;
    });
}

QByteArray itemsToJson(const QJsonArray &items)
{
    return QJsonDocument(items).toJson(QJsonDocument::Compact);
}

} // namespace

// ---- BookItem ----

bool BookItem::isComic() const
{
    for (const QString &f : formats)
        if (isComicExt(f))
            return true;
    return false;
}

bool BookItem::isPdf() const
{
    return formats.contains(QStringLiteral("pdf")) && !isComic();
}

bool BookItem::isAudio() const
{
    if (formats.isEmpty())
        return false;
    for (const QString &f : formats)
        if (!isAudioExt(f))
            return false;
    return true;
}

QString BookItem::formatLabel() const
{
    if (isComic()) {
        for (const QString &f : formats)
            if (isComicExt(f))
                return f.toUpper();
    }
    if (isPdf())
        return QStringLiteral("PDF");
    return QString();
}

QString BookItem::extension() const
{
    if (formats.isEmpty())
        return QStringLiteral("epub");
    return formats.first();
}

QDateTime BookItem::lastRead() const
{
    return lastReadAt.isEmpty() ? QDateTime() : QDateTime::fromString(lastReadAt, Qt::ISODate);
}

QDateTime BookItem::added() const
{
    return addedAt.isEmpty() ? QDateTime() : QDateTime::fromString(addedAt, Qt::ISODate);
}

ReadStatus parseReadStatus(const QString &wire)
{
    const QString w = wire.toLower();
    if (w == QLatin1String("unread"))
        return ReadStatus::Unread;
    if (w == QLatin1String("want_to_read") || w == QLatin1String("want-to-read"))
        return ReadStatus::WantToRead;
    if (w == QLatin1String("reading"))
        return ReadStatus::Reading;
    if (w == QLatin1String("on_hold") || w == QLatin1String("on-hold"))
        return ReadStatus::OnHold;
    if (w == QLatin1String("read") || w == QLatin1String("complete")
        || w == QLatin1String("finished"))
        return ReadStatus::Read;
    if (w == QLatin1String("abandoned"))
        return ReadStatus::Abandoned;
    return ReadStatus::Unknown;
}

QString readStatusWire(ReadStatus s)
{
    switch (s) {
    case ReadStatus::Unread:
        return QStringLiteral("unread");
    case ReadStatus::WantToRead:
        return QStringLiteral("want_to_read");
    case ReadStatus::Reading:
        return QStringLiteral("reading");
    case ReadStatus::OnHold:
        return QStringLiteral("on_hold");
    case ReadStatus::Read:
        return QStringLiteral("read");
    case ReadStatus::Abandoned:
        return QStringLiteral("abandoned");
    case ReadStatus::Unknown:
        break;
    }
    return QString();
}

QString sortLabel(SortOrder s)
{
    switch (s) {
    case SortOrder::Author:
        return QStringLiteral("Author");
    case SortOrder::RecentlyAdded:
        return QStringLiteral("Recently added");
    case SortOrder::RecentlyRead:
        return QStringLiteral("Recently read");
    case SortOrder::Title:
    default:
        return QStringLiteral("Title");
    }
}

BookItem parseBookItem(const QJsonObject &o)
{
    BookItem b;
    b.id = qint64(o.value(QStringLiteral("id")).toDouble());
    b.title = o.value(QStringLiteral("title")).toString();
    const QJsonArray authors = o.value(QStringLiteral("authors")).toArray();
    for (const QJsonValue &a : authors) {
        // Plain names, or objects with a name.
        const QString name =
                a.isObject() ? a.toObject().value(QStringLiteral("name")).toString() : a.toString();
        if (!name.isEmpty())
            b.authors << name;
    }
    b.seriesId = qint64(o.value(QStringLiteral("seriesId")).toDouble());
    b.seriesName = o.value(QStringLiteral("seriesName")).toString();
    const QJsonValue idx = o.value(QStringLiteral("seriesIndex"));
    b.seriesIndex = idx.isDouble() ? idx.toDouble() : -1;
    const QJsonValue pr = o.value(QStringLiteral("progressPercentage"));
    if (pr.isDouble()) {
        // Percent (0..100) on the server; keep 0..1 here.
        const double v = pr.toDouble();
        b.progress = qBound(0.0, v > 1.0 ? v / 100.0 : v, 1.0);
    }
    b.lastReadAt = o.value(QStringLiteral("lastReadAt")).toString();
    b.status = parseReadStatus(o.value(QStringLiteral("readStatus")).toString());
    const QJsonArray formats = o.value(QStringLiteral("formats")).toArray();
    for (const QJsonValue &f : formats)
        b.formats << f.toString().toLower();
    b.addedAt = o.value(QStringLiteral("addedAt")).toString();
    b.hasCover = o.value(QStringLiteral("hasCover")).toBool(true);
    return b;
}

QVector<BookItem> applyFilter(const QVector<BookItem> &in, const LibraryFilter &f,
                              const std::function<bool(const BookItem &)> &onKobo,
                              const std::function<ReadStatus(const BookItem &)> &status)
{
    QVector<BookItem> out;
    const bool anyFormat = f.books || f.comics || f.pdfs;
    const bool anyStatus = f.unread || f.reading || f.finished;
    for (const BookItem &b : in) {
        if (anyFormat) {
            const bool ok = (f.books && !b.isComic() && !b.isPdf()) || (f.comics && b.isComic())
                    || (f.pdfs && b.isPdf());
            if (!ok)
                continue;
        }
        if (anyStatus) {
            const ReadStatus s = status(b);
            const bool ok = (f.unread && statusIsUnread(s)) || (f.reading && statusIsReading(s))
                    || (f.finished && s == ReadStatus::Read);
            if (!ok)
                continue;
        }
        if (f.onKobo && !onKobo(b))
            continue;
        out.append(b);
    }
    return out;
}

QVector<SeriesItem> buildSeries(const QVector<BookItem> &books,
                                const std::function<ReadStatus(const BookItem &)> &status)
{
    QHash<qint64, int> at;
    QVector<SeriesItem> out;
    for (const BookItem &b : books) {
        if (b.seriesId <= 0)
            continue;
        int i = at.value(b.seriesId, -1);
        if (i < 0) {
            i = out.size();
            at.insert(b.seriesId, i);
            SeriesItem s;
            s.id = b.seriesId;
            s.name = b.seriesName;
            s.allComics = true;
            out.append(s);
        }
        SeriesItem &s = out[i];
        s.books.append(b);
        if (s.name.isEmpty())
            s.name = b.seriesName;
        const ReadStatus st = status(b);
        if (st == ReadStatus::Read)
            ++s.read;
        else if (statusIsReading(st))
            ++s.reading;
        if (!b.isComic())
            s.allComics = false;
        const QDateTime lr = b.lastRead();
        if (lr.isValid() && (!s.lastRead.isValid() || lr > s.lastRead))
            s.lastRead = lr;
        const QDateTime ad = b.added();
        if (ad.isValid() && (!s.added.isValid() || ad > s.added))
            s.added = ad;
    }
    for (SeriesItem &s : out) {
        std::stable_sort(s.books.begin(), s.books.end(), [](const BookItem &a, const BookItem &b) {
            const bool ha = a.seriesIndex >= 0, hb = b.seriesIndex >= 0;
            if (ha != hb)
                return ha;
            if (a.seriesIndex != b.seriesIndex)
                return a.seriesIndex < b.seriesIndex;
            return compareText(a.title, b.title) < 0;
        });
    }
    std::stable_sort(out.begin(), out.end(), [](const SeriesItem &a, const SeriesItem &b) {
        return compareText(a.name, b.name) < 0;
    });
    return out;
}

// ---- Catalog ----

Catalog::Catalog(const ShellDeps &deps, QObject *parent) : QObject(parent), m_deps(deps)
{
    m_coverTimer.setSingleShot(true);
    m_coverTimer.setInterval(400);
    connect(&m_coverTimer, SIGNAL(timeout()), this, SLOT(emitCoversChanged()));
    if (m_deps.nickel)
        m_online = m_deps.nickel->networkConnected();
    if (!m_deps.client)
        m_online = false;
}

void Catalog::emitCoversChanged()
{
    emit coversChanged();
}

const BookItem *Catalog::find(qint64 bookId) const
{
    const int i = m_byId.value(bookId, -1);
    return i < 0 ? nullptr : &m_books.at(i);
}

void Catalog::publish(const QVector<BookItem> &items)
{
    m_books = items;
    sortBooks(m_books, SortOrder::Title);
    m_byId.clear();
    for (int i = 0; i < m_books.size(); ++i)
        m_byId.insert(m_books.at(i).id, i);
    invalidateLocal();
    emit indexChanged();
}

void Catalog::setOnline(bool online, ErrorKind error)
{
    m_lastError = error;
    if (m_online == online)
        return;
    m_online = online;
    emit onlineChanged();
}

void Catalog::setServerStatus(qint64 bookId, ReadStatus status)
{
    const int i = m_byId.value(bookId, -1);
    if (i >= 0)
        m_books[i].status = status;
}

void Catalog::loadFromCache()
{
    if (!m_deps.ledger)
        return;
    QByteArray meta;
    QDateTime when;
    if (!m_deps.ledger->cacheGet(metaKey(), &meta, &when).ok())
        return;
    const int pages = QJsonDocument::fromJson(meta).object().value(QStringLiteral("pages")).toInt();
    QVector<BookItem> all;
    for (int p = 1; p <= pages && p <= kMaxPages; ++p) {
        QByteArray bytes;
        if (!m_deps.ledger->cacheGet(pageKey(p), &bytes, &when).ok())
            continue; // A missing page is skipped; the refresh fills it.
        const QJsonArray items = QJsonDocument::fromJson(bytes).array();
        for (const QJsonValue &v : items) {
            const BookItem b = parseBookItem(v.toObject());
            if (b.id > 0 && !b.isAudio())
                all.append(b);
        }
    }
    publish(all);
}

void Catalog::refresh()
{
    if (m_refreshing || !m_deps.client)
        return;
    if (m_deps.nickel && !m_deps.nickel->networkConnected()) {
        setOnline(false, ErrorKind::Offline);
        return;
    }
    m_refreshing = true;
    m_scan.clear();
    m_scanPages = 0;
    fetchPage(1);
}

void Catalog::cachePage(int page, const QJsonArray &items)
{
    if (!m_deps.ledger)
        return;
    const Status s = m_deps.ledger->cachePut(pageKey(page), QLatin1String(kKindCatalog),
                                             itemsToJson(items), QDateTime::currentDateTimeUtc());
    if (!s.ok())
        log::warn("screens", QStringLiteral("catalog page not cached: %1").arg(s.message));
}

void Catalog::fetchPage(int page)
{
    BookQuery q;
    q.page = page;
    q.size = kServerPageSize;
    q.sort = QStringLiteral("title");
    q.order = QStringLiteral("asc");
    QPointer<Catalog> self(this);
    m_deps.client->books(q, [self, page](const Reply<QJsonObject> &r) {
        if (!self)
            return;
        if (!r.result.ok()) {
            self->m_refreshing = false;
            self->setOnline(reachable(r.result.error), r.result.error);
            log::info("screens",
                      QStringLiteral("library refresh stopped: %1").arg(r.result.message));
            return;
        }
        self->setOnline(true, ErrorKind::None);
        const QJsonArray items = r.value.value(QStringLiteral("items")).toArray();
        self->cachePage(page, items);
        for (const QJsonValue &v : items) {
            const BookItem b = parseBookItem(v.toObject());
            if (b.id > 0 && !b.isAudio())
                self->m_scan.append(b);
        }
        self->m_scanPages = page;
        const bool more = r.value.value(QStringLiteral("hasNext")).toBool() && !items.isEmpty()
                && page < kMaxPages;
        if (self->m_books.isEmpty() && !self->m_scan.isEmpty())
            self->publish(self->m_scan); // First run: show what has arrived.
        if (more) {
            self->fetchPage(page + 1);
            return;
        }
        self->finishRefresh(true);
    });
}

void Catalog::finishRefresh(bool ok)
{
    m_refreshing = false;
    if (!ok)
        return;
    if (m_deps.ledger) {
        QJsonObject meta;
        meta.insert(QStringLiteral("pages"), m_scanPages);
        meta.insert(QStringLiteral("total"), m_scan.size());
        m_deps.ledger->cachePut(metaKey(), QLatin1String(kKindCatalog),
                                QJsonDocument(meta).toJson(QJsonDocument::Compact),
                                QDateTime::currentDateTimeUtc());
    }
    publish(m_scan);
    m_scan.clear();
}

// ---- Local state ----

void Catalog::invalidateLocal()
{
    m_localValid = false;
    m_maps.clear();
}

void Catalog::ensureLocalCache() const
{
    if (m_localValid)
        return;
    m_maps.clear();
    m_localValid = true;
    if (!m_deps.ledger)
        return;
    for (const BookItem &b : m_books) {
        BookMapEntry e;
        if (m_deps.ledger->bookMapByBookId(b.id, &e).ok())
            m_maps.insert(b.id, e);
    }
}

bool Catalog::bookMap(qint64 bookId, BookMapEntry *out) const
{
    ensureLocalCache();
    const auto it = m_maps.constFind(bookId);
    if (it == m_maps.constEnd()) {
        // A book that isn't in the index (search hit) may still be on the Kobo.
        if (!m_byId.contains(bookId) && m_deps.ledger) {
            BookMapEntry e;
            if (m_deps.ledger->bookMapByBookId(bookId, &e).ok()) {
                if (out)
                    *out = e;
                return true;
            }
        }
        return false;
    }
    if (out)
        *out = it.value();
    return true;
}

bool Catalog::onKobo(qint64 bookId) const
{
    return bookMap(bookId, nullptr);
}

ReadStatus Catalog::statusOf(const BookItem &b) const
{
    BookMapEntry e;
    if (m_deps.ledger && bookMap(b.id, &e)) {
        BookStateRecord rec;
        if (m_deps.ledger->bookState(e.contentId, &rec).ok() && rec.status != ReadStatus::Unknown)
            return rec.status;
    }
    return b.status;
}

double Catalog::progressOf(const BookItem &b) const
{
    BookMapEntry e;
    if (m_deps.ledger && bookMap(b.id, &e)) {
        ProgressRecord rec;
        if (m_deps.ledger->progress(e.contentId, &rec).ok() && rec.localUpdatedUtc.isValid())
            return qBound(0.0, rec.local.percent / 100.0, 1.0);
    }
    return b.progress;
}

QDateTime Catalog::localLastRead(const BookItem &b) const
{
    BookMapEntry e;
    if (m_deps.ledger && bookMap(b.id, &e)) {
        ProgressRecord rec;
        if (m_deps.ledger->progress(e.contentId, &rec).ok() && rec.localUpdatedUtc.isValid())
            return rec.localUpdatedUtc;
    }
    return b.lastRead();
}

// ---- Derived lists ----

QVector<BookItem> Catalog::filtered(const LibraryFilter &f) const
{
    QVector<BookItem> out = applyFilter(
            m_books, f, [this](const BookItem &b) { return onKobo(b.id); },
            [this](const BookItem &b) { return statusOf(b); });
    sortBooks(out, f.sort);
    return out;
}

QVector<SeriesItem> Catalog::series() const
{
    return buildSeries(m_books, [this](const BookItem &b) { return statusOf(b); });
}

QVector<BookItem> Catalog::readingNow() const
{
    QVector<BookItem> out;
    for (const BookItem &b : m_books) {
        if (!onKobo(b.id))
            continue;
        const ReadStatus s = statusOf(b);
        const double p = progressOf(b);
        const bool inProgress = statusIsReading(s)
                || ((s == ReadStatus::Unknown || s == ReadStatus::Unread) && p > 0 && p < 1);
        if (inProgress)
            out.append(b);
    }
    std::stable_sort(out.begin(), out.end(), [this](const BookItem &a, const BookItem &b) {
        const QDateTime x = localLastRead(a), y = localLastRead(b);
        if (x.isValid() != y.isValid())
            return x.isValid();
        return x > y;
    });
    return out;
}

QVector<BookItem> Catalog::upNext() const
{
    QVector<SeriesItem> list = series();
    std::stable_sort(list.begin(), list.end(), [](const SeriesItem &a, const SeriesItem &b) {
        if (a.lastRead.isValid() != b.lastRead.isValid())
            return a.lastRead.isValid();
        return a.lastRead > b.lastRead;
    });
    QVector<BookItem> out;
    for (const SeriesItem &s : list) {
        if (s.read + s.reading == 0)
            continue; // Not a series you're reading.
        int last = -1; // Position of the furthest book read or being read.
        for (int i = 0; i < s.books.size(); ++i) {
            const ReadStatus st = statusOf(s.books.at(i));
            if (st == ReadStatus::Read || statusIsReading(st))
                last = i;
        }
        for (int i = last + 1; i < s.books.size(); ++i) {
            const ReadStatus st = statusOf(s.books.at(i));
            if (statusIsUnread(st)) {
                out.append(s.books.at(i));
                break;
            }
        }
    }
    return out;
}

QVector<BookItem> Catalog::fromOtherDevices() const
{
    QVector<BookItem> out;
    for (const BookItem &b : m_books) {
        if (onKobo(b.id))
            continue;
        const bool inProgress = statusIsReading(b.status)
                || ((b.status == ReadStatus::Unknown || b.status == ReadStatus::Unread)
                    && b.progress > 0 && b.progress < 1);
        if (inProgress)
            out.append(b);
    }
    sortBooks(out, SortOrder::RecentlyRead);
    return out;
}

QVector<BookItem> Catalog::recentlyAdded(const LibraryFilter &chips) const
{
    LibraryFilter f;
    f.books = chips.books;
    f.comics = chips.comics;
    f.pdfs = chips.pdfs;
    f.sort = SortOrder::RecentlyAdded;
    return filtered(f);
}

QVector<BookItem> Catalog::searchLocal(const QString &query, bool onKoboOnly) const
{
    QVector<BookItem> out;
    const QString q = query.trimmed();
    if (q.isEmpty())
        return out;
    for (const BookItem &b : m_books) {
        if (onKoboOnly && !onKobo(b.id))
            continue;
        if (b.title.contains(q, Qt::CaseInsensitive)
            || b.seriesName.contains(q, Qt::CaseInsensitive)
            || b.authorLine().contains(q, Qt::CaseInsensitive))
            out.append(b);
    }
    sortBooks(out, SortOrder::Title);
    return out;
}

// ---- Covers ----

QPixmap Catalog::decodeCover(const QByteArray &bytes) const
{
    QPixmap p;
    if (!p.loadFromData(bytes))
        return QPixmap();
    if (p.height() > kCoverMaxHeightPx)
        p = p.scaledToHeight(kCoverMaxHeightPx, Qt::SmoothTransformation);
    return p;
}

QPixmap Catalog::cover(qint64 bookId)
{
    const auto it = m_covers.constFind(bookId);
    if (it != m_covers.constEnd())
        return it.value();
    QPixmap pix;
    if (m_deps.ledger) {
        QByteArray bytes;
        QDateTime when;
        if (m_deps.ledger->cacheGet(QStringLiteral("cover:%1").arg(bookId), &bytes, &when).ok())
            pix = decodeCover(bytes);
    }
    if (!pix.isNull()) {
        m_covers.insert(bookId, pix);
        m_coverOrder.append(bookId);
        while (m_coverOrder.size() > kMaxCoversInMemory)
            m_covers.remove(m_coverOrder.takeFirst());
        return pix;
    }
    if (!m_deps.client || !m_online || m_coverPending.contains(bookId)
        || m_coverFailed.contains(bookId))
        return QPixmap();
    m_coverPending.insert(bookId);
    QPointer<Catalog> self(this);
    m_deps.client->thumbnail(bookId, [self, bookId](const Reply<QByteArray> &r) {
        if (!self)
            return;
        self->m_coverPending.remove(bookId);
        if (!r.result.ok()) {
            self->m_coverFailed.insert(bookId); // Not retried until the next start.
            return;
        }
        if (self->m_deps.ledger)
            self->m_deps.ledger->cachePut(QStringLiteral("cover:%1").arg(bookId),
                                          QLatin1String(kKindCover), r.value,
                                          QDateTime::currentDateTimeUtc());
        const QPixmap p = self->decodeCover(r.value);
        if (p.isNull())
            return;
        self->m_covers.insert(bookId, p);
        self->m_coverOrder.append(bookId);
        while (self->m_coverOrder.size() > kMaxCoversInMemory)
            self->m_covers.remove(self->m_coverOrder.takeFirst());
        self->m_coverTimer.start(); // One redraw for a burst of covers.
    });
    return QPixmap();
}

ui::CoverSpec Catalog::coverSpec(const BookItem &b, bool withProgress)
{
    ui::CoverSpec c;
    c.pixmap = cover(b.id);
    c.title = b.title;
    c.format = b.formatLabel();
    if (withProgress) {
        const double p = progressOf(b);
        c.progress = (p > 0 && p < 1) ? p : -1;
    }
    return c;
}

// ---- Detail and search ----

void Catalog::detail(qint64 bookId, DetailCallback done)
{
    const QString key = QStringLiteral("book:%1").arg(bookId);
    bool cached = false;
    if (m_deps.ledger) {
        QByteArray bytes;
        QDateTime when;
        if (m_deps.ledger->cacheGet(key, &bytes, &when).ok()) {
            const QJsonDocument d = QJsonDocument::fromJson(bytes);
            if (d.isObject()) {
                cached = true;
                done(d.object(), true, ErrorKind::None);
            }
        }
    }
    if (!m_deps.client)
        return;
    if (m_deps.nickel && !m_deps.nickel->networkConnected()) {
        if (!cached)
            done(QJsonObject(), false, ErrorKind::Offline);
        return;
    }
    QPointer<Catalog> self(this);
    m_deps.client->book(bookId, [self, key, done, cached](const Reply<QJsonObject> &r) {
        if (!self)
            return;
        if (!r.result.ok()) {
            self->setOnline(reachable(r.result.error), r.result.error);
            if (!cached)
                done(QJsonObject(), false, r.result.error);
            return;
        }
        self->setOnline(true, ErrorKind::None);
        if (self->m_deps.ledger)
            self->m_deps.ledger->cachePut(key, QLatin1String(kKindBook),
                                          QJsonDocument(r.value).toJson(QJsonDocument::Compact),
                                          QDateTime::currentDateTimeUtc());
        done(r.value, false, ErrorKind::None);
    });
}

void Catalog::detailFresh(qint64 bookId, std::function<void(const QJsonObject &, ErrorKind)> done)
{
    const QString key = QStringLiteral("book:%1").arg(bookId);
    QJsonObject cached;
    if (m_deps.ledger) {
        QByteArray bytes;
        QDateTime when;
        if (m_deps.ledger->cacheGet(key, &bytes, &when).ok())
            cached = QJsonDocument::fromJson(bytes).object();
    }
    const bool networkUp = m_deps.client && (!m_deps.nickel || m_deps.nickel->networkConnected());
    if (!networkUp) {
        done(cached, cached.isEmpty() ? ErrorKind::Offline : ErrorKind::None);
        return;
    }
    QPointer<Catalog> self(this);
    m_deps.client->book(bookId, [self, key, cached, done](const Reply<QJsonObject> &r) {
        if (!self)
            return;
        if (!r.result.ok()) {
            self->setOnline(reachable(r.result.error), r.result.error);
            done(cached, cached.isEmpty() ? r.result.error : ErrorKind::None);
            return;
        }
        self->setOnline(true, ErrorKind::None);
        if (self->m_deps.ledger)
            self->m_deps.ledger->cachePut(key, QLatin1String(kKindBook),
                                          QJsonDocument(r.value).toJson(QJsonDocument::Compact),
                                          QDateTime::currentDateTimeUtc());
        done(r.value, ErrorKind::None);
    });
}

void Catalog::searchOnline(const QString &query,
                           std::function<void(const QVector<BookItem> &, ErrorKind)> done)
{
    if (!m_deps.client) {
        done(QVector<BookItem>(), ErrorKind::Offline);
        return;
    }
    BookQuery q;
    q.q = query;
    q.page = 1;
    q.size = 50;
    q.sort = QStringLiteral("title");
    QPointer<Catalog> self(this);
    m_deps.client->books(q, [self, done](const Reply<QJsonObject> &r) {
        if (!self)
            return;
        QVector<BookItem> out;
        if (r.result.ok()) {
            self->setOnline(true, ErrorKind::None);
            const QJsonArray items = r.value.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &v : items) {
                const BookItem b = parseBookItem(v.toObject());
                if (b.id > 0 && !b.isAudio())
                    out.append(b);
            }
        } else {
            self->setOnline(reachable(r.result.error), r.result.error);
        }
        done(out, r.result.error);
    });
}

} // namespace screens
} // namespace smalt
