// The download flow (SPEC, "Downloads"): queue several books, fetch each in its
// own format, save to <folder>/<Author>/<Title>.<ext>, record the BookOrbit IDs
// and file hash in book_map, ask Nickel to import the file, then offer Open.
//
// One book is fetched at a time (gentle on the Wi-Fi and the Kobo's CPU). A
// dropped connection is retried a couple of times; the client continues from
// the .part file with a Range request, so nothing is fetched twice. Progress is
// static text that changes in 10% steps (E Ink: no spinners, few redraws).
#pragma once

#include "catalog.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

namespace smalt {
namespace screens {

enum class DownloadState { None, Queued, Preparing, Downloading, Done, Failed };

struct DownloadInfo
{
    DownloadState state = DownloadState::None;
    int percent = 0;
    QString error; // For Failed: the text to show.
};

class DownloadManager : public QObject
{
    Q_OBJECT

public:
    DownloadManager(const ShellDeps &deps, Catalog *catalog, QObject *parent);

    // Adds books to the queue; books already queued, running or on the Kobo
    // (and up to date) are skipped. Returns how many were added.
    int enqueue(const QVector<BookItem> &books);
    int enqueue(const BookItem &book);
    // Re-fetches a book that is already on the Kobo ("Update available").
    int enqueueUpdate(const BookItem &book);

    DownloadInfo info(qint64 bookId) const;

    bool busy() const { return m_current != 0 || m_queuedCount > 0; }

    // "Downloading 3 of 5" while a batch runs, else empty.
    QString statusText() const;
    // Live value for a list badge: "42%" while fetching, "Queued" while waiting.
    QString badgeText(qint64 bookId) const;

    // The message to show after the last book finished, if there is one (for
    // example that Nickel can't import yet). Empty when everything went well.
    QString lastNote() const { return m_note; }

    // Retries after a dropped connection (default 2, 2 seconds apart).
    void setRetry(int attempts, int delayMs);

    // <folder>/<Author>/<Title>.<ext> for a book, with safe file names. A name
    // that already belongs to another BookOrbit book gets " (<id>)" added.
    QString destinationFor(const BookItem &book, const QString &extension) const;

    static QString sanitizeFileName(const QString &name, const QString &fallback);

signals:
    void changed();
    void finished(qint64 bookId, const QString &contentId);
    void failed(qint64 bookId, const QString &message);

private slots:
    void retryCurrent();

private:
    struct Entry
    {
        BookItem book;
        DownloadState state = DownloadState::None;
        int percent = 0;
        int lastStep = -1;
        int attempts = 0;
        bool update = false;
        qint64 fileId = 0;
        QString extension;
        QString destination;
        QString error;
    };

    int add(const BookItem &book, bool update);
    void startNext();
    void prepare(qint64 bookId);
    void beginTransfer(qint64 bookId);
    void transferDone(qint64 bookId, const Reply<QString> &reply);
    void complete(qint64 bookId, const QString &path);
    void fail(qint64 bookId, const QString &message, ErrorKind error);
    void endBatchIfIdle();
    QString baseFolder() const;

    ShellDeps m_deps;
    Catalog *m_catalog;
    QHash<qint64, Entry> m_entries;
    QList<qint64> m_order;
    qint64 m_current = 0;
    int m_queuedCount = 0;
    int m_batchTotal = 0;
    int m_batchDone = 0;
    int m_maxAttempts = 2;
    int m_retryDelayMs = 2000;
    QString m_note;
    QTimer m_retryTimer;
};

} // namespace screens
} // namespace smalt
