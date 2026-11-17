#include "bookorbit/qt_bookorbit_client.h"

#include "bookorbit/transfer.h"
#include "smalt/log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#ifndef QT_NO_SSL
#  include <QSslCertificate>
#  include <QSslSocket>
#endif

namespace smalt {

using bookorbit::Transfer;
using bookorbit::TransferResult;
using bookorbit::TransferSpec;

namespace {

// Limits from the server's request validation
// (server/src/modules/koreader/dto/koreader-plugin.dto.ts, koreader-exchange.dto.ts,
// koreader-catalog-query.dto.ts) and the plugin's own body cap (bookorbit_api.lua).
constexpr int kMaxBodyBytes = 900 * 1024; // the server's limit is 1 MiB
constexpr int kMaxMatchBooks = 500;
constexpr int kMatchTitleMaxBytes = 500;
constexpr int kMatchAuthorsMaxBytes = 1000;
constexpr int kMaxPageStatsEvents = 500;
constexpr int kMaxPageStatsBooks = 50;
constexpr int kMaxEventDurationSeconds = 86400;
constexpr int kMaxBulkProgressItems = 100;
constexpr int kMaxBookStates = 200;
constexpr int kMaxAnnotationChanges = 50;
constexpr int kMaxBookIds = 200;
constexpr int kMaxPageSize = 100;

const char *const kModule = "bookorbit";

QString lowerHex(const QString &s)
{
    return s.toLower();
}

bool validHash(const QString &h)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{32}$"));
    return re.match(h).hasMatch();
}

bool validDeviceId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,100}$"));
    return re.match(id).hasMatch();
}

bool hasLineBreak(const QString &s)
{
    return s.contains(QLatin1Char('\r')) || s.contains(QLatin1Char('\n'));
}

// Cuts to at most maxBytes of UTF-8 without splitting a character.
QString boundedUtf8(QString s, int maxBytes)
{
    while (s.toUtf8().size() > maxBytes)
        s.chop(1);
    return s;
}

QString enc(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

HttpResult makeError(ErrorKind kind, const QString &message, int status = 0)
{
    HttpResult r;
    r.error = kind;
    r.message = message;
    r.httpStatus = status;
    return r;
}

struct ParsedObject
{
    HttpResult http;
    QJsonObject obj;
};

// A reply body must be empty or one JSON object; anything else is BadResponse.
ParsedObject parseObject(const TransferResult &r)
{
    ParsedObject p;
    p.http = r.http;
    if (!p.http.ok())
        return p;
    const QByteArray body = r.body.trimmed();
    if (body.isEmpty())
        return p;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError) {
        p.http = makeError(ErrorKind::BadResponse,
                           QStringLiteral("malformed JSON: %1").arg(pe.errorString()),
                           r.http.httpStatus);
        return p;
    }
    if (!doc.isObject()) {
        p.http = makeError(ErrorKind::BadResponse, QStringLiteral("expected a JSON object"),
                           r.http.httpStatus);
        return p;
    }
    p.obj = doc.object();
    return p;
}

qint64 asInt(const QJsonValue &v)
{
    return static_cast<qint64>(v.toDouble());
}

} // namespace

// --- Construction and plumbing ---------------------------------------------------

QtBookOrbitClient::QtBookOrbitClient(const ClientConfig &config, QObject *parent)
    : QObject(parent), m_config(config), m_nam(this)
{
    configureTls();
}

QtBookOrbitClient::~QtBookOrbitClient()
{
    // Transfers are children of this object; their destructors abort the replies.
    m_active.clear();
}

void QtBookOrbitClient::setTimeouts(int connectMs, int requestMs)
{
    m_connectMs = connectMs;
    m_requestMs = requestMs;
}

void QtBookOrbitClient::setConfig(const ClientConfig &config)
{
    m_config = config;
    m_capsKnown = false;
    m_caps.clear();
    m_serverVersion.clear();
    m_libraryVersion.clear();
    configureTls();
}

void QtBookOrbitClient::configureTls()
{
    m_failClosedTls = false;
#ifndef QT_NO_SSL
    m_hasSsl = false;
    if (m_config.caBundlePath.isEmpty())
        return; // No bundle named: the system's own certificates (desktop and tests).
    QFile file(m_config.caBundlePath);
    QList<QSslCertificate> certs;
    if (file.open(QIODevice::ReadOnly))
        certs = QSslCertificate::fromData(file.readAll(), QSsl::Pem);
    if (certs.isEmpty()) {
        // The shipped placeholder holds no certificates: HTTPS fails closed.
        m_failClosedTls = true;
        log::warn(kModule,
                  QStringLiteral("CA bundle has no certificates; HTTPS requests will fail"));
        return;
    }
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setCaCertificates(certs);
    ssl.setProtocol(QSsl::SecureProtocols);
    m_ssl = ssl;
    m_hasSsl = true;
#else
    if (!m_config.caBundlePath.isEmpty())
        m_failClosedTls = true;
#endif
}

QString QtBookOrbitClient::baseUrlForTest() const
{
    QUrl base;
    QString error;
    if (!buildBaseUrl(&base, &error))
        return QString();
    base.setPath(base.path() + QLatin1Char('/'));
    return base.toString();
}

void QtBookOrbitClient::post(std::function<void()> fn)
{
    m_ready.append(std::move(fn));
    if (!m_flushScheduled) {
        m_flushScheduled = true;
        QMetaObject::invokeMethod(this, "flushReady", Qt::QueuedConnection);
    }
}

