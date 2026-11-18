#include "bookorbit/transfer.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace smalt {
namespace bookorbit {

namespace {

constexpr int kMaxErrorBodyBytes = 64 * 1024;

bool isRedirectStatus(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

int portOf(const QUrl &url)
{
    return url.port(url.scheme() == QLatin1String("https") ? 443 : 80);
}

HttpResult makeResult(ErrorKind kind, int status, const QString &message)
{
    HttpResult r;
    r.error = kind;
    r.httpStatus = status;
    r.message = message;
    return r;
}

// 4xx is a definite answer, 5xx and timeouts are "unknown" (Spec: Networking).
HttpResult classify(int status, QNetworkReply *reply)
{
    const QNetworkReply::NetworkError err = reply->error();
    if (status >= 200 && status < 300 && err == QNetworkReply::NoError) {
        HttpResult ok;
        ok.httpStatus = status;
        return ok;
    }
    const QString detail = QStringLiteral("HTTP %1 (%2)").arg(status).arg(reply->errorString());
    if (status >= 500)
        return makeResult(ErrorKind::ServerError, status, detail);
    if (status == 401 || status == 403)
        return makeResult(ErrorKind::AuthRejected, status, detail);
    if (status == 404)
        return makeResult(ErrorKind::NotFound, status, detail);
    if (status >= 400)
        return makeResult(ErrorKind::ClientError, status, detail);
    if (status >= 300)
        return makeResult(ErrorKind::BadResponse, status,
                          QStringLiteral("unexpected %1").arg(detail));

    // No usable HTTP answer (or the body broke off after a 2xx): a network failure.
    const QString what = reply->errorString();
    switch (err) {
    case QNetworkReply::TimeoutError:
        return makeResult(ErrorKind::Timeout, status, what);
    case QNetworkReply::SslHandshakeFailedError:
        return makeResult(ErrorKind::TlsError, status,
                          QStringLiteral("TLS handshake failed: ") + what);
    case QNetworkReply::NoError:
        return makeResult(ErrorKind::Offline, status, QStringLiteral("no HTTP response"));
    default:
        return makeResult(ErrorKind::Offline, status, what);
    }
}

} // namespace

Transfer::Transfer(QNetworkAccessManager *nam, const TransferSpec &spec, QObject *parent)
    : QObject(parent),
      m_nam(nam),
      m_spec(spec),
      m_url(spec.url),
      m_isDownload(!spec.downloadPath.isEmpty())
{
    m_connectTimer.setSingleShot(true);
    m_requestTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, &Transfer::onConnectTimeout);
    connect(&m_requestTimer, &QTimer::timeout, this, &Transfer::onRequestTimeout);
}

Transfer::~Transfer()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file.isOpen())
        m_file.close();
}

void Transfer::start()
{
    if (m_started)
        return;
    m_started = true;
    if (m_spec.failClosedTls && m_url.scheme() == QLatin1String("https")) {
        finishWith(makeResult(ErrorKind::TlsError, 0,
                              QStringLiteral("TLS: the CA bundle holds no certificates")));
        return;
    }
    beginAttempt();
}

void Transfer::abort()
{
    abortWith(ErrorKind::Cancelled, QStringLiteral("cancelled"));
}

void Transfer::beginAttempt()
{
    m_filePrepared = false;
    m_written = 0;
    m_base = 0;
    m_expectedTotal = -1;
    m_errorBody.clear();

    QNetworkRequest request(m_url);
    for (int i = 0; i < m_spec.headers.size(); ++i)
        request.setRawHeader(m_spec.headers.at(i).first, m_spec.headers.at(i).second);
    if (m_isDownload && m_spec.resumeFrom > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_spec.resumeFrom) + "-");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
#ifndef QT_NO_SSL
    if (m_spec.hasSslConfig && m_url.scheme() == QLatin1String("https"))
        request.setSslConfiguration(m_spec.ssl);
