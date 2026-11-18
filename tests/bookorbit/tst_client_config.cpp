// config.ini reader: format from packaging/resources/config.ini.example. Fake credentials only.
#include "bookorbit/client_config.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstClientConfig : public QObject
{
    Q_OBJECT

    QString write(QTemporaryDir &dir, const QByteArray &text)
    {
        const QString path = dir.filePath(QStringLiteral("config.ini"));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write(text);
        return path;
    }

private slots:

    void md5OfKnownPasswords()
    {
        // RFC 1321 test vectors.
        QCOMPARE(smalt::md5Hex(QString()), QByteArray("d41d8cd98f00b204e9800998ecf8427e"));
        QCOMPARE(smalt::md5Hex(QStringLiteral("abc")),
                 QByteArray("900150983cd24fb0d6963f7d28e17f72"));
    }

    void readsTheExampleShape()
    {
        QTemporaryDir dir;
        const QString path = write(dir,
                                   "; Smalt configuration.\n"
                                   "[server]\n"
                                   "url = https://books.example.com\n"
                                   "\n"
                                   "[koreader]\n"
                                   "username = fake-reader\n"
                                   "password = fake;pass,word # not a comment\n");
        smalt::ClientConfig c;
        c.deviceId = QStringLiteral("keep-me");
        const smalt::Status s = smalt::readClientConfig(path, &c);
        QVERIFY2(s.ok(), qPrintable(s.message));
        QCOMPARE(c.serverUrl.toString(), QStringLiteral("https://books.example.com"));
        QCOMPARE(c.username, QStringLiteral("fake-reader"));
        QCOMPARE(c.keyMd5Hex, smalt::md5Hex(QStringLiteral("fake;pass,word # not a comment")));
        QCOMPARE(c.deviceId, QStringLiteral("keep-me")); // other fields untouched
    }

    void toleratesBomCrlfCaseAndSpacing()
    {
        QTemporaryDir dir;
        const QString path = write(dir,
                                   "\xEF\xBB\xBF[Server]\r\n"
                                   "  URL=books.example.com:8080  \r\n"
                                   "[KOReader]\r\n"
                                   "UserName = fake\r\n"
                                   "Password=x\r\n");
        smalt::ClientConfig c;
        QVERIFY(smalt::readClientConfig(path, &c).ok());
        QCOMPARE(c.serverUrl.toString(), QStringLiteral("http://books.example.com:8080"));
        QCOMPARE(c.username, QStringLiteral("fake"));
        QCOMPARE(c.keyMd5Hex, smalt::md5Hex(QStringLiteral("x")));
    }

    void missingFile()
    {
        smalt::ClientConfig c;
        const smalt::Status s =
                smalt::readClientConfig(QStringLiteral("/nonexistent/config.ini"), &c);
        QCOMPARE(s.code, smalt::StatusCode::NotFound);
    }

    void emptyValuesAreRefusedWithoutEchoingSecrets_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QString>("mentions");
        QTest::newRow("no url") << QByteArray(
                "[server]\nurl =\n[koreader]\nusername=u\npassword=p\n")
                                << QStringLiteral("url");
        QTest::newRow("no username") << QByteArray(
                "[server]\nurl=https://a.example\n[koreader]\nusername=\npassword=p\n")
                                     << QStringLiteral("username");
        QTest::newRow("no password") << QByteArray(
                "[server]\nurl=https://a.example\n[koreader]\nusername=u\npassword=\n")
                                     << QStringLiteral("password");
        QTest::newRow("not http") << QByteArray(
                "[server]\nurl=ftp://a.example\n[koreader]\nusername=u\npassword=p\n")
                                  << QStringLiteral("url");
        QTest::newRow("untouched example")
                << QByteArray("[server]\nurl =\n[koreader]\nusername =\npassword =\n")
                << QStringLiteral("url");
    }

    void emptyValuesAreRefusedWithoutEchoingSecrets()
    {
        QFETCH(QByteArray, text);
        QFETCH(QString, mentions);
        QTemporaryDir dir;
        smalt::ClientConfig c;
        const smalt::Status s = smalt::readClientConfig(write(dir, text), &c);
        QCOMPARE(s.code, smalt::StatusCode::InvalidArgument);
        QVERIFY2(s.message.contains(mentions), qPrintable(s.message));
        QVERIFY(c.keyMd5Hex.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstClientConfig)
#include "tst_client_config.moc"