void QtBookOrbitClient::flushReady()
{
    m_flushScheduled = false;
    QVector<std::function<void()>> batch;
    batch.swap(m_ready);
    for (int i = 0; i < batch.size(); ++i)
        batch[i]();
}

bool QtBookOrbitClient::buildBaseUrl(QUrl *base, QString *error) const
{
    QUrl u = m_config.serverUrl;
    const QString scheme = u.scheme();
    if (!u.isValid() || u.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        *error = QStringLiteral("the server URL is not an http(s) address");
        return false;
    }
    if (!u.userInfo().isEmpty() || u.hasQuery() || u.hasFragment()) {
        *error = QStringLiteral("the server URL must not hold credentials, a query or a fragment");
        return false;
    }
    // Same normalisation as the plugin (bookorbit_api.lua, normalizeServerUrl).
    QString path = u.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.endsWith(QLatin1String("/api/v1/koreader")))
        path.chop(QString::fromLatin1("/api/v1/koreader").size());
    else if (path.endsWith(QLatin1String("/api/v1")))
        path.chop(QString::fromLatin1("/api/v1").size());
    u.setPath(path + QStringLiteral("/api/v1/koreader"));
    *base = u;
    return true;
}

bool QtBookOrbitClient::buildSpec(const Call &call, TransferSpec *spec, ErrorKind *kind,
                                  QString *error) const
{
    QUrl url;
    if (!buildBaseUrl(&url, error)) {
        *kind = ErrorKind::ClientError;
        return false;
    }
    if (m_config.username.isEmpty() || m_config.keyMd5Hex.isEmpty()) {
        *kind = ErrorKind::AuthRejected;
        *error = QStringLiteral("no KOReader login is configured");
        return false;
    }
    if (hasLineBreak(m_config.username)) {
        *kind = ErrorKind::ClientError;
        *error = QStringLiteral("the KOReader username holds a line break");
        return false;
    }
    url.setPath(url.path() + QLatin1Char('/') + call.path);
    if (!call.query.isEmpty()) {
        QVector<QPair<QString, QString>> sorted = call.query;
        std::sort(sorted.begin(), sorted.end(),
                  [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                      return a.first < b.first;
                  });
        QStringList parts;
        for (int i = 0; i < sorted.size(); ++i)
            parts << enc(sorted.at(i).first) + QLatin1Char('=') + enc(sorted.at(i).second);
        url.setQuery(parts.join(QLatin1Char('&')));
    }

    spec->method = call.method;
    spec->url = url;
    spec->connectTimeoutMs = m_connectMs;
    spec->requestTimeoutMs = m_requestMs;
    spec->downloadPath = call.downloadPath;
    if (!call.downloadPath.isEmpty()) {
        QFileInfo part(call.downloadPath);
        spec->resumeFrom = part.exists() ? part.size() : 0;
    }

    spec->headers.append(qMakePair(QByteArray("accept"), call.accept));
    spec->headers.append(qMakePair(QByteArray("x-auth-user"), m_config.username.toUtf8()));
    spec->headers.append(qMakePair(QByteArray("x-auth-key"), m_config.keyMd5Hex));
    spec->headers.append(qMakePair(QByteArray("User-Agent"),
                                   QByteArray("Smalt/") + m_config.pluginVersion.toLatin1()));
    if (!call.downloadPath.isEmpty())
        spec->headers.append(qMakePair(QByteArray("Accept-Encoding"), QByteArray("identity")));

    if (call.versionHeaders) {
        // Only sent when well formed, as the plugin does (getPluginVersion).
        if (validDeviceId(m_config.deviceId) && m_config.pluginVersion.size() <= 20
            && !hasLineBreak(m_config.pluginVersion)) {
            spec->headers.append(
                    qMakePair(QByteArray("x-bookorbit-device-id"), m_config.deviceId.toLatin1()));
            spec->headers.append(qMakePair(QByteArray("x-bookorbit-plugin-version"),
                                           m_config.pluginVersion.toLatin1()));
        }
    }

    if (call.hasBody) {
        QJsonObject body = call.body;
        if (call.plugin) {
            if (!validDeviceId(m_config.deviceId)) {
                *kind = ErrorKind::ClientError;
                *error = QStringLiteral(
                        "the device ID is empty or has characters BookOrbit refuses");
                return false;
            }
            body.insert(QStringLiteral("deviceId"), m_config.deviceId);
            body.insert(QStringLiteral("deviceModel"), boundedUtf8(m_config.deviceModel, 100));
            body.insert(QStringLiteral("pluginVersion"), m_config.pluginVersion.left(20));
            // Local wall clock, no time zone (VERIFY-FINDINGS; bookorbit_api.lua withDevice).
            body.insert(
                    QStringLiteral("deviceTime"),
                    QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        }
        spec->body = QJsonDocument(body).toJson(QJsonDocument::Compact);
        if (spec->body.size() > kMaxBodyBytes) {
            *kind = ErrorKind::ClientError;
            *error = QStringLiteral(
                             "request body too large (%1 bytes); send fewer items per request")
                             .arg(spec->body.size());
            return false;
        }
        spec->headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
    }

#ifndef QT_NO_SSL
    spec->hasSslConfig = m_hasSsl;
    if (m_hasSsl)
        spec->ssl = m_ssl;
#endif
    spec->failClosedTls = m_failClosedTls;
    return true;
}

// --- Request lifecycle -------------------------------------------------------------

RequestId QtBookOrbitClient::localFailure(ErrorKind kind, const QString &message,
                                          RawHandler handler)
{
    const RequestId id = m_nextId++;
    TransferResult r;
    r.http = makeError(kind, message);
    post([handler, r]() { handler(r); });
    return id;
}

RequestId QtBookOrbitClient::localSuccess(RawHandler handler)
{
    const RequestId id = m_nextId++;
    TransferResult r;
    post([handler, r]() { handler(r); });
    return id;
}

RequestId QtBookOrbitClient::submit(const Call &call, RawHandler handler)
{
    if (!call.capability.isEmpty()) {
        if (m_capsKnown && !m_caps.contains(call.capability))
            return localFailure(
                    ErrorKind::NotSupported,
                    QStringLiteral("the server does not advertise %1").arg(call.capability),
                    handler);
        if (!m_capsKnown) {
            const RequestId id = m_nextId++;
            Active a;
            a.call = call;
            a.handler = handler;
            a.gated = true;
            m_active.insert(id, a);
            m_capWaiters.append(id);
            ensureCapabilityFetch();
            return id;
        }
    }
    const RequestId id = m_nextId++;
    Active a;
    a.call = call;
    a.handler = handler;
    m_active.insert(id, a);
    launch(id);
    return id;
}

void QtBookOrbitClient::fail(RequestId id, ErrorKind kind, int httpStatus, const QString &message)
{
    auto it = m_active.find(id);
    if (it == m_active.end())
        return;
    RawHandler handler = it->handler;
    m_active.erase(it);
    TransferResult r;
    r.http = makeError(kind, message, httpStatus);
    post([handler, r]() { handler(r); });
}

void QtBookOrbitClient::launch(RequestId id)
{
    auto it = m_active.find(id);
    if (it == m_active.end())
        return;
    TransferSpec spec;
    ErrorKind kind = ErrorKind::ClientError;
    QString error;
    if (!buildSpec(it->call, &spec, &kind, &error)) {
        fail(id, kind, 0, error);
        return;
    }
    const std::function<void(qint64, qint64)> progress = it->call.progress;
    const QString path = it->call.path;
    log::debug(kModule, QStringLiteral("%1 /%2").arg(QString::fromLatin1(spec.method), path));

    Transfer *t = new Transfer(&m_nam, spec, this);
    it->transfer = t;
    connect(t, &Transfer::finished, this, [this, id]() { onTransferFinished(id); });
    if (progress)
        connect(t, &Transfer::progress, this,
                [progress](qint64 done, qint64 total) { progress(done, total); });
    t->start(); // May finish at once (fail-closed TLS); do not touch `it` after this.
}

void QtBookOrbitClient::onTransferFinished(RequestId id)
{
    auto it = m_active.find(id);
    if (it == m_active.end())
        return;
    Active a = it.value();
    m_active.erase(it);
    const TransferResult r = a.transfer->result();
    a.transfer->deleteLater();
    if (!r.http.ok() && r.http.error != ErrorKind::Cancelled)
        log::warn(kModule, QStringLiteral("/%1 failed: %2").arg(a.call.path, r.http.message));
    RawHandler handler = a.handler;
    post([handler, r]() { handler(r); });
}

void QtBookOrbitClient::cancel(RequestId id)
{
    auto it = m_active.find(id);
    if (it == m_active.end())
        return;
    if (it->gated) {
        m_capWaiters.removeAll(id);
        fail(id, ErrorKind::Cancelled, 0, QStringLiteral("cancelled"));
        return;
    }
    if (it->transfer)
        it->transfer->abort(); // finished() follows at once and queues the callback
}

void QtBookOrbitClient::cancelAll()
{
    const QList<RequestId> ids = m_active.keys();
    for (int i = 0; i < ids.size(); ++i)
        cancel(ids.at(i));
}

// --- Capabilities ---------------------------------------------------------------------

void QtBookOrbitClient::ensureCapabilityFetch()
{
    if (m_capFetch)
        return;
    Call c;
    c.path = QStringLiteral("plugin/version");
    c.versionHeaders = true;
    m_capFetch = submit(c, [this](const TransferResult &r) { onCapabilityFetched(r); });
}

QJsonObject QtBookOrbitClient::applyVersionReply(const TransferResult &r, HttpResult *http)
{
    const ParsedObject p = parseObject(r);
    *http = p.http;
    if (p.http.ok()) {
        if (!p.obj.value(QStringLiteral("capabilities")).isArray()) {
            *http = makeError(ErrorKind::BadResponse,
                              QStringLiteral("version reply has no capability list"),
                              r.http.httpStatus);
            return p.obj;
        }
        QSet<QString> caps;
        const QJsonArray list = p.obj.value(QStringLiteral("capabilities")).toArray();
        for (int i = 0; i < list.size(); ++i)
            if (list.at(i).isString())
                caps.insert(list.at(i).toString());
        m_caps = caps;
        m_serverVersion = p.obj.value(QStringLiteral("serverVersion")).toString();
        m_capsKnown = true;
    } else if (p.http.error == ErrorKind::NotFound || p.http.error == ErrorKind::ClientError) {
        // A definite answer from an older server: it has no capability list.
        m_caps.clear();
        m_serverVersion.clear();
        m_capsKnown = true;
    }
    // 5xx, timeouts and auth failures leave the capabilities unknown.
    return p.obj;
}

void QtBookOrbitClient::onCapabilityFetched(const TransferResult &r)
{
    m_capFetch = 0;
    HttpResult http;
    applyVersionReply(r, &http);

    QVector<RequestId> waiters;
    waiters.swap(m_capWaiters);
    for (int i = 0; i < waiters.size(); ++i) {
        const RequestId id = waiters.at(i);
        auto it = m_active.find(id);
        if (it == m_active.end() || !it->gated)
            continue;
        if (!m_capsKnown) {
            fail(id, http.error == ErrorKind::None ? ErrorKind::BadResponse : http.error,
                 http.httpStatus, http.message);
        } else if (m_caps.contains(it->call.capability)) {
            it->gated = false;
            launch(id);
        } else {
            fail(id, ErrorKind::NotSupported, 0,
                 QStringLiteral("the server does not advertise %1").arg(it->call.capability));
        }
    }
}

RequestId QtBookOrbitClient::capabilities(Callback<QJsonObject> done)
{
    Call c;
    c.path = QStringLiteral("plugin/version");
    c.versionHeaders = true;
    return submit(c, [this, done](const TransferResult &r) {
        Reply<QJsonObject> reply;
        reply.value = applyVersionReply(r, &reply.result);
        if (done)
            done(reply);
    });
}

bool QtBookOrbitClient::hasCapability(const QString &name) const
{
    return m_capsKnown && m_caps.contains(name);
}

// --- Handler factories -----------------------------------------------------------------

QtBookOrbitClient::RawHandler QtBookOrbitClient::objectHandler(Callback<QJsonObject> done)
{
    return [done](const TransferResult &r) {
        const ParsedObject p = parseObject(r);
        Reply<QJsonObject> reply;
        reply.result = p.http;
        reply.value = p.obj;
        if (done)
            done(reply);
    };
}

QtBookOrbitClient::RawHandler QtBookOrbitClient::boolHandler(Callback<bool> done)
{
    return [done](const TransferResult &r) {
        const ParsedObject p = parseObject(r);
        Reply<bool> reply;
        reply.result = p.http;
        reply.value = p.http.ok();
        if (done)
            done(reply);
    };
}

RequestId QtBookOrbitClient::objectCall(const Call &call, Callback<QJsonObject> done)
{
    return submit(call, objectHandler(done));
}

RequestId QtBookOrbitClient::boolCall(const Call &call, Callback<bool> done)
{
    return submit(call, boolHandler(done));
}

void QtBookOrbitClient::noteLibraryVersion(const QJsonObject &o)
{
    const QString v = o.value(QStringLiteral("libraryVersion")).toString();
    if (!v.isEmpty())
        m_libraryVersion = v;
}

// --- Session ------------------------------------------------------------------------------

RequestId QtBookOrbitClient::checkAuth(Callback<bool> done)
{
    Call c;
    c.path = QStringLiteral("users/auth");
    // kosync: {"authorized": "OK", "username": ...} (koreader.controller.ts).
    return submit(c, [done](const TransferResult &r) {
        const ParsedObject p = parseObject(r);
        Reply<bool> reply;
        reply.result = p.http;
        if (p.http.ok()) {
            if (p.obj.value(QStringLiteral("authorized")).toString() == QLatin1String("OK"))
                reply.value = true;
            else
                reply.result = makeError(ErrorKind::BadResponse,
                                         QStringLiteral("auth reply is not authorized: OK"),
                                         r.http.httpStatus);
        }
        if (done)
            done(reply);
    });
}

// --- Book identity ---------------------------------------------------------------------------

RequestId QtBookOrbitClient::matchCheck(const QVector<MatchQuery> &books,
                                        Callback<QVector<MatchResult>> done)
{
    // Wire format: MatchCheckDto (koreader-plugin.dto.ts); reply: MatchCheckResult
    // {matches: [{hash, bookId, bookFileId, positionCompatible?: false}], libraryVersion}
    // (koreader-plugin.service.ts). positionCompatible only appears as false.
    QJsonArray hashes;
    QJsonArray booksJson;
    QSet<QString> seen;
    for (int i = 0; i < books.size(); ++i) {
        const MatchQuery &q = books.at(i);
        if (!validHash(q.partialMd5))
            continue;
        const QString hash = lowerHex(q.partialMd5);
        if (seen.contains(hash))
            continue;
        seen.insert(hash);
        hashes.append(hash);
        QJsonObject b;
        b.insert(QStringLiteral("hash"), hash);
        if (!q.title.isEmpty())
            b.insert(QStringLiteral("title"), boundedUtf8(q.title, kMatchTitleMaxBytes));
        if (!q.authors.isEmpty())
            b.insert(QStringLiteral("authors"),
                     boundedUtf8(q.authors.join(QLatin1Char('\n')), kMatchAuthorsMaxBytes));
        if (q.bookFileId > 0)
            b.insert(QStringLiteral("bookFileId"), static_cast<double>(q.bookFileId));
        // The server links a hash to bookFileId only for source "file"/"current_file".
        b.insert(QStringLiteral("source"), QStringLiteral("file"));
        booksJson.append(b);
    }

    auto convert = [this, books, done](const TransferResult &r) {
        const ParsedObject p = parseObject(r);
        Reply<QVector<MatchResult>> reply;
        reply.result = p.http;
        QHash<QString, QJsonObject> matched;
        if (p.http.ok() && !r.body.trimmed().isEmpty()) {
            if (!p.obj.value(QStringLiteral("matches")).isArray()) {
                reply.result =
                        makeError(ErrorKind::BadResponse,
                                  QStringLiteral("reply has no matches list"), r.http.httpStatus);
            } else {
                const QJsonArray list = p.obj.value(QStringLiteral("matches")).toArray();
                for (int i = 0; i < list.size(); ++i) {
                    const QJsonObject m = list.at(i).toObject();
                    matched.insert(m.value(QStringLiteral("hash")).toString().toLower(), m);
                }
                noteLibraryVersion(p.obj);
            }
        }
        if (reply.result.ok()) {
            for (int i = 0; i < books.size(); ++i) {
                MatchResult res;
                res.partialMd5 = books.at(i).partialMd5;
                const auto it = matched.constFind(lowerHex(books.at(i).partialMd5));
                if (it != matched.constEnd()) {
                    res.matched = true;
                    res.bookId = asInt(it->value(QStringLiteral("bookId")));
                    res.bookFileId = asInt(it->value(QStringLiteral("bookFileId")));
                    res.positionCompatible =
                            it->value(QStringLiteral("positionCompatible")).toBool(true);
                }
                reply.value.append(res);
            }
        }
        if (done)
            done(reply);
    };

    if (hashes.size() > kMaxMatchBooks)
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("match-check takes at most %1 books per request")
                                    .arg(kMaxMatchBooks),
                            convert);
    if (hashes.isEmpty())
        return localSuccess(convert); // Nothing valid to ask about: all unmatched.

    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/match-check");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("hashes"), hashes);
    c.body.insert(QStringLiteral("books"), booksJson);
    return submit(c, convert);
}