#endif

    QNetworkReply *reply = nullptr;
    if (m_spec.method == "POST")
        reply = m_nam->post(request, m_spec.body);
    else if (m_spec.method == "PUT")
        reply = m_nam->put(request, m_spec.body);
    else
        reply = m_nam->get(request);
    m_reply = reply;

    connect(reply, &QIODevice::readyRead, this, &Transfer::onReadyRead);
    connect(reply, &QNetworkReply::metaDataChanged, this, &Transfer::onMetaData);
    connect(reply, &QNetworkReply::downloadProgress, this, &Transfer::onDownloadProgress);
    connect(reply, &QNetworkReply::uploadProgress, this, &Transfer::onUploadProgress);
#ifndef QT_NO_SSL
    connect(reply, &QNetworkReply::encrypted, this, &Transfer::onEncrypted);
#endif
    connect(reply, &QNetworkReply::finished, this, &Transfer::onReplyFinished);

    m_connectTimer.start(m_spec.connectTimeoutMs);
    m_requestTimer.start(m_spec.requestTimeoutMs);
}

void Transfer::markAlive()
{
    m_connectTimer.stop();
    if (m_isDownload && m_requestTimer.isActive())
        m_requestTimer.start(m_spec.requestTimeoutMs); // stall timeout: restart on data
}

void Transfer::onMetaData()
{
    markAlive();
}

void Transfer::onDownloadProgress(qint64, qint64)
{
    markAlive();
}

void Transfer::onUploadProgress(qint64 sent, qint64)
{
    if (sent > 0)
        markAlive();
}

void Transfer::onEncrypted()
{
    m_connectTimer.stop();
}

void Transfer::onReadyRead()
{
    markAlive();
    if (m_isDownload)
        drainDownload();
}

void Transfer::onConnectTimeout()
{
    abortWith(ErrorKind::Timeout, QStringLiteral("connect timeout"));
}

void Transfer::onRequestTimeout()
{
    abortWith(ErrorKind::Timeout,
              m_isDownload ? QStringLiteral("download stalled")
                           : QStringLiteral("request timeout"));
}

bool Transfer::prepareFile(int status)
{
    if (m_filePrepared)
        return true;
    const QString path = m_spec.downloadPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);

    if (status == 206) {
        static const QRegularExpression range(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+|\\*)$"));
        const QString header = QString::fromLatin1(m_reply->rawHeader("Content-Range")).trimmed();
        const QRegularExpressionMatch m = range.match(header);
        if (!m.hasMatch() || m.captured(1).toLongLong() != m_spec.resumeFrom) {
            abortWith(ErrorKind::BadResponse,
                      QStringLiteral("206 with unexpected Content-Range '%1'").arg(header));
            return false;
        }
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            abortWith(ErrorKind::LocalIo,
                      QStringLiteral("cannot open %1: %2").arg(path, m_file.errorString()));
            return false;
        }
        if (m_file.size() != m_spec.resumeFrom) {
            m_file.close();
            abortWith(ErrorKind::LocalIo, QStringLiteral("partial file changed while resuming"));
            return false;
        }
        m_base = m_spec.resumeFrom;
        const QString total = m.captured(3);
        m_expectedTotal = total == QLatin1String("*") ? -1 : total.toLongLong();
        m_result.resumed = true;
    } else {
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            abortWith(ErrorKind::LocalIo,
                      QStringLiteral("cannot open %1: %2").arg(path, m_file.errorString()));
            return false;
        }
        m_base = 0;
        const QByteArray length = m_reply->rawHeader("Content-Length");
        bool ok = false;
        const qint64 n = length.trimmed().toLongLong(&ok);
        m_expectedTotal = ok ? n : -1;
        m_result.resumed = false;
    }
    m_filePrepared = true;
    return true;
}

void Transfer::drainDownload()
{
    if (m_done || !m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 200 || status == 206) {
        if (!prepareFile(status))
            return;
        const QByteArray chunk = m_reply->readAll();
        if (chunk.isEmpty())
            return;
        const qint64 n = m_file.write(chunk);
        if (n != chunk.size()) {
            abortWith(ErrorKind::LocalIo,
                      QStringLiteral("write failed: %1").arg(m_file.errorString()));
            return;
        }
        m_written += n;
        emit progress(m_base + m_written, m_expectedTotal);
    } else if (status >= 300 || status == 0) {
        // Not a body we keep: hold the start of it for the log, drop the rest.
        const QByteArray chunk = m_reply->readAll();
        if (m_errorBody.size() < kMaxErrorBodyBytes)
            m_errorBody += chunk.left(kMaxErrorBodyBytes - m_errorBody.size());
    }
}

