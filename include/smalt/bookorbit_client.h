// Client for the protocol BookOrbit's own KOReader plugin uses.
//
// Spec: Decisions after verification ("BookOrbit connection"), Sync engine
// ("Networking"), Feature design ("Downloads"). Endpoint list:
// docs/VERIFY-FINDINGS.md, "BookOrbit (KOReader plugin protocol)".
// Implemented by task 11 in src/bookorbit/.
//
// - Base URL {server}/api/v1/koreader/. Headers x-auth-user (username) and
//   x-auth-key (MD5 of the KOReader password). No API key.
// - Every call is asynchronous and never blocks the calling thread. The
//   callback runs later on the thread that owns the client (Qt event loop),
//   exactly once, also on timeout or cancel.
// - Timeouts: kConnectTimeoutMs to connect, kRequestTimeoutMs per request.
// - HTTPS uses the CA bundle at Paths::caBundle(). Same-host redirects only.
//
// Response shapes the plugin protocol doesn't pin down here are passed
// through as JSON (QJsonObject/QJsonArray); task 11 may add typed wrappers.
#pragma once

#include "smalt/paths.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>
#include <memory>

namespace smalt {

constexpr int kConnectTimeoutMs = 5000; // "5 s to connect"
constexpr int kRequestTimeoutMs = 15000; // "15 s per request"

// What went wrong, so callers can decide between "retry later" and "tell the
// user". 4xx is a definite answer; 5xx and timeouts are unknown.
enum class ErrorKind {
    None,
    Offline, // No network, DNS failure, connection refused.
    Timeout, // Connect or request timeout.
    AuthRejected, // 401/403: "BookOrbit didn't accept your KOReader login."
    NotFound, // 404.
    ClientError, // Other 4xx.
    ServerError, // 5xx.
    BadResponse, // Malformed JSON or unexpected shape.
    NotSupported, // The server's capability list doesn't advertise it.
    Cancelled, // cancel() or cancelAll().
    LocalIo, // A download couldn't be written to disk.
};

struct HttpResult
{
    int httpStatus = 0; // 0 when no HTTP response arrived.
    ErrorKind error = ErrorKind::None;
    QString message; // For the log.

    bool ok() const { return error == ErrorKind::None; }
};

// Credentials and device identity, from Paths::configIni() and the ledger.
struct ClientConfig
{
    QUrl serverUrl; // Without /api/v1/koreader/.
    QString username; // x-auth-user
    QByteArray keyMd5Hex; // x-auth-key: MD5 of the KOReader password, hex.
    QString deviceId; // Stable per Kobo (ledger server row).
    QString deviceModel; // e.g. "Kobo Clara Colour".
    QString pluginVersion; // Smalt's version string.
    QString caBundlePath; // Paths::caBundle()
};

// --- Typed payloads for the calls whose shape VERIFY-FINDINGS gives ---

// One book for plugin/match-check.
struct MatchQuery
{
    QString partialMd5;
    QString title;
    QStringList authors;
    qint64 bookFileId = 0; // 0 = unknown.
};

struct MatchResult
{
    QString partialMd5;
    bool matched = false;
    qint64 bookId = 0;
    qint64 bookFileId = 0;
    bool positionCompatible = false;
};

// kosync progress (GET/PUT syncs/progress), snake_case on the wire.
struct RemoteProgress
{
    QString document; // Partial MD5.
    double percentage = 0; // 0..1 as KOReader sends it.
    QString progress; // XPointer.
    QString device;
    QString deviceId;
    qint64 timestamp = 0; // Unix seconds.
};

// One page event for plugin/page-stats.
struct PageEvent
{
    int page = 0;
    qint64 startTime = 0; // Unix seconds.
    int durationSeconds = 0;
    int totalPages = 0;
};

struct PageStatsBook
{
    QString hash; // Partial MD5.
    qint64 bookFileId = 0; // 0 = omit.
    QVector<PageEvent> events;
};

// Catalog query for plugin/catalog/books.
struct BookQuery
{
    QString q;
    int page = 0;
    int size = 0; // 0 = server default.
    QString sort;
    QString order;
    qint64 seriesId = 0;
    qint64 collectionId = 0;
    QVector<qint64> ids;
};

template <typename T>
struct Reply
{
    HttpResult result;
    T value;
};

using RequestId = quint64;

class BookOrbitClient
{
public:
    template <typename T>
    using Callback = std::function<void(const Reply<T> &)>;