// --- Progress ------------------------------------------------------------------------------------

RequestId QtBookOrbitClient::getProgress(const QString &document, Callback<RemoteProgress> done)
{
    // GET syncs/progress/{digest}: the stored position or {} (koreader.controller.ts).
    auto convert = [done](const TransferResult &r) {
        const ParsedObject p = parseObject(r);
        Reply<RemoteProgress> reply;
        reply.result = p.http;
        if (p.http.ok()) {
            RemoteProgress &v = reply.value;
            v.document = p.obj.value(QStringLiteral("document")).toString();
            v.percentage = p.obj.value(QStringLiteral("percentage")).toDouble();
            const QJsonValue progress = p.obj.value(QStringLiteral("progress"));
            // KOReader sends an XPointer for reflowable books and a page number for paged ones.
            v.progress = progress.isString()
                    ? progress.toString()
                    : (progress.isDouble() ? QString::number(progress.toDouble(), 'g', 15)
                                           : QString());
            v.device = p.obj.value(QStringLiteral("device")).toString();
            v.deviceId = p.obj.value(QStringLiteral("device_id")).toString();
            v.timestamp = asInt(p.obj.value(QStringLiteral("timestamp")));
        }
        if (done)
            done(reply);
    };
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9]{1,128}$"));
    if (!ok.match(document).hasMatch())
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("the document digest is malformed"), convert);
    Call c;
    c.path = QStringLiteral("syncs/progress/") + document;
    return submit(c, convert);
}

