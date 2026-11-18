// Acceptance: "Download resumes after a dropped connection."
//
// The mock streams fixture files (deterministic bytes), honours Range, and can
// cut a download after N body bytes. It can also ignore Range, as a server
// that doesn't support it would.
#include "bookorbit/qt_bookorbit_client.h"
#include "mock_server.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace smalt;
using testing::MockServer;

class TstDownload : public QObject
{
    Q_OBJECT

    MockServer m_server;
    QTemporaryDir m_tmp;
    std::unique_ptr<QtBookOrbitClient> m_client;
    int m_counter = 0;

    QString dest(const QString &name = QString())
    {
        return m_tmp.filePath(name.isEmpty() ? QStringLiteral("book-%1.epub").arg(++m_counter)
                                             : name);
    }

    struct Outcome
    {
        bool done = false;
        int calls = 0;
        Reply<QString> reply;
        QVector<qint64> progress;
        qint64 total = -2;
    };

    QSharedPointer<Outcome> start(qint64 fileId, const QString &path,
                                  std::function<void(RequestId)> onProgress = nullptr)
    {
        QSharedPointer<Outcome> o(new Outcome);
        auto idHolder = QSharedPointer<RequestId>::create(0);
        *idHolder = m_client->download(
                fileId, path,
                [o, onProgress, idHolder](qint64 done, qint64 total) {
                    o->progress.append(done);
                    o->total = total;
                    if (onProgress)
                        onProgress(*idHolder);
                },
                [o](const Reply<QString> &r) {
                    o->reply = r;
                    o->done = true;
                    ++o->calls;
                });
        return o;
    }

    QSharedPointer<Outcome> run(qint64 fileId, const QString &path)
    {
        QSharedPointer<Outcome> o = start(fileId, path);
        (void)QTest::qWaitFor([&] { return o->done; }, 15000);
        QTest::qWait(10);
        return o;
    }

    static QByteArray sha256Of(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(&f);
        return h.result().toHex();
    }

    QJsonObject fileInfo(int id)
    {
        return m_server.state().value("files").toObject().value(QString::number(id)).toObject();
    }

    void writeBytes(const QString &path, qint64 count, char fill)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(static_cast<int>(count), fill));
    }

