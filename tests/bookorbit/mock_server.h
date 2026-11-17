// Starts tools/mock-bookorbit as a child process on a free port for one test
// program, and drives it through its /__mock/ control endpoints.
//
// Needs python3 with fastapi and uvicorn (tools/mock-bookorbit/requirements.txt).
// Set SMALT_PYTHON to use a particular interpreter.
#pragma once

#include "smalt/bookorbit_client.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace testing {

// The fake credentials the mock accepts (tools/mock-bookorbit/mock_bookorbit/fixtures.py).
inline QString mockUser()
{
    return QStringLiteral("test-reader");
}

inline QString mockPassword()
{
    return QStringLiteral("fake-password");
}

class MockServer
{
public:
    MockServer() = default;

    ~MockServer() { stop(); }

    MockServer(const MockServer &) = delete;
    MockServer &operator=(const MockServer &) = delete;

    // Returns an empty string on success, otherwise what went wrong.
    QString start(const QStringList &extraArgs = QStringList())
    {
        QString python = qEnvironmentVariable("SMALT_PYTHON");
        if (python.isEmpty())
            python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            return QStringLiteral("python3 not found (set SMALT_PYTHON)");

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONPATH"), QStringLiteral(SMALT_MOCK_DIR));
        m_proc.setProcessEnvironment(env);
        m_proc.setWorkingDirectory(QStringLiteral(SMALT_MOCK_DIR));
        m_proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QStringList args;
        args << QStringLiteral("-m") << QStringLiteral("mock_bookorbit") << QStringLiteral("--port")
             << QStringLiteral("0") << extraArgs;
        m_proc.start(python, args);
        if (!m_proc.waitForStarted(10000))
            return QStringLiteral("could not start %1").arg(python);

        QByteArray out;
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.start(20000);
        while (deadline.isActive()) {
            if (!m_proc.waitForReadyRead(200) && m_proc.state() != QProcess::Running)
                break;
            out += m_proc.readAllStandardOutput();
            const int at = out.indexOf("listening ");
            if (at >= 0 && out.indexOf('\n', at) > 0) {
                m_port = out.mid(at + 10, out.indexOf('\n', at) - at - 10).trimmed().toInt();
                break;
            }
        }
        if (m_port <= 0) {
            stop();
            return QStringLiteral(
                    "the mock did not report a port; is fastapi/uvicorn installed? "
                    "(python3 -m pip install -r tools/mock-bookorbit/requirements.txt)");
        }
        return QString();
    }

    void stop()
    {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.terminate();
            if (!m_proc.waitForFinished(3000)) {
                m_proc.kill();
                m_proc.waitForFinished(3000);
            }
        }
        m_port = 0;
    }

    int port() const { return m_port; }

    QUrl url(const QString &scheme = QStringLiteral("http")) const
    {
        return QUrl(QStringLiteral("%1://127.0.0.1:%2").arg(scheme).arg(m_port));
    }

    smalt::ClientConfig clientConfig() const
    {
        smalt::ClientConfig c;
        c.serverUrl = url();
        c.username = mockUser();
        c.keyMd5Hex =
                QCryptographicHash::hash(mockPassword().toUtf8(), QCryptographicHash::Md5).toHex();
        c.deviceId = QStringLiteral("smalt-test-device-1");
        c.deviceModel = QStringLiteral("Kobo Test");
        c.pluginVersion = QStringLiteral("0.1.0");
        return c;
    }

    // --- Control endpoints ---
    void reset() { request("POST", "/__mock/reset", QByteArray("{}")); }

    void config(const QJsonObject &o)
    {
        request("POST", "/__mock/config", QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

    void setRules(const QJsonArray &rules)
    {
        QJsonObject o;
        o.insert(QStringLiteral("rules"), rules);
        config(o);
    }

    void clearRules() { setRules(QJsonArray()); }

    QJsonArray requests(bool clear = false)
    {
        return QJsonDocument::fromJson(
                       request("GET", clear ? "/__mock/requests?clear=1" : "/__mock/requests"))
                .array();
    }

    // Only the requests whose path is `path`.
    QJsonArray requestsTo(const QString &path)
    {
        QJsonArray out;
        const QJsonArray all = requests();
        for (int i = 0; i < all.size(); ++i)
            if (all.at(i).toObject().value(QStringLiteral("path")).toString() == path)
                out.append(all.at(i));
        return out;
    }

    QJsonObject state()
    {
        return QJsonDocument::fromJson(request("GET", "/__mock/state")).object();
    }

    static QJsonObject rule(const QString &path, const QString &method = QString())
    {
        QJsonObject r;
        r.insert(QStringLiteral("path"), path);
        if (!method.isEmpty())
            r.insert(QStringLiteral("method"), method);
        return r;
    }

private:
    QByteArray request(const char *method, const QString &path,
                       const QByteArray &body = QByteArray())
    {
        QNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *reply = QByteArray(method) == "GET" ? nam.get(req) : nam.post(req, body);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, SLOT(quit()));
        if (!reply->isFinished())
            loop.exec();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        return data;
    }

    QProcess m_proc;
    int m_port = 0;
};

} // namespace testing