RequestId QtBookOrbitClient::putProgress(const RemoteProgress &progress, Callback<bool> done)
{
    // PUT syncs/progress, snake_case (KoreaderSaveProgressDto).
    if (progress.document.isEmpty() || !(progress.percentage >= 0.0 && progress.percentage <= 1.0))
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("progress needs a document and a percentage in 0..1"),
                            boolHandler(done));
    Call c;
    c.method = "PUT";
    c.path = QStringLiteral("syncs/progress");
    c.hasBody = true;
    c.body.insert(QStringLiteral("document"), progress.document);
    c.body.insert(QStringLiteral("percentage"), progress.percentage);
    c.body.insert(QStringLiteral("progress"), progress.progress);
    c.body.insert(QStringLiteral("device"),
                  progress.device.isEmpty() ? m_config.deviceModel : progress.device);
    c.body.insert(QStringLiteral("device_id"),
                  progress.deviceId.isEmpty() ? m_config.deviceId : progress.deviceId);
    c.body.insert(QStringLiteral("timestamp"), static_cast<double>(progress.timestamp));
    return boolCall(c, done);
}

RequestId QtBookOrbitClient::bulkProgress(const QJsonArray &items, Callback<QJsonObject> done)
{
    // POST plugin/progress {items: [{hash, percentage, progress?, timestamp?}]}
    // -> {results: [{hash, accepted}], unmatched: [hash]} (BulkProgressDto).
    if (items.size() > kMaxBulkProgressItems)
        return localFailure(
                ErrorKind::ClientError,
                QStringLiteral("bulk progress takes at most %1 items").arg(kMaxBulkProgressItems),
                objectHandler(done));
    if (items.isEmpty())
        return localSuccess(objectHandler(done));
    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/progress");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("items"), items);
    return objectCall(c, done);
}

