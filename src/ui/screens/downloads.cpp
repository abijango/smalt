#include "downloads.h"

#include "copy.h"
#include "smalt/log.h"
#include "smalt/nickel.h"
#include "smalt/settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>

namespace smalt {
namespace screens {

namespace {

constexpr int kMaxNameLength = 100;

bool retryable(ErrorKind e)
{
    return e == ErrorKind::Offline || e == ErrorKind::Timeout || e == ErrorKind::ServerError;
}

} // namespace

DownloadManager::DownloadManager(const ShellDeps &deps, Catalog *catalog, QObject *parent)
    : QObject(parent), m_deps(deps), m_catalog(catalog)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, SIGNAL(timeout()), this, SLOT(retryCurrent()));
}

void DownloadManager::setRetry(int attempts, int delayMs)
{
    m_maxAttempts = attempts;
    m_retryDelayMs = delayMs;
}

QString DownloadManager::sanitizeFileName(const QString &name, const QString &fallback)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const ushort u = c.unicode();
        const bool bad = u < 0x20 || u == 0x7f || c == QLatin1Char('/') || c == QLatin1Char('\\')
                || c == QLatin1Char(':') || c == QLatin1Char('*') || c == QLatin1Char('?')
                || c == QLatin1Char('"') || c == QLatin1Char('<') || c == QLatin1Char('>')
                || c == QLatin1Char('|');
        out.append(bad ? QLatin1Char(' ') : c);
    }
    out = out.simplified();
    out = out.left(kMaxNameLength);
    // FAT does not keep trailing dots or spaces.
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    while (out.startsWith(QLatin1Char('.')) || out.startsWith(QLatin1Char(' ')))
        out.remove(0, 1);
    return out.isEmpty() ? fallback : out;
}

QString DownloadManager::baseFolder() const
{
    QString folder;
    if (m_deps.settings)
        folder = m_deps.settings->current().downloadFolder.trimmed();
    if (folder.isEmpty())
        return m_deps.paths.downloadsDir();
    if (QDir::isAbsolutePath(folder))
        return QDir::cleanPath(folder);
    return QDir::cleanPath(m_deps.paths.onboard() + QLatin1Char('/') + folder);
}

QString DownloadManager::destinationFor(const BookItem &book, const QString &extension) const
{
    const QString author = sanitizeFileName(book.firstAuthor(), QStringLiteral("Unknown author"));
    const QString title = sanitizeFileName(book.title, QStringLiteral("Untitled"));
    const QString ext = extension.isEmpty() ? QStringLiteral("epub") : extension.toLower();
    QString path = QStringLiteral("%1/%2/%3.%4").arg(baseFolder(), author, title, ext);
    if (m_deps.ledger) {
        BookMapEntry other;
        const QString contentId = QStringLiteral("file://") + path;
        if (m_deps.ledger->bookMapByContentId(contentId, &other).ok() && other.bookId != book.id)
            path = QStringLiteral("%1/%2/%3 (%4).%5")
                           .arg(baseFolder(), author, title)
                           .arg(book.id)
                           .arg(ext);
    }
    return path;
}

DownloadInfo DownloadManager::info(qint64 bookId) const
{
    DownloadInfo i;
    const auto it = m_entries.constFind(bookId);
    if (it == m_entries.constEnd())
        return i;
    i.state = it->state;
    i.percent = it->percent;
    i.error = it->error;
    return i;
}

QString DownloadManager::statusText() const
{
    if (!busy())
        return QString();
    return copy::downloadingOf(qMin(m_batchDone + 1, m_batchTotal), m_batchTotal);
}

QString DownloadManager::badgeText(qint64 bookId) const
{
    const DownloadInfo i = info(bookId);
    if (i.state == DownloadState::Downloading)
        return QStringLiteral("%1%").arg(i.percent);
    if (i.state == DownloadState::Queued || i.state == DownloadState::Preparing)
        return QStringLiteral("Queued");
    return QString();
}

int DownloadManager::enqueue(const QVector<BookItem> &books)
{
    int n = 0;
    for (const BookItem &b : books)
        n += add(b, false);
    if (n > 0)
        startNext();
    return n;
}

int DownloadManager::enqueue(const BookItem &book)
{
    const int n = add(book, false);
    if (n > 0)
        startNext();
    return n;
}

int DownloadManager::enqueueUpdate(const BookItem &book)
{
    const int n = add(book, true);
    if (n > 0)
        startNext();
    return n;
}

int DownloadManager::add(const BookItem &book, bool update)
{
    const DownloadState now = info(book.id).state;
    if (now == DownloadState::Queued || now == DownloadState::Preparing
        || now == DownloadState::Downloading)
        return 0;
    if (!update && m_catalog->onKobo(book.id))
        return 0;
    if (!busy()) {
        m_batchTotal = 0;
        m_batchDone = 0;
        m_note.clear();
    }
    Entry e;
    e.book = book;
    e.state = DownloadState::Queued;
    e.update = update;
    m_entries.insert(book.id, e);
    m_order.removeAll(book.id);
    m_order.append(book.id);
    ++m_queuedCount;
    ++m_batchTotal;
    emit changed();
    return 1;
}

void DownloadManager::startNext()
{
    if (m_current != 0)
        return;
    for (qint64 id : m_order) {
        if (m_entries.value(id).state == DownloadState::Queued) {
            m_current = id;
            --m_queuedCount;
            prepare(id);
            return;
        }
    }
}

