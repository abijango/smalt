#include "smalt/page_stats.h"

#include "smalt/log.h"

#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <algorithm>

namespace smalt {

namespace {

const char *const kModule = "bookorbit";
constexpr int kMaxEventDurationSeconds = 86400;

bool eventLess(const PageEvent &a, const PageEvent &b)
{
    if (a.startTime != b.startTime)
        return a.startTime < b.startTime;
    return a.page < b.page;
}

class UploadJob : public QObject
{
    Q_OBJECT
public:
    UploadJob(BookOrbitClient *client, const SessionUploadOptions &options,
              std::function<void(const QString &, qint64)> onWatermark,
              std::function<void(const SessionUploadResult &)> done, QObject *parent)
        : QObject(parent),
          m_client(client),
          m_options(options),
          m_onWatermark(std::move(onWatermark)),
          m_done(std::move(done))
    {
    }

    void begin(const QVector<SessionUploadBook> &input)
    {
        for (int i = 0; i < input.size(); ++i) {
            const SessionUploadBook &in = input.at(i);
            BookState b;
            b.hash = in.hash.toLower();
            b.fileId = in.bookFileId;
            b.watermark = in.watermark;
            int atOrBelowWatermark = 0;
            for (int j = 0; j < in.events.size(); ++j) {
                PageEvent e = in.events.at(j);
                if (e.startTime < 1 || e.totalPages < 1 || e.page < 0) {
                    ++m_result.eventsSkippedInvalid;
                    continue;
                }
                if (e.startTime <= in.watermark) {
                    // Acknowledged already, or the device clock went backwards and
                    // these sessions will never upload: report either way.
                    ++atOrBelowWatermark;
                    continue;
                }
                e.durationSeconds = qBound(0, e.durationSeconds, kMaxEventDurationSeconds);
                b.pending.append(e);
            }
            if (atOrBelowWatermark > 0) {
                m_result.eventsSkippedAtOrBelowWatermark += atOrBelowWatermark;
                log::warn(kModule,
                          QStringLiteral("book %1: skipped %2 page events at or below the "
                                         "watermark %3 (already uploaded, or the device clock "
                                         "went backwards)")
                                  .arg(b.hash)
                                  .arg(atOrBelowWatermark)
                                  .arg(in.watermark));
            }
            std::stable_sort(b.pending.begin(), b.pending.end(), eventLess);
            if (!b.pending.isEmpty())
                m_books.append(b);
        }
        if (m_result.eventsSkippedInvalid > 0)
            log::warn(kModule,
                      QStringLiteral("skipped %1 page events the server would refuse")
                              .arg(m_result.eventsSkippedInvalid));
        if (m_books.isEmpty()) {
            m_result.complete = true;
            QTimer::singleShot(0, this, SLOT(finishNow()));
            return;
        }
        step();
    }

private slots:

    void finishNow() { finish(); }

private:
    struct BookState
    {
        QString hash;
        qint64 fileId = 0;
        qint64 watermark = 0;
        QVector<PageEvent> pending;
        int next = 0;
        bool dropped = false;
    };

    struct Slice
    {
        int book = 0;
        int count = 0;
        qint64 lastStart = 0;
    };

    // A 4xx that says the data itself is wrong. 401/403 (AuthRejected) and 404
    // (NotFound) have their own kinds; 408 and 429 are "try again later".
    static bool refusedForGood(const HttpResult &r)
    {
        return r.error == ErrorKind::ClientError && r.httpStatus != 408 && r.httpStatus != 429;
    }

    bool bookDone(const BookState &b) const { return b.dropped || b.next >= b.pending.size(); }

    void step()
    {
        QVector<PageStatsBook> request;
        QVector<Slice> slices;
        int capacity = m_options.maxEventsPerRequest;
        for (int i = 0; i < m_books.size() && capacity > 0; ++i) {
            const BookState &b = m_books.at(i);
            if (bookDone(b))
                continue;
            if (request.size() >= m_options.maxBooksPerRequest
                || (m_oneAtATime && !request.isEmpty()))
                break;
            const int remaining = b.pending.size() - b.next;
            int n = qMin(remaining, capacity);
            if (n < remaining) {
                // Don't cut a group of events that share a startTime.
                int cut = n;
                while (cut > 0
                       && b.pending.at(b.next + cut - 1).startTime
                               == b.pending.at(b.next + cut).startTime)
                    --cut;
                if (cut > 0)
                    n = cut;
            }
            PageStatsBook out;
            out.hash = b.hash;
            out.bookFileId = b.fileId;
            for (int j = 0; j < n; ++j)
                out.events.append(b.pending.at(b.next + j));
            request.append(out);
            Slice s;
            s.book = i;
            s.count = n;
            s.lastStart = b.pending.at(b.next + n - 1).startTime;
            slices.append(s);
            capacity -= n;
        }
        if (request.isEmpty()) {
            afterRun();
            return;
        }
        ++m_result.requests;
        m_client->uploadPageStats(request, [this, slices](const Reply<QJsonObject> &reply) {
            onUploaded(reply, slices);
        });
    }