// --- Sessions -----------------------------------------------------------------------------------------

RequestId QtBookOrbitClient::uploadPageStats(const QVector<PageStatsBook> &books,
                                             Callback<QJsonObject> done)
{
    // POST plugin/page-stats {books: [{hash, bookFileId?, events: [...]}]}
    // -> {results: [{hash, accepted, duplicates, watermark}], unmatched: [hash]}
    // (PageStatsUploadDto, koreader-stats.service.ts). At most 50 books and 500
    // events per request; the server rejects the whole request otherwise.
    QJsonArray booksJson;
    int events = 0;
    for (int i = 0; i < books.size(); ++i) {
        const PageStatsBook &b = books.at(i);
        if (b.events.isEmpty())
            continue; // The server refuses a book with no events.
        if (!validHash(b.hash))
            return localFailure(ErrorKind::ClientError,
                                QStringLiteral("page stats need a 32-character hex digest"),
                                objectHandler(done));
        QJsonArray evs;
        for (int j = 0; j < b.events.size(); ++j) {
            const PageEvent &e = b.events.at(j);
            QJsonObject o;
            o.insert(QStringLiteral("page"), e.page);
            o.insert(QStringLiteral("startTime"), static_cast<double>(e.startTime));
            o.insert(QStringLiteral("durationSeconds"),
                     qBound(0, e.durationSeconds, kMaxEventDurationSeconds));
            o.insert(QStringLiteral("totalPages"), e.totalPages);
            evs.append(o);
        }
        events += b.events.size();
        QJsonObject bj;
        bj.insert(QStringLiteral("hash"), lowerHex(b.hash));
        if (b.bookFileId > 0)
            bj.insert(QStringLiteral("bookFileId"), static_cast<double>(b.bookFileId));
        bj.insert(QStringLiteral("events"), evs);
        booksJson.append(bj);
    }
    if (booksJson.isEmpty())
        return localSuccess(objectHandler(done));
    if (events > kMaxPageStatsEvents || booksJson.size() > kMaxPageStatsBooks)
        return localFailure(
                ErrorKind::ClientError,
                QStringLiteral("page-stats takes at most %1 events and %2 books per request")
                        .arg(kMaxPageStatsEvents)
                        .arg(kMaxPageStatsBooks),
                objectHandler(done));
    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/page-stats");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("books"), booksJson);
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::sendSweep(const QJsonObject &counts, Callback<QJsonObject> done)
{
    // POST plugin/sweeps {booksMatched, pageStatsUploaded, annotationsUpserted}
    // -> {ok: true, lastSweepAt, libraryVersion} (SweepCompleteDto,
    // koreader-plugin.service.ts sweepComplete).
    auto count = [&counts](const char *key) {
        const double v = counts.value(QLatin1String(key)).toDouble(0);
        return static_cast<double>(v < 0 ? 0 : static_cast<qint64>(v));
    };
    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/sweeps");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("booksMatched"), count("booksMatched"));
    c.body.insert(QStringLiteral("pageStatsUploaded"), count("pageStatsUploaded"));
    c.body.insert(QStringLiteral("annotationsUpserted"), count("annotationsUpserted"));
    return submit(c, [this, done](const TransferResult &r) {
        const ParsedObject p = parseObject(r);
        Reply<QJsonObject> reply;
        reply.result = p.http;
        reply.value = p.obj;
        if (p.http.ok())
            noteLibraryVersion(p.obj);
        if (done)
            done(reply);
    });
}

// --- Highlights ------------------------------------------------------------------------------------------

RequestId QtBookOrbitClient::exchangeAnnotations(const QString &document,
                                                 const QJsonArray &annotations,
                                                 Callback<QJsonObject> done)
{
    // No key list: the server does no deletion detection for this call.
    return exchangeAnnotationsEx(document, annotations, QJsonArray(), false, QString(), done);
}

RequestId QtBookOrbitClient::exchangeAnnotationsEx(const QString &document,
                                                   const QJsonArray &changes,
                                                   const QJsonArray &keys, bool keysComplete,
                                                   const QString &sidecarState,
                                                   Callback<QJsonObject> done)
{
    // POST plugin/annotations/exchange {books: [{hash, keys: [{k, dt}], keysComplete,
    // sidecarState?, changes: [annotation]}]} (AnnotationExchangeDto). Reply:
    // {results: [{hash, bookId, applied, toApply: {add, edit, delete}, more,
    // skippedNoPosition}], unmatched, positionIncompatible} (koreader-annotation-exchange.service.ts).
    if (!validHash(document))
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("annotations need a 32-character hex digest"),
                            objectHandler(done));
    if (changes.size() > kMaxAnnotationChanges)
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("annotation exchange takes at most %1 changes")
                                    .arg(kMaxAnnotationChanges),
                            objectHandler(done));
    QJsonObject book;
    book.insert(QStringLiteral("hash"), lowerHex(document));
    book.insert(QStringLiteral("keys"), keys);
    book.insert(QStringLiteral("keysComplete"), keysComplete);
    if (sidecarState == QLatin1String("known") || sidecarState == QLatin1String("new"))
        book.insert(QStringLiteral("sidecarState"), sidecarState);
    book.insert(QStringLiteral("changes"), changes);
    QJsonArray books;
    books.append(book);
    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/annotations/exchange");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("books"), books);
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::ackAnnotations(const QString &document, const QJsonObject &ack,
                                            Callback<bool> done)
{
    // POST plugin/annotations/exchange-ack {books: [{hash, applied: [{serverId,
    // version, status}], deleted: [{serverId, status}]}]} (AnnotationExchangeAckDto).
    if (!validHash(document))
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("annotations need a 32-character hex digest"),
                            boolHandler(done));
    QJsonObject book;
    book.insert(QStringLiteral("hash"), lowerHex(document));
    book.insert(QStringLiteral("applied"), ack.value(QStringLiteral("applied")).toArray());
    book.insert(QStringLiteral("deleted"), ack.value(QStringLiteral("deleted")).toArray());
    QJsonArray books;
    books.append(book);
    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/annotations/exchange-ack");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("books"), books);
    return boolCall(c, done);
}