void DownloadManager::prepare(qint64 bookId)
{
    Entry &e = m_entries[bookId];
    e.state = DownloadState::Preparing;
    emit changed();
    if (!m_deps.client) {
        fail(bookId, copy::downloadFailed(ErrorKind::Offline), ErrorKind::Offline);
        return;
    }
    QPointer<DownloadManager> self(this);
    m_catalog->detailFresh(bookId, [self, bookId](const QJsonObject &d, ErrorKind error) {
        if (!self || self->m_current != bookId)
            return;
        Entry &entry = self->m_entries[bookId];
        const QJsonArray files = d.value(QStringLiteral("files")).toArray();
        QJsonObject chosen;
        for (const QJsonValue &v : files) {
            const QJsonObject f = v.toObject();
            if (chosen.isEmpty()
                || f.value(QStringLiteral("role")).toString() == QLatin1String("content"))
                chosen = f;
            if (f.value(QStringLiteral("role")).toString() == QLatin1String("content"))
                break;
        }
        entry.fileId = qint64(chosen.value(QStringLiteral("id")).toDouble());
        if (entry.fileId <= 0) {
            self->fail(
                    bookId,
                    copy::downloadFailed(error == ErrorKind::None ? ErrorKind::BadResponse : error),
                    error == ErrorKind::None ? ErrorKind::BadResponse : error);
            return;
        }
        entry.extension = chosen.value(QStringLiteral("format")).toString().toLower();
        if (entry.extension.isEmpty())
            entry.extension = entry.book.extension();
        entry.destination = self->destinationFor(entry.book, entry.extension);
        self->beginTransfer(bookId);
    });
}

void DownloadManager::beginTransfer(qint64 bookId)
{
    Entry &e = m_entries[bookId];
    e.state = DownloadState::Downloading;
    e.percent = 0;
    e.lastStep = -1;
    emit changed();
    QPointer<DownloadManager> self(this);
    m_deps.client->download(
            e.fileId, e.destination,
            [self, bookId](qint64 got, qint64 total) {
                if (!self)
                    return;
                Entry &entry = self->m_entries[bookId];
                const int percent = total > 0 ? int(qMin<qint64>(100, got * 100 / total)) : 0;
                const int step = percent / 10;
                if (step == entry.lastStep)
                    return; // Redraw only every 10%.
                entry.lastStep = step;
                entry.percent = step * 10;
                emit self->changed();
            },
            [self, bookId](const Reply<QString> &reply) {
                if (self)
                    self->transferDone(bookId, reply);
            });
}

void DownloadManager::transferDone(qint64 bookId, const Reply<QString> &reply)
{
    Entry &e = m_entries[bookId];
    if (reply.result.ok()) {
        complete(bookId, reply.value);
        return;
    }
    log::info("screens",
              QStringLiteral("download of book %1 stopped: %2")
                      .arg(bookId)
                      .arg(reply.result.message));
    if (retryable(reply.result.error) && e.attempts < m_maxAttempts) {
        ++e.attempts;
        m_retryTimer.start(m_retryDelayMs);
        return;
    }
    fail(bookId, copy::downloadFailed(reply.result.error), reply.result.error);
}

void DownloadManager::retryCurrent()
{
    if (m_current == 0)
        return;
    beginTransfer(m_current); // The client continues from the .part file.
}

void DownloadManager::complete(qint64 bookId, const QString &path)
{
    Entry &e = m_entries[bookId];
    const QString md5 = partialMd5(path);
    if (md5.isEmpty()) {
        fail(bookId, copy::downloadFailed(ErrorKind::LocalIo), ErrorKind::LocalIo);
        return;
    }
    const QString contentId = QStringLiteral("file://") + QDir::cleanPath(path);
    if (m_deps.ledger) {
        BookMapEntry entry;
        entry.contentId = contentId;
        entry.bookId = bookId;
        entry.bookFileId = e.fileId;
        entry.partialMd5 = md5;
        entry.origin = BookOrigin::Downloaded;
        entry.createdUtc = QDateTime::currentDateTimeUtc();
        BookMapEntry old;
        if (m_deps.ledger->bookMapByBookId(bookId, &old).ok() && old.contentId == contentId) {
            entry.createdUtc = old.createdUtc.isValid() ? old.createdUtc : entry.createdUtc;
            // The same file again keeps what the server already has of it.
            if (old.partialMd5 == md5)
                entry.statsWatermark = old.statsWatermark;
        }
        const Status s = m_deps.ledger->putBookMap(entry);
        if (!s.ok())
            log::warn("screens", QStringLiteral("book_map not written: %1").arg(s.message));
    }
    m_catalog->invalidateLocal();
    QString note;
    if (m_deps.nickel) {
        const Status s = m_deps.nickel->importFiles(QStringList() << path);
        if (s.code == StatusCode::NotImplemented || s.code == StatusCode::Unavailable)
            note = copy::importNotReady();
        else if (!s.ok())
            log::warn("screens", QStringLiteral("import request failed: %1").arg(s.message));
    }
    if (!note.isEmpty())
        m_note = note;
    e.state = DownloadState::Done;
    e.percent = 100;
    e.error.clear();
    ++m_batchDone;
    m_current = 0;
    emit finished(bookId, contentId);
    startNext();
    endBatchIfIdle();
    emit changed();
}

void DownloadManager::fail(qint64 bookId, const QString &message, ErrorKind)
{
    Entry &e = m_entries[bookId];
    e.state = DownloadState::Failed;
    e.error = message;
    e.attempts = 0;
    ++m_batchDone;
    m_current = 0;
    emit failed(bookId, message);
    startNext();
    endBatchIfIdle();
    emit changed();
}

void DownloadManager::endBatchIfIdle()
{
    if (m_current == 0 && m_queuedCount == 0) {
        m_batchTotal = 0;
        m_batchDone = 0;
    }
}

} // namespace screens
} // namespace smalt