private slots:

    void initTestCase()
    {
        const QString error = m_server.start();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(m_tmp.isValid());
    }

    void init()
    {
        m_server.reset();
        m_server.requests(true);
        m_client.reset(new QtBookOrbitClient(m_server.clientConfig()));
        m_client->setTimeouts(2000, 2000);
    }

    void cleanup() { m_client.reset(); }

    void fullDownloadMatchesTheServersBytes()
    {
        const QString path = dest();
        const auto o = run(102, path);
        QVERIFY2(o->reply.result.ok(), qPrintable(o->reply.result.message));
        QCOMPARE(o->calls, 1);
        QCOMPARE(o->reply.value, path);
        const QJsonObject info = fileInfo(102);
        QCOMPARE(QFileInfo(path).size(), qint64(info.value("size").toInt()));
        QCOMPARE(sha256Of(path), info.value("sha256").toString().toLatin1());
        QVERIFY(!QFile::exists(path + ".part")); // renamed into place
        // The Python mock and the C++ port agree on KOReader's digest of this file.
        QCOMPARE(partialMd5(path), info.value("hash").toString());
        QVERIFY(!m_server.requestsTo("/plugin/catalog/files/102/download")
                         .at(0)
                         .toObject()
                         .value("headers")
                         .toObject()
                         .contains("range"));
        QCOMPARE(m_server.requestsTo("/plugin/catalog/files/102/download")
                         .at(0)
                         .toObject()
                         .value("headers")
                         .toObject()
                         .value("accept-encoding")
                         .toString(),
                 QStringLiteral("identity"));
    }

    void progressIsMonotonicAndEndsAtTheFileSize()
    {
        QJsonObject slow = MockServer::rule("/plugin/catalog/files/103/download");
        slow.insert("chunk_delay_ms", 2);
        m_server.setRules({ slow });
        const QString path = dest();
        const auto o = run(103, path); // 1.2 MB: many chunks
        QVERIFY(o->reply.result.ok());
        QVERIFY(o->progress.size() > 1);
        for (int i = 1; i < o->progress.size(); ++i)
            QVERIFY(o->progress[i] >= o->progress[i - 1]);
        QCOMPARE(o->progress.last(), qint64(1200000));
        QCOMPARE(o->total, qint64(1200000));
    }

    void dropThenResumeFinishesTheFileWithoutRestarting()
    {
        QJsonObject rule = MockServer::rule("/plugin/catalog/files/103/download");
        rule.insert("drop_after_bytes", 200000);
        rule.insert("times", 1);
        m_server.setRules({ rule });

        const QString path = dest();
        const auto first = run(103, path);
        QCOMPARE(first->reply.result.error,
                 ErrorKind::Offline); // dropped connection: try again later
        QCOMPARE(first->calls, 1);
        QVERIFY(!QFile::exists(path)); // nothing half-written under the real name
        const qint64 partSize = QFileInfo(path + ".part").size();
        QVERIFY2(partSize > 0 && partSize <= 200000, qPrintable(QString::number(partSize)));

        const auto second = run(103, path);
        QVERIFY2(second->reply.result.ok(), qPrintable(second->reply.result.message));
        QCOMPARE(QFileInfo(path).size(), qint64(1200000));
        QCOMPARE(sha256Of(path), fileInfo(103).value("sha256").toString().toLatin1());
        QVERIFY(!QFile::exists(path + ".part"));

        // The second request asked only for the rest, and progress started from what was there.
        const QJsonArray log = m_server.requestsTo("/plugin/catalog/files/103/download");
        QCOMPARE(log.size(), 2);
        QVERIFY(!log.at(0).toObject().value("headers").toObject().contains("range"));
        QCOMPARE(log.at(1).toObject().value("headers").toObject().value("range").toString(),
                 QStringLiteral("bytes=%1-").arg(partSize));
        QVERIFY(second->progress.first() > partSize);
        QCOMPARE(second->progress.last(), qint64(1200000));
    }

    void severalDropsInARowStillConverge()
    {
        QJsonObject rule = MockServer::rule("/plugin/catalog/files/103/download");
        rule.insert("drop_after_bytes", 150000);
        rule.insert("times", 3);
        m_server.setRules({ rule });
        const QString path = dest();
        qint64 lastPart = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto o = run(103, path);
            QCOMPARE(o->reply.result.error, ErrorKind::Offline);
            const qint64 part = QFileInfo(path + ".part").size();
            QVERIFY2(part > lastPart, "each attempt must make progress");
            lastPart = part;
        }
        const auto o = run(103, path);
        QVERIFY2(o->reply.result.ok(), qPrintable(o->reply.result.message));
        QCOMPARE(sha256Of(path), fileInfo(103).value("sha256").toString().toLatin1());
    }

    void aServerThatIgnoresRangeRestartsTheFile()
    {
        QJsonObject cfg;
        cfg.insert("ranges", false);
        m_server.config(cfg);
        const QString path = dest();
        writeBytes(path + ".part", 5000, 'X'); // stale junk from an earlier attempt
        const auto o = run(102, path);
        QVERIFY2(o->reply.result.ok(), qPrintable(o->reply.result.message));
        // Not junk + file: the 200 told us to start over.
        QCOMPARE(QFileInfo(path).size(), qint64(150000));
        QCOMPARE(sha256Of(path), fileInfo(102).value("sha256").toString().toLatin1());
        QCOMPARE(m_server.requestsTo("/plugin/catalog/files/102/download")
                         .at(0)
                         .toObject()
                         .value("headers")
                         .toObject()
                         .value("range")
                         .toString(),
                 QStringLiteral("bytes=5000-"));
    }

    void aPartialFileLongerThanTheServersFileStartsOver()
    {
        const QString path = dest();
        writeBytes(path + ".part", 400000,
                   'Y'); // longer than file 102 (150000): the server says 416
        const auto o = run(102, path);
        QVERIFY2(o->reply.result.ok(), qPrintable(o->reply.result.message));
        QCOMPARE(sha256Of(path), fileInfo(102).value("sha256").toString().toLatin1());
        const QJsonArray log = m_server.requestsTo("/plugin/catalog/files/102/download");
        QCOMPARE(log.size(), 2);
        QCOMPARE(log.at(0).toObject().value("headers").toObject().value("range").toString(),
                 QStringLiteral("bytes=400000-"));
        QVERIFY(!log.at(1).toObject().value("headers").toObject().contains("range"));
    }

    void aWrongContentRangeIsRefused()
    {
        // A 206 that doesn't start where we asked would corrupt the file if appended.
        QJsonObject rule = MockServer::rule("/plugin/catalog/files/102/download");
        rule.insert("status", 206);
        rule.insert("raw_body", "garbage");
        m_server.setRules({ rule });
        const QString path = dest();
        writeBytes(path + ".part", 100, 'Z');
        const auto o = run(102, path);
        QCOMPARE(o->reply.result.error, ErrorKind::BadResponse);
        QCOMPARE(QFileInfo(path + ".part").size(), qint64(100)); // untouched
        QVERIFY(!QFile::exists(path));
    }

    void anExistingFileIsReplaced()
    {
        const QString path = dest();
        writeBytes(path, 10, 'O');
        const auto o = run(104, path);
        QVERIFY(o->reply.result.ok());
        QCOMPARE(QFileInfo(path).size(), qint64(20000));
    }

    void notFoundLeavesNoFile()
    {
        const QString path = dest();
        const auto o = run(999, path);
        QCOMPARE(o->reply.result.error, ErrorKind::NotFound);
        QVERIFY(!QFile::exists(path));
        QVERIFY(!QFile::exists(path + ".part"));
    }

    void serverErrorKeepsAnExistingPartialFile()
    {
        QJsonObject rule = MockServer::rule("/plugin/catalog/files/102/download");
        rule.insert("status", 500);
        m_server.setRules({ rule });
        const QString path = dest();
        writeBytes(path + ".part", 777, 'P');
        const auto o = run(102, path);
        QCOMPARE(o->reply.result.error, ErrorKind::ServerError);
        QCOMPARE(QFileInfo(path + ".part").size(), qint64(777));
    }

    void unwritableDestinationIsLocalIo()
    {
        const auto o = run(104, QStringLiteral("/proc/smalt-no-such-dir/book.epub"));
        QCOMPARE(o->reply.result.error, ErrorKind::LocalIo);
        QCOMPARE(o->calls, 1);
    }

    void destinationFolderIsCreated()
    {
        const QString path = m_tmp.filePath(QStringLiteral("Ada Quill/The Salt Road.epub"));
        const auto o = run(104, path);
        QVERIFY2(o->reply.result.ok(), qPrintable(o->reply.result.message));
        QVERIFY(QFile::exists(path));
    }

    void cancelKeepsThePartialFileForALaterResume()
    {
        QJsonObject slow = MockServer::rule("/plugin/catalog/files/103/download");
        slow.insert("chunk_delay_ms", 5);
        slow.insert("times", 1);
        m_server.setRules({ slow });
        const QString path = dest();
        QSharedPointer<Outcome> o =
                start(103, path, [this](RequestId id) { m_client->cancel(id); });
        (void)QTest::qWaitFor([&] { return o->done; }, 15000);
        QTest::qWait(20);
        QCOMPARE(o->calls, 1);
        QCOMPARE(o->reply.result.error, ErrorKind::Cancelled);
        QVERIFY(!QFile::exists(path));
        const qint64 part = QFileInfo(path + ".part").size();
        QVERIFY(part > 0 && part < 1200000);

        const auto again = run(103, path);
        QVERIFY2(again->reply.result.ok(), qPrintable(again->reply.result.message));
        QCOMPARE(sha256Of(path), fileInfo(103).value("sha256").toString().toLatin1());
    }

    void severalDownloadsAtOnce()
    {
        const QString a = dest();
        const QString b = dest();
        const QString c = dest();
        const auto oa = start(101, a);
        const auto ob = start(102, b);
        const auto oc = start(103, c);
        (void)QTest::qWaitFor([&] { return oa->done && ob->done && oc->done; }, 20000);
        QVERIFY(oa->reply.result.ok());
        QVERIFY(ob->reply.result.ok());
        QVERIFY(oc->reply.result.ok());
        QCOMPARE(sha256Of(a), fileInfo(101).value("sha256").toString().toLatin1());
        QCOMPARE(sha256Of(b), fileInfo(102).value("sha256").toString().toLatin1());
        QCOMPARE(sha256Of(c), fileInfo(103).value("sha256").toString().toLatin1());
    }

    void aStalledDownloadTimesOutAndKeepsWhatItHas()
    {
        // Headers and some bytes, then a long pause: the stall timeout fires.
        QJsonObject rule = MockServer::rule("/plugin/catalog/files/102/download");
        rule.insert("delay_ms", 3000);
        m_server.setRules({ rule });
        m_client->setTimeouts(2000, 300);
        const QString path = dest();
        const auto o = run(102, path);
        QCOMPARE(o->reply.result.error, ErrorKind::Timeout);
        QVERIFY(!QFile::exists(path));
    }

    void badArgumentsFailLocally()
    {
        auto o = run(0, dest());
        QCOMPARE(o->reply.result.error, ErrorKind::ClientError);
        o = run(101, QString());
        QCOMPARE(o->reply.result.error, ErrorKind::ClientError);
        QCOMPARE(m_server.requests().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TstDownload)
#include "tst_download.moc"