// --- Status and ratings -----------------------------------------------------------------------------------

RequestId QtBookOrbitClient::bookStates(const QJsonArray &states, Callback<QJsonObject> done)
{
    // POST plugin/book-states {books: [{hash, status?, statusModified?, rating?, ...}]}
    // -> {results: [...], unmatched: [hash]} (BookStatesUploadDto).
    if (states.size() > kMaxBookStates)
        return localFailure(
                ErrorKind::ClientError,
                QStringLiteral("book-states takes at most %1 books").arg(kMaxBookStates),
                objectHandler(done));
    if (states.isEmpty())
        return localSuccess(objectHandler(done));
    Call c;
    c.method = "POST";
    c.path = QStringLiteral("plugin/book-states");
    c.plugin = true;
    c.hasBody = true;
    c.body.insert(QStringLiteral("books"), states);
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::setReadStatus(qint64 bookId, const QString &status,
                                           Callback<bool> done)
{
    // PUT plugin/catalog/books/{id}/read-status {status} -> {readStatus}
    // (KoreaderCatalogSetReadStatusDto: unread, want_to_read, reading, on_hold, read, abandoned).
    static const QStringList allowed = { QStringLiteral("unread"),  QStringLiteral("want_to_read"),
                                         QStringLiteral("reading"), QStringLiteral("on_hold"),
                                         QStringLiteral("read"),    QStringLiteral("abandoned") };
    if (bookId <= 0 || !allowed.contains(status))
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("not a status BookOrbit accepts"), boolHandler(done));
    Call c;
    c.method = "PUT";
    c.path = QStringLiteral("plugin/catalog/books/%1/read-status").arg(bookId);
    c.hasBody = true;
    c.body.insert(QStringLiteral("status"), status);
    return boolCall(c, done);
}

