// The Qt Network implementation of BookOrbitClient (include/smalt/bookorbit_client.h).
//
// Callers normally hold a std::unique_ptr<BookOrbitClient> from
// createBookOrbitClient(). This header exists for tests and for the few
// things the abstract interface does not carry (see "Beyond the interface").
//
// Threading: construct and use the client on one thread that runs a Qt event
// loop. Every callback runs on that thread from the event loop, exactly once,
// never from inside the call that started the request. Destroying the client
// aborts what is in flight and drops callbacks that have not run yet.
//
// What the protocol comes from: docs/VERIFY-FINDINGS.md, and where that is
// silent, the BookOrbit repository at commit
// c77966cc529423e993fae8a298aa77048b43bba2 (server/src/modules/koreader/ and
// koreader-plugin/bookorbit.koplugin/bookorbit_api.lua). Each call below names
// the file its wire format comes from.
#pragma once

#include "smalt/bookorbit_client.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QVector>
#ifndef QT_NO_SSL
#  include <QSslConfiguration>
#endif

#include <functional>

namespace smalt {

namespace bookorbit {
class Transfer;
struct TransferResult;
struct TransferSpec;
} // namespace bookorbit

class QtBookOrbitClient : public QObject, public BookOrbitClient
{
    Q_OBJECT
public:
    explicit QtBookOrbitClient(const ClientConfig &config, QObject *parent = nullptr);
    ~QtBookOrbitClient() override;

    // --- BookOrbitClient ---
    void setConfig(const ClientConfig &config) override;
    void cancel(RequestId id) override;
    void cancelAll() override;

    RequestId checkAuth(Callback<bool> done) override;
    RequestId capabilities(Callback<QJsonObject> done) override;
    bool hasCapability(const QString &name) const override;

    RequestId matchCheck(const QVector<MatchQuery> &books,
                         Callback<QVector<MatchResult>> done) override;

    RequestId getProgress(const QString &document, Callback<RemoteProgress> done) override;
    RequestId putProgress(const RemoteProgress &progress, Callback<bool> done) override;
    RequestId bulkProgress(const QJsonArray &items, Callback<QJsonObject> done) override;

    RequestId uploadPageStats(const QVector<PageStatsBook> &books,
                              Callback<QJsonObject> done) override;
    RequestId sendSweep(const QJsonObject &counts, Callback<QJsonObject> done) override;

    RequestId exchangeAnnotations(const QString &document, const QJsonArray &annotations,
                                  Callback<QJsonObject> done) override;
    RequestId ackAnnotations(const QString &document, const QJsonObject &ack,
                             Callback<bool> done) override;

    RequestId bookStates(const QJsonArray &states, Callback<QJsonObject> done) override;
    RequestId setReadStatus(qint64 bookId, const QString &status, Callback<bool> done) override;
    RequestId setRating(qint64 bookId, int rating, Callback<bool> done) override;

    RequestId catalogRoot(Callback<QJsonObject> done) override;
    RequestId dashboard(Callback<QJsonObject> done) override;
    RequestId dashboardSection(const QString &type, Callback<QJsonObject> done) override;
    RequestId section(const QString &kind, int page, Callback<QJsonObject> done) override;
    RequestId books(const BookQuery &query, Callback<QJsonObject> done) override;
    RequestId book(qint64 bookId, Callback<QJsonObject> done) override;
    RequestId manifest(const QString &cursor, Callback<QJsonObject> done) override;

    RequestId download(qint64 fileId, const QString &destinationPath,
                       std::function<void(qint64, qint64)> progress,
                       Callback<QString> done) override;
    RequestId thumbnail(qint64 bookId, Callback<QByteArray> done) override;

    // --- Beyond the interface (see the report's "Proposed interface changes") ---

    // True once a capability list has been fetched (or a 4xx confirmed that the
    // server has none). hasCapability() is false while this is false.
    bool capabilitiesKnown() const { return m_capsKnown; }

    QString serverVersion() const { return m_serverVersion; }

    // The newest libraryVersion seen in a match-check or sweeps reply (empty if none).
    QString libraryVersion() const { return m_libraryVersion; }

    // exchangeAnnotations() with the whole book entry the server understands:
    // `keys` is [{k, dt}] for every live highlight (server-side deletion
    // detection needs the complete list and keysComplete = true);
    // `sidecarState` is "known", "new" or empty (omitted).
    RequestId exchangeAnnotationsEx(const QString &document, const QJsonArray &changes,
                                    const QJsonArray &keys, bool keysComplete,
                                    const QString &sidecarState, Callback<QJsonObject> done);

    // Tests shorten the timeouts; production keeps kConnectTimeoutMs / kRequestTimeoutMs.
    void setTimeouts(int connectMs, int requestMs);

    // The URL a call would use, for tests: "{server}/api/v1/koreader/".
    QString baseUrlForTest() const;

private slots:
    void flushReady();

private:
    struct Call
    {
        QByteArray method = "GET";
        QString path; // After /api/v1/koreader/
        QVector<QPair<QString, QString>> query;
        QJsonObject body;
        bool hasBody = false;
        bool plugin = false; // add deviceId, deviceModel, pluginVersion, deviceTime
        bool versionHeaders = false; // x-bookorbit-device-id / -plugin-version
        QString capability; // Required server capability, empty = none.
        QByteArray accept = "application/json";
        QString downloadPath;
        std::function<void(qint64, qint64)> progress;
    };

    using RawHandler = std::function<void(const bookorbit::TransferResult &)>;

    struct Active
    {
        bookorbit::Transfer *transfer = nullptr;
        RawHandler handler;
        Call call;
        bool gated = false; // Waiting for the capability list.
    };

    RequestId submit(const Call &call, RawHandler handler);
    RequestId localFailure(ErrorKind kind, const QString &message, RawHandler handler);
    RequestId localSuccess(RawHandler handler);
    void launch(RequestId id);
    void fail(RequestId id, ErrorKind kind, int httpStatus, const QString &message);
    void onTransferFinished(RequestId id);
    void post(std::function<void()> fn);

    bool buildBaseUrl(QUrl *base, QString *error) const;
    bool buildSpec(const Call &call, bookorbit::TransferSpec *spec, ErrorKind *kind,
                   QString *error) const;
    void configureTls();

    void ensureCapabilityFetch();
    void onCapabilityFetched(const bookorbit::TransferResult &r);
    // Updates the cache from a plugin/version reply; returns the parsed object.
    QJsonObject applyVersionReply(const bookorbit::TransferResult &r, HttpResult *http);

    RawHandler objectHandler(Callback<QJsonObject> done);
    RawHandler boolHandler(Callback<bool> done);
    RequestId objectCall(const Call &call, Callback<QJsonObject> done);
    RequestId boolCall(const Call &call, Callback<bool> done);
    void noteLibraryVersion(const QJsonObject &o);

    ClientConfig m_config;
    QNetworkAccessManager m_nam;
    int m_connectMs = kConnectTimeoutMs;
    int m_requestMs = kRequestTimeoutMs;
    RequestId m_nextId = 1;
    QHash<RequestId, Active> m_active;

    QVector<RequestId> m_capWaiters;
    RequestId m_capFetch = 0;
    bool m_capsKnown = false;
    QSet<QString> m_caps;
    QString m_serverVersion;
    QString m_libraryVersion;

    QVector<std::function<void()>> m_ready;
    bool m_flushScheduled = false;

#ifndef QT_NO_SSL
    QSslConfiguration m_ssl;
    bool m_hasSsl = false;
#endif
    bool m_failClosedTls = false;
};

} // namespace smalt
