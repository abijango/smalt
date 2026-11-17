// One HTTP exchange over Qt Network, with the timeouts and redirect rules the
// spec asks for (Sync engine, "Networking"). Internal to src/bookorbit/.
//
// Qt 5.2 has no transfer timeout (QNetworkRequest::setTransferTimeout is 5.15)
// and no redirect policy (5.9), so both are done here with QTimer and by hand.
//
// - Connect timeout: a timer that stops at the first sign of life from the
//   reply (TLS handshake done, request body going out, headers or data
//   arriving). Qt 5.2 has no "TCP connected" signal, so for a plain-HTTP GET
//   this is "time to the first byte"; over HTTPS it is exact.
// - Request timeout: 15 s from start to the end of the reply. For a download
//   (a file written to disk) it is a stall timeout instead: it restarts every
//   time data arrives, so a big file can take as long as it needs while the
//   connection is alive.
// - Redirects: GET only, up to 5, same host only (same scheme and port, or
//   http to https), as the KOReader plugin does. Credentials therefore never
//   leave the server's host.
// - Downloads: 2xx bodies are appended to a file. With resumeFrom > 0 the
//   request carries Range: bytes=N-. A 206 whose Content-Range starts at N
//   continues the file; a 200 (server ignored the range) restarts it; a 416
//   restarts it once from zero.
//
// A Transfer reports exactly once through finished(), never from inside
// start(), except for failures that are known before any I/O (it still emits
// finished(), so callers must register for it before calling start()).
#pragma once

#include "smalt/bookorbit_client.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#ifndef QT_NO_SSL
#  include <QSslConfiguration>
#endif

namespace smalt {
namespace bookorbit {

struct TransferSpec
{
    QByteArray method = "GET"; // GET, POST or PUT
    QUrl url;
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
    QString downloadPath; // Non-empty: stream 2xx bodies into this file.
    qint64 resumeFrom = 0; // Download: size of the partial file already on disk.
    int connectTimeoutMs = kConnectTimeoutMs;
    int requestTimeoutMs = kRequestTimeoutMs;
    int maxRedirects = 5;
#ifndef QT_NO_SSL
    bool hasSslConfig = false;
    QSslConfiguration ssl;
#endif
    // The URL is https but there is no usable CA bundle: fail without
    // connecting (the shipped placeholder bundle holds no certificates).
    bool failClosedTls = false;
};

struct TransferResult
{
    HttpResult http;
    QByteArray body; // Reply body (error bodies of a download too, capped).
    QByteArray contentType;
    qint64 fileBytes = 0; // Download: bytes on disk (resumed part included).
    qint64 totalBytes = -1; // Download: size of the whole file, -1 if unknown.
    bool resumed = false; // Download: a 206 continued an existing file.
};

class Transfer : public QObject
{
    Q_OBJECT
public:
    Transfer(QNetworkAccessManager *nam, const TransferSpec &spec, QObject *parent = nullptr);
    ~Transfer() override;

    void start();
    // Stops the exchange; finished() follows with ErrorKind::Cancelled.
    void abort();

    const TransferResult &result() const { return m_result; }

    bool isDone() const { return m_done; }

signals:
    // Download only: bytes on disk so far and the file's total size (-1 unknown).
    void progress(qint64 done, qint64 total);
    void finished();

private slots:
    void onReadyRead();
    void onMetaData();
    void onDownloadProgress(qint64, qint64);
    void onUploadProgress(qint64 sent, qint64 total);
    void onEncrypted();
    void onReplyFinished();
    void onConnectTimeout();
    void onRequestTimeout();

private:
    void beginAttempt();
    void markAlive();
    void drainDownload();
    bool prepareFile(int status);
    void stopAndDropReply();
    void abortWith(ErrorKind kind, const QString &message);
    void finishWith(const HttpResult &http);
    bool handleRedirect(int status);
    void restartFromZero();

    QNetworkAccessManager *m_nam;
    TransferSpec m_spec;
    QUrl m_url;
    QPointer<QNetworkReply> m_reply;
    QTimer m_connectTimer;
    QTimer m_requestTimer;
    QFile m_file;
    QByteArray m_errorBody;
    TransferResult m_result;
    qint64 m_base = 0; // Bytes already in the file when this attempt began writing.
    qint64 m_written = 0; // Bytes written by this attempt.
    qint64 m_expectedTotal = -1;
    int m_redirects = 0;
    bool m_isDownload = false;
    bool m_filePrepared = false;
    bool m_retriedFromZero = false;
    bool m_started = false;
    bool m_done = false;
};

} // namespace bookorbit
} // namespace smalt