    virtual ~BookOrbitClient() = default;

    virtual void setConfig(const ClientConfig &config) = 0;

    // Cancels one request or all of them; callbacks still run with Cancelled.
    virtual void cancel(RequestId id) = 0;
    virtual void cancelAll() = 0;

    // --- Session and capabilities ---
    virtual RequestId checkAuth(Callback<bool> done) = 0; // GET users/auth
    virtual RequestId
    capabilities(Callback<QJsonObject> done) = 0; // capability list + server version
    virtual bool hasCapability(const QString &name) const = 0; // From the cached list.

    // --- Book identity ---
    virtual RequestId
    matchCheck(const QVector<MatchQuery> &books,
               Callback<QVector<MatchResult>> done) = 0; // POST plugin/match-check

    // --- Progress ---
    virtual RequestId getProgress(const QString &document, Callback<RemoteProgress> done) = 0;
    virtual RequestId putProgress(const RemoteProgress &progress, Callback<bool> done) = 0;
    virtual RequestId bulkProgress(const QJsonArray &items,
                                   Callback<QJsonObject> done) = 0; // plugin/progress

    // --- Sessions ---
    // POST plugin/page-stats; the reply carries the server's watermark.
    virtual RequestId uploadPageStats(const QVector<PageStatsBook> &books,
                                      Callback<QJsonObject> done) = 0;
    // POST plugin/sweeps after every sync that uploaded sessions.
    virtual RequestId sendSweep(const QJsonObject &counts, Callback<QJsonObject> done) = 0;

    // --- Highlights (one way, Kobo to BookOrbit) ---
    virtual RequestId
    exchangeAnnotations(const QString &document, const QJsonArray &annotations,
                        Callback<QJsonObject> done) = 0; // plugin/annotations/exchange
    virtual RequestId ackAnnotations(const QString &document, const QJsonObject &ack,
                                     Callback<bool> done) = 0; // plugin/annotations/exchange-ack

    // --- Status and ratings ---
    virtual RequestId bookStates(const QJsonArray &states, Callback<QJsonObject> done) = 0;
    virtual RequestId setReadStatus(qint64 bookId, const QString &status, Callback<bool> done) = 0;
    virtual RequestId setRating(qint64 bookId, int rating, Callback<bool> done) = 0;

    // --- Catalog (browser, collections, change detection) ---
    virtual RequestId catalogRoot(Callback<QJsonObject> done) = 0;
    virtual RequestId dashboard(Callback<QJsonObject> done) = 0;
    virtual RequestId dashboardSection(const QString &type, Callback<QJsonObject> done) = 0;
    // `kind`: authors, series, collections, smart-scopes, libraries, shelves.
    virtual RequestId section(const QString &kind, int page, Callback<QJsonObject> done) = 0;
    virtual RequestId books(const BookQuery &query, Callback<QJsonObject> done) = 0;
    virtual RequestId book(qint64 bookId, Callback<QJsonObject> done) = 0;
    virtual RequestId manifest(const QString &cursor, Callback<QJsonObject> done) = 0;

    // --- Files ---
    // Downloads to `destinationPath` (via a .part file), resuming a partial
    // file after a dropped connection. `progress` gets bytes so far and total
    // (-1 if unknown); it may be empty.
    virtual RequestId download(qint64 fileId, const QString &destinationPath,
                               std::function<void(qint64, qint64)> progress,
                               Callback<QString> done) = 0;
    virtual RequestId thumbnail(qint64 bookId, Callback<QByteArray> done) = 0;
};

// Makes the Qt Network client. Defined by task 11 (src/bookorbit/).
std::unique_ptr<BookOrbitClient> createBookOrbitClient(const ClientConfig &config);

// KOReader's partial MD5 of a file (port of util.partialMD5), hex. Empty on
// read failure. Defined by task 11.
QString partialMd5(const QString &filePath);

} // namespace smalt