void Transfer::stopAndDropReply()
{
    m_connectTimer.stop();
    m_requestTimer.stop();
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void Transfer::abortWith(ErrorKind kind, const QString &message)
{
    if (m_done)
        return;
    const int status =
            m_reply ? m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    stopAndDropReply();
    finishWith(makeResult(kind, status, message));
}

void Transfer::finishWith(const HttpResult &http)
{
    if (m_done)
        return;
    m_done = true;
    m_connectTimer.stop();
    m_requestTimer.stop();
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply.clear();
        reply->disconnect(this);
        reply->deleteLater();
    }
    m_result.http = http;
    if (m_isDownload) {
        m_result.fileBytes = m_filePrepared ? m_base + m_written : m_spec.resumeFrom;
        m_result.totalBytes = m_expectedTotal;
    }
    emit finished();
}

bool Transfer::handleRedirect(int status)
{
    if (m_spec.method != "GET") {
        finishWith(makeResult(
                ErrorKind::BadResponse, status,
                QStringLiteral("unexpected redirect (HTTP %1) to a non-GET request").arg(status)));
        return false;
    }
    if (m_redirects >= m_spec.maxRedirects) {
        finishWith(
                makeResult(ErrorKind::BadResponse, status, QStringLiteral("too many redirects")));
        return false;
    }
    const QByteArray location = m_reply->rawHeader("Location").trimmed();
    if (location.isEmpty()) {
        finishWith(makeResult(ErrorKind::BadResponse, status,
                              QStringLiteral("redirect without Location")));
        return false;
    }
    const QUrl target = m_url.resolved(QUrl::fromEncoded(location, QUrl::TolerantMode));
    const bool sameHost = target.host().compare(m_spec.url.host(), Qt::CaseInsensitive) == 0;
    const bool sameOrigin = target.scheme() == m_url.scheme() && portOf(target) == portOf(m_url);
    const bool upgrade =
            m_url.scheme() == QLatin1String("http") && target.scheme() == QLatin1String("https");
    if (!sameHost || !(sameOrigin || upgrade)) {
        finishWith(makeResult(ErrorKind::BadResponse, status,
                              QStringLiteral("refused redirect to another host or scheme (%1)")
                                      .arg(target.host())));
        return false;
    }
    stopAndDropReply();
    m_url = target;
    ++m_redirects;
    beginAttempt();
    return true;
}

void Transfer::restartFromZero()
{
    stopAndDropReply();
    m_retriedFromZero = true;
    m_spec.resumeFrom = 0;
    QFile::remove(m_spec.downloadPath);
    beginAttempt();
}

void Transfer::onReplyFinished()
{
    if (m_done || !m_reply || sender() != m_reply.data())
        return;
    m_connectTimer.stop();
    m_requestTimer.stop();

    QNetworkReply *reply = m_reply;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_result.contentType = reply->rawHeader("Content-Type");

    if (isRedirectStatus(status)) {
        handleRedirect(status);
        return;
    }
    if (m_isDownload && status == 416 && m_spec.resumeFrom > 0 && !m_retriedFromZero) {
        restartFromZero();
        return;
    }

    if (m_isDownload) {
        drainDownload();
        if (m_done) // drainDownload failed the transfer
            return;
        if (status != 200 && status != 206)
            m_result.body = m_errorBody;
    } else {
        m_result.body = reply->readAll();
    }

    HttpResult http = classify(status, reply);
    if (http.ok() && m_isDownload) {
        const qint64 have = m_filePrepared ? m_base + m_written : 0;
        if (have == 0) {
            http = makeResult(ErrorKind::BadResponse, status, QStringLiteral("empty download"));
        } else if (m_expectedTotal >= 0 && have != m_expectedTotal) {
            http = makeResult(ErrorKind::Offline, status,
                              QStringLiteral("incomplete download: %1 of %2 bytes")
                                      .arg(have)
                                      .arg(m_expectedTotal));
        }
    }
    finishWith(http);
}

} // namespace bookorbit
} // namespace smalt
