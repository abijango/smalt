// HTTPS against the mock with a throwaway self-signed certificate, made with the
// openssl command at test time (nothing secret is committed). Checks that the
// client trusts exactly the certificates in Paths::caBundle()'s file.
#include "bookorbit/qt_bookorbit_client.h"
#include "mock_server.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#ifndef QT_NO_SSL
#  include <QSslSocket>
#endif

using namespace smalt;

class TstHttps : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    testing::MockServer m_server;

    bool makeCert(const QString &stem)
    {
        QProcess openssl;
        openssl.start(QStringLiteral("openssl"),
                      { QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
                        QStringLiteral("rsa:2048"), QStringLiteral("-nodes"),
                        QStringLiteral("-keyout"), m_dir.filePath(stem + ".key"),
                        QStringLiteral("-out"), m_dir.filePath(stem + ".pem"),
                        QStringLiteral("-days"), QStringLiteral("2"), QStringLiteral("-subj"),
                        QStringLiteral("/CN=127.0.0.1"), QStringLiteral("-addext"),
                        QStringLiteral("subjectAltName=IP:127.0.0.1") });
        return openssl.waitForFinished(30000) && openssl.exitStatus() == QProcess::NormalExit
                && openssl.exitCode() == 0;
    }

    Reply<bool> checkAuth(const QString &bundle)
    {
        ClientConfig c = m_server.clientConfig();
        c.serverUrl = m_server.url(QStringLiteral("https"));
        c.caBundlePath = bundle;
        QtBookOrbitClient client(c);
        client.setTimeouts(3000, 5000);
        Reply<bool> out;
        bool done = false;
        client.checkAuth([&](const Reply<bool> &r) {
            out = r;
            done = true;
        });
        (void)QTest::qWaitFor([&] { return done; }, 10000);
        return out;
    }

private slots:

    void initTestCase()
    {
#ifdef QT_NO_SSL
        QSKIP("Qt was built without SSL");
#else
        if (!QSslSocket::supportsSsl())
            QSKIP("this Qt has no working SSL library");
        QVERIFY(m_dir.isValid());
        if (!makeCert(QStringLiteral("server")) || !makeCert(QStringLiteral("other")))
            QSKIP("the openssl command is not available");
        const QString error =
                m_server.start({ QStringLiteral("--ssl-certfile"), m_dir.filePath("server.pem"),
                                 QStringLiteral("--ssl-keyfile"), m_dir.filePath("server.key") });
        QVERIFY2(error.isEmpty(), qPrintable(error));
#endif
    }

    void trustsACertificateInTheBundle()
    {
        const auto r = checkAuth(m_dir.filePath("server.pem"));
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        QVERIFY(r.value);
    }

    void aBundleThatDoesNotHoldTheServersCertificateIsRefused()
    {
        const auto r = checkAuth(m_dir.filePath("other.pem"));
        QCOMPARE(r.result.error, ErrorKind::Offline);
        QVERIFY2(r.result.message.contains("TLS"), qPrintable(r.result.message));
        QVERIFY(!r.value);
    }

    void aBundleWithSeveralCertificatesWorks()
    {
        QFile a(m_dir.filePath("server.pem"));
        QFile b(m_dir.filePath("other.pem"));
        QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        QFile both(m_dir.filePath("both.pem"));
        QVERIFY(both.open(QIODevice::WriteOnly));
        both.write(b.readAll());
        both.write(a.readAll());
        both.close();
        QVERIFY(checkAuth(m_dir.filePath("both.pem")).result.ok());
    }

    void withoutABundleTheSystemCertificatesDecideAndRefuseASelfSignedOne()
    {
        const auto r = checkAuth(QString());
        QCOMPARE(r.result.error, ErrorKind::Offline);
    }
};

QTEST_GUILESS_MAIN(TstHttps)
#include "tst_https.moc"
