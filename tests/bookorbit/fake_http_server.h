// A scriptable one-thread HTTP server on a QTcpServer, for what the mock can't
// do: connections that die before any response, servers that accept and never
// answer, redirects to other hosts. Runs on the test's event loop, so a test
// must be waiting in QTRY_* (or similar) for it to answer.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVector>

#include <functional>

namespace testing {

class FakeHttpServer
{
public:
    struct Request
    {
        QByteArray method;
        QByteArray target;
        QMap<QByteArray, QByteArray> headers; // lower-case names
        QByteArray body;
    };

    using Handler = std::function<void(QTcpSocket *, const Request &)>;

    FakeHttpServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                attach(socket);
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }

    quint16 port() const { return m_server.serverPort(); }

    QUrl url(const QString &host = QStringLiteral("127.0.0.1")) const
    {
        return QUrl(QStringLiteral("http://%1:%2").arg(host).arg(port()));
    }

    void setHandler(Handler handler) { m_handler = std::move(handler); }

    int connections() const { return m_connections; }

    QVector<Request> requests; // Every complete request seen.

    static void reply(QTcpSocket *socket, int status, const QByteArray &body,
                      const QByteArray &extraHeaders = QByteArray(),
                      const QByteArray &contentType = "application/json")
    {
        QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
        out += "Content-Type: " + contentType + "\r\n";
        out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        out += "Connection: close\r\n" + extraHeaders + "\r\n" + body;
        socket->write(out);
        socket->disconnectFromHost();
    }

private:
    void attach(QTcpSocket *socket)
    {
        ++m_connections;
        QSharedPointer<QByteArray> buffer(new QByteArray);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const int end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            Request r;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            r.method = first.value(0);
            r.target = first.value(1);
            for (int i = 1; i < lines.size(); ++i) {
                const int colon = lines.at(i).indexOf(':');
                if (colon > 0)
                    r.headers.insert(lines.at(i).left(colon).trimmed().toLower(),
                                     lines.at(i).mid(colon + 1).trimmed());
            }
            const int length = r.headers.value("content-length").toInt();
            if (buffer->size() < end + 4 + length)
                return; // wait for the rest of the body
            r.body = buffer->mid(end + 4, length);
            buffer->clear();
            requests.append(r);
            if (m_handler)
                m_handler(socket, r);
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    QTcpServer m_server;
    Handler m_handler;
    int m_connections = 0;
};

} // namespace testing