    void onUploaded(const Reply<QJsonObject> &reply, const QVector<Slice> &slices)
    {
        if (refusedForGood(reply.result)) {
            if (slices.size() > 1) {
                // One bad book must not hold back the others: retry one book at a time.
                m_oneAtATime = true;
                step();
                return;
            }
            const Slice &s = slices.at(0);
            BookState &b = m_books[s.book];
            b.dropped = true;
            m_result.rejected.insert(b.hash, s.lastStart);
            log::warn(kModule,
                      QStringLiteral("book %1: the server refused its page events for good (%2); "
                                     "they will be skipped")
                              .arg(b.hash)
                              .arg(reply.result.message));
            step();
            return;
        }
        if (!reply.result.ok()) {
            m_result.result = reply.result;
            afterRun();
            return;
        }
        QHash<QString, QJsonObject> results;
        const QJsonArray list = reply.value.value(QStringLiteral("results")).toArray();
        for (int i = 0; i < list.size(); ++i) {
            const QJsonObject o = list.at(i).toObject();
            results.insert(o.value(QStringLiteral("hash")).toString().toLower(), o);
        }
        QSet<QString> unmatched;
        const QJsonArray un = reply.value.value(QStringLiteral("unmatched")).toArray();
        for (int i = 0; i < un.size(); ++i)
            unmatched.insert(un.at(i).toString().toLower());

        for (int i = 0; i < slices.size(); ++i) {
            const Slice &s = slices.at(i);
            BookState &b = m_books[s.book];
            if (unmatched.contains(b.hash)) {
                b.dropped = true;
                m_result.unmatched << b.hash;
                continue;
            }
            const auto it = results.constFind(b.hash);
            if (it == results.constEnd()) {
                // Neither acknowledged nor unmatched: don't move anything for this book.
                m_result.result.error = ErrorKind::BadResponse;
                m_result.result.message =
                        QStringLiteral("page-stats reply has no entry for a book we sent");
                continue;
            }
            const qint64 wm =
                    static_cast<qint64>(it->value(QStringLiteral("watermark")).toDouble());
            const qint64 advanced = (wm > 0 && wm <= s.lastStart) ? wm : s.lastStart;
            b.next += s.count;
            b.watermark = qMax(b.watermark, advanced);
            m_result.watermarks.insert(b.hash, b.watermark);
            m_result.eventsSent += s.count;
            m_result.eventsAccepted += it->value(QStringLiteral("accepted")).toInt();
            m_result.eventsDuplicate += it->value(QStringLiteral("duplicates")).toInt();
            m_acked.insert(b.hash);
            if (m_onWatermark)
                m_onWatermark(b.hash, b.watermark);
        }
        if (!m_result.result.ok()) {
            afterRun();
            return;
        }
        step();
    }

    void afterRun()
    {
        m_result.booksAcknowledged = m_acked.size();
        bool allDone = true;
        for (int i = 0; i < m_books.size(); ++i)
            allDone = allDone && bookDone(m_books.at(i));
        m_result.complete = m_result.result.ok() && allDone;

        if (m_result.eventsSent > 0 && m_options.sendSweep && m_result.complete) {
            QJsonObject counts;
            counts.insert(QStringLiteral("booksMatched"), m_result.booksAcknowledged);
            counts.insert(QStringLiteral("pageStatsUploaded"), m_result.eventsSent);
            counts.insert(QStringLiteral("annotationsUpserted"), m_options.annotationsUpserted);
            m_client->sendSweep(counts, [this](const Reply<QJsonObject> &reply) {
                m_result.sweepResult = reply.result;
                m_result.sweepReply = reply.value;
                m_result.sweepSent = reply.result.ok();
                finish();
            });
            return;
        }
        finish();
    }

    void finish()
    {
        m_result.sweepOwed = m_result.eventsSent > 0 && m_options.sendSweep && !m_result.sweepSent;
        if (m_done)
            m_done(m_result);
        deleteLater();
    }

    BookOrbitClient *m_client;
    SessionUploadOptions m_options;
    std::function<void(const QString &, qint64)> m_onWatermark;
    std::function<void(const SessionUploadResult &)> m_done;
    QVector<BookState> m_books;
    QSet<QString> m_acked;
    bool m_oneAtATime = false; // After a refused multi-book request.
    SessionUploadResult m_result;
};

} // namespace

void uploadSessions(BookOrbitClient *client, const QVector<SessionUploadBook> &books,
                    const SessionUploadOptions &options,
                    std::function<void(const QString &hash, qint64 watermark)> onWatermark,
                    std::function<void(const SessionUploadResult &)> done)
{
    // Child of the client when it is a QObject, so destroying the client cleans up.
    QObject *parent = dynamic_cast<QObject *>(client);
    UploadJob *job =
            new UploadJob(client, options, std::move(onWatermark), std::move(done), parent);
    job->begin(books);
}

} // namespace smalt

#include "page_stats_uploader.moc"