RequestId QtBookOrbitClient::setRating(qint64 bookId, int rating, Callback<bool> done)
{
    // PUT plugin/catalog/books/{id}/rating {rating: 0..5 | null} (KoreaderCatalogSetRatingDto).
    // The plugin sends null to clear a rating; here 0 (or less) clears.
    if (bookId <= 0 || rating > 5)
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("a rating is 1 to 5, or 0 to clear"), boolHandler(done));
    Call c;
    c.method = "PUT";
    c.path = QStringLiteral("plugin/catalog/books/%1/rating").arg(bookId);
    c.hasBody = true;
    c.body.insert(QStringLiteral("rating"),
                  rating > 0 ? QJsonValue(rating) : QJsonValue(QJsonValue::Null));
    return boolCall(c, done);
}

// --- Catalog -------------------------------------------------------------------------------------------------

RequestId QtBookOrbitClient::catalogRoot(Callback<QJsonObject> done)
{
    Call c;
    c.path = QStringLiteral("plugin/catalog/root");
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::dashboard(Callback<QJsonObject> done)
{
    Call c;
    c.path = QStringLiteral("plugin/catalog/dashboard");
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::dashboardSection(const QString &type, Callback<QJsonObject> done)
{
    // GET plugin/catalog/dashboard/sections/{type}; only on servers advertising
    // catalogDashboardSections (older ones answer 404). "smart-scope" also needs a
    // smartScopeId the interface does not carry, so it is not offered.
    static const QStringList types = { QStringLiteral("random"), QStringLiteral("recently-added"),
                                       QStringLiteral("want-to-read"),
                                       QStringLiteral("up-next-in-series") };
    if (!types.contains(type))
        return localFailure(ErrorKind::ClientError, QStringLiteral("unknown dashboard section"),
                            objectHandler(done));
    Call c;
    c.path = QStringLiteral("plugin/catalog/dashboard/sections/") + type;
    c.capability = QStringLiteral("catalogDashboardSections");
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::section(const QString &kind, int page, Callback<QJsonObject> done)
{
    static const QStringList kinds = {
        QStringLiteral("authors"),      QStringLiteral("series"),    QStringLiteral("collections"),
        QStringLiteral("smart-scopes"), QStringLiteral("libraries"), QStringLiteral("shelves")
    };
    if (!kinds.contains(kind))
        return localFailure(ErrorKind::ClientError, QStringLiteral("unknown catalog section"),
                            objectHandler(done));
    Call c;
    c.path = QStringLiteral("plugin/catalog/sections/") + kind;
    if (page >= 1) // The server counts pages from 1.
        c.query.append(qMakePair(QStringLiteral("page"), QString::number(page)));
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::books(const BookQuery &query, Callback<QJsonObject> done)
{
    // GET plugin/catalog/books?q&page&size&sort&order&seriesId&collectionId&ids
    // (KoreaderCatalogBooksQueryDto: page from 1, size 1..100, ids up to 200).
    if (query.ids.size() > kMaxBookIds)
        return localFailure(ErrorKind::ClientError,
                            QStringLiteral("at most %1 ids per request").arg(kMaxBookIds),
                            objectHandler(done));
    Call c;
    c.path = QStringLiteral("plugin/catalog/books");
    if (!query.q.isEmpty())
        c.query.append(qMakePair(QStringLiteral("q"), query.q));
    if (query.page >= 1)
        c.query.append(qMakePair(QStringLiteral("page"), QString::number(query.page)));
    if (query.size >= 1)
        c.query.append(
                qMakePair(QStringLiteral("size"), QString::number(qMin(query.size, kMaxPageSize))));
    if (!query.sort.isEmpty())
        c.query.append(qMakePair(QStringLiteral("sort"), query.sort));
    if (!query.order.isEmpty())
        c.query.append(qMakePair(QStringLiteral("order"), query.order));
    if (query.seriesId > 0)
        c.query.append(qMakePair(QStringLiteral("seriesId"), QString::number(query.seriesId)));
    if (query.collectionId > 0)
        c.query.append(
                qMakePair(QStringLiteral("collectionId"), QString::number(query.collectionId)));
    if (!query.ids.isEmpty()) {
        QStringList ids;
        for (int i = 0; i < query.ids.size(); ++i)
            ids << QString::number(query.ids.at(i));
        c.query.append(qMakePair(QStringLiteral("ids"), ids.join(QLatin1Char(','))));
    }
    if (query.sort == QLatin1String("published"))
        c.capability = QStringLiteral("catalogPublishedSort");
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::book(qint64 bookId, Callback<QJsonObject> done)
{
    if (bookId <= 0)
        return localFailure(ErrorKind::ClientError, QStringLiteral("bad book id"),
                            objectHandler(done));
    Call c;
    c.path = QStringLiteral("plugin/catalog/books/%1").arg(bookId);
    if (validDeviceId(m_config.deviceId))
        c.query.append(qMakePair(QStringLiteral("deviceId"), m_config.deviceId));
    return objectCall(c, done);
}

RequestId QtBookOrbitClient::manifest(const QString &cursor, Callback<QJsonObject> done)
{
    // GET plugin/catalog/manifest?cursor&deviceId -> {items, hasNext, nextCursor,
    // manifestVersion, restartRequired}; only with catalogBulkManifest.
    Call c;
    c.path = QStringLiteral("plugin/catalog/manifest");
    if (!cursor.isEmpty())
        c.query.append(qMakePair(QStringLiteral("cursor"), cursor));
    if (validDeviceId(m_config.deviceId))
        c.query.append(qMakePair(QStringLiteral("deviceId"), m_config.deviceId));
    c.capability = QStringLiteral("catalogBulkManifest");
    return objectCall(c, done);
}

// --- Files -------------------------------------------------------------------------------------------------------

RequestId QtBookOrbitClient::download(qint64 fileId, const QString &destinationPath,
                                      std::function<void(qint64, qint64)> progress,
                                      Callback<QString> done)
{
    auto convert = [done, destinationPath](const TransferResult &r) {
        Reply<QString> reply;
        reply.result = r.http;
        if (reply.result.ok()) {
            const QString part = destinationPath + QStringLiteral(".part");
            // Publish the finished file. An older copy at the destination is replaced.
            if (QFile::exists(destinationPath) && !QFile::remove(destinationPath)) {
                reply.result = makeError(ErrorKind::LocalIo,
                                         QStringLiteral("cannot replace the existing file"));
            } else if (!QFile::rename(part, destinationPath)) {
                reply.result =
                        makeError(ErrorKind::LocalIo,
                                  QStringLiteral("cannot move the finished download into place"));
            } else {
                reply.value = destinationPath;
            }
        }
        if (done)
            done(reply);
    };
    if (fileId <= 0 || destinationPath.isEmpty())
        return localFailure(ErrorKind::ClientError, QStringLiteral("bad file id or destination"),
                            convert);
    // GET plugin/catalog/files/{id}/download. The data goes to <destination>.part;
    // a partial file left by an earlier attempt is continued with a Range request.
    Call c;
    c.path = QStringLiteral("plugin/catalog/files/%1/download").arg(fileId);
    c.accept = "application/octet-stream";
    c.downloadPath = destinationPath + QStringLiteral(".part");
    c.progress = progress;
    return submit(c, convert);
}

RequestId QtBookOrbitClient::thumbnail(qint64 bookId, Callback<QByteArray> done)
{
    // GET plugin/catalog/books/{id}/thumbnail: an image (bookorbit_api.lua checks image/*).
    auto convert = [done](const TransferResult &r) {
        Reply<QByteArray> reply;
        reply.result = r.http;
        if (reply.result.ok()) {
            if (!r.contentType.toLower().startsWith("image/"))
                reply.result = makeError(ErrorKind::BadResponse,
                                         QStringLiteral("thumbnail is not an image (%1)")
                                                 .arg(QString::fromLatin1(r.contentType)),
                                         r.http.httpStatus);
            else if (r.body.isEmpty())
                reply.result = makeError(ErrorKind::BadResponse, QStringLiteral("empty thumbnail"),
                                         r.http.httpStatus);
            else
                reply.value = r.body;
        }
        if (done)
            done(reply);
    };
    if (bookId <= 0)
        return localFailure(ErrorKind::ClientError, QStringLiteral("bad book id"), convert);
    Call c;
    c.path = QStringLiteral("plugin/catalog/books/%1/thumbnail").arg(bookId);
    c.accept = "image/jpeg,image/*";
    return submit(c, convert);
}

// --- Factory ---------------------------------------------------------------------------------------------------------

std::unique_ptr<BookOrbitClient> createBookOrbitClient(const ClientConfig &config)
{
    return std::unique_ptr<BookOrbitClient>(new QtBookOrbitClient(config));
}

} // namespace smalt
