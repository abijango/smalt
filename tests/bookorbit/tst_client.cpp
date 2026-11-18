// Acceptance: "Every call against the mock: success, timeout, 401, 500,
// malformed JSON." and "No call blocks the calling thread.", plus the typed
// payloads, capability gating, redirects, cancellation and network failures.
#include "bookorbit/qt_bookorbit_client.h"
#include "fake_http_server.h"
#include "mock_server.h"

#include <QJsonDocument>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QtTest>

using namespace smalt;
using testing::FakeHttpServer;
using testing::MockServer;

namespace {

using Sink = std::function<void(const HttpResult &)>;

template <typename T>
BookOrbitClient::Callback<T> sink(Sink cb)
{
    return [cb](const Reply<T> &r) { cb(r.result); };
}

struct CallCase
{
    QString name;
    QString path; // The mock path a rule matches (after /api/v1/koreader).
    bool malformedApplies = true;
    std::function<RequestId(QtBookOrbitClient &, Sink)> issue;
};

QJsonObject sampleAnnotation()
{
    QJsonObject a;
    a.insert("datetime", "2026-11-16 10:00:00");
    a.insert("drawer", "lighten");
    a.insert("color", "yellow");
    a.insert("text", "the salt road");
    a.insert("note", "remember");
    a.insert("chapter", "One");
    a.insert("posFormat", "xpointer");
    a.insert("pos0", "/body/DocFragment[3]/body/p[2]/text().10");
    a.insert("pos1", "/body/DocFragment[3]/body/p[2]/text().23");
    return a;
}

QJsonObject statusRule(const QString &path, int status, const QString &method = QString())
{
    QJsonObject r = MockServer::rule(path, method);
    r.insert("status", status);
    return r;
}

} // namespace

class TstClient : public QObject
{
    Q_OBJECT

    MockServer m_server;
    QTemporaryDir m_tmp;
    QString m_hash; // Partial MD5 of fixture file 101 (book 1).
    std::unique_ptr<QtBookOrbitClient> m_client;
    int m_calls = 0; // Callbacks seen by the last await().
    bool m_sync = false; // A callback ran inside the call that started it.
    int m_downloads = 0;

    ClientConfig config() const { return m_server.clientConfig(); }

    void makeClient(int connectMs = 400, int requestMs = 400)
    {
        m_client.reset(new QtBookOrbitClient(config()));
        m_client->setTimeouts(connectMs, requestMs);
    }

    template <typename T, typename F>
    Reply<T> await(F issue)
    {
        bool done = false;
        m_calls = 0;
        m_sync = false;
        bool inIssue = true;
        Reply<T> out;
        issue([&](const Reply<T> &r) {
            if (inIssue)
                m_sync = true;
            out = r;
            done = true;
            ++m_calls;
        });
        inIssue = false;
        (void)QTest::qWaitFor([&] { return done; }, 8000);
        QTest::qWait(10); // a second callback would show up here
        return out;
    }

    QVector<CallCase> buildCalls()
    {
        QVector<CallCase> v;
        const QString hash = m_hash;
        auto add = [&v](const QString &name, const QString &path,
                        std::function<RequestId(QtBookOrbitClient &, Sink)> issue,
                        bool malformedApplies = true) {
            CallCase c;
            c.name = name;
            c.path = path;
            c.issue = issue;
            c.malformedApplies = malformedApplies;
            v.append(c);
        };
        add("checkAuth", "/users/auth",
            [](QtBookOrbitClient &c, Sink s) { return c.checkAuth(sink<bool>(s)); });
        add("capabilities", "/plugin/version",
            [](QtBookOrbitClient &c, Sink s) { return c.capabilities(sink<QJsonObject>(s)); });
        add("matchCheck", "/plugin/match-check", [hash](QtBookOrbitClient &c, Sink s) {
            MatchQuery q;
            q.partialMd5 = hash;
            return c.matchCheck({ q }, sink<QVector<MatchResult>>(s));
        });
        add("getProgress", "/syncs/progress/" + hash, [hash](QtBookOrbitClient &c, Sink s) {
            return c.getProgress(hash, sink<RemoteProgress>(s));
        });
        add("putProgress", "/syncs/progress", [hash](QtBookOrbitClient &c, Sink s) {
            RemoteProgress p;
            p.document = hash;
            p.percentage = 0.25;
            p.progress = "/body/DocFragment[2]";
            p.timestamp = 1700000000;
            return c.putProgress(p, sink<bool>(s));
        });
        add("bulkProgress", "/plugin/progress", [hash](QtBookOrbitClient &c, Sink s) {
            QJsonObject item;
            item.insert("hash", hash);
            item.insert("percentage", 0.5);
            return c.bulkProgress({ item }, sink<QJsonObject>(s));
        });
        add("uploadPageStats", "/plugin/page-stats", [hash](QtBookOrbitClient &c, Sink s) {
            PageStatsBook b;
            b.hash = hash;
            b.events.append(PageEvent{ 3, 1700000000, 40, 300 });
            return c.uploadPageStats({ b }, sink<QJsonObject>(s));
        });
        add("sendSweep", "/plugin/sweeps", [](QtBookOrbitClient &c, Sink s) {
            QJsonObject counts;
            counts.insert("booksMatched", 1);
            counts.insert("pageStatsUploaded", 1);
            return c.sendSweep(counts, sink<QJsonObject>(s));
        });
        add("exchangeAnnotations", "/plugin/annotations/exchange",
            [hash](QtBookOrbitClient &c, Sink s) {
                return c.exchangeAnnotations(hash, { sampleAnnotation() }, QJsonArray(), false,
                                             QString(), sink<QJsonObject>(s));
            });
        add("ackAnnotations", "/plugin/annotations/exchange-ack",
            [hash](QtBookOrbitClient &c, Sink s) {
                return c.ackAnnotations(hash, QJsonObject(), sink<bool>(s));
            });
        add("bookStates", "/plugin/book-states", [hash](QtBookOrbitClient &c, Sink s) {
            QJsonObject st;
            st.insert("hash", hash);
            st.insert("status", "reading");
            return c.bookStates({ st }, sink<QJsonObject>(s));
        });
        add("setReadStatus", "/plugin/catalog/books/1/read-status",
            [](QtBookOrbitClient &c, Sink s) {
                return c.setReadStatus(1, "reading", sink<bool>(s));
            });
        add("setRating", "/plugin/catalog/books/1/rating",
            [](QtBookOrbitClient &c, Sink s) { return c.setRating(1, 4, sink<bool>(s)); });
        add("catalogRoot", "/plugin/catalog/root",
            [](QtBookOrbitClient &c, Sink s) { return c.catalogRoot(sink<QJsonObject>(s)); });
        add("dashboard", "/plugin/catalog/dashboard",
            [](QtBookOrbitClient &c, Sink s) { return c.dashboard(sink<QJsonObject>(s)); });
        add("dashboardSection", "/plugin/catalog/dashboard/sections/want-to-read",
            [](QtBookOrbitClient &c, Sink s) {
                return c.dashboardSection("want-to-read", sink<QJsonObject>(s));
            });
        add("section", "/plugin/catalog/sections/authors", [](QtBookOrbitClient &c, Sink s) {
            return c.section("authors", 1, sink<QJsonObject>(s));
        });
        add("books", "/plugin/catalog/books", [](QtBookOrbitClient &c, Sink s) {
            BookQuery q;
            q.page = 1;
            q.size = 10;
            return c.books(q, sink<QJsonObject>(s));
        });
        add("book", "/plugin/catalog/books/1",
            [](QtBookOrbitClient &c, Sink s) { return c.book(1, sink<QJsonObject>(s)); });
        add("manifest", "/plugin/catalog/manifest", [](QtBookOrbitClient &c, Sink s) {
            return c.manifest(QString(), sink<QJsonObject>(s));
        });
        add(
                "download", "/plugin/catalog/files/102/download",
                [this](QtBookOrbitClient &c, Sink s) {
                    const QString dest =
                            m_tmp.filePath(QStringLiteral("matrix-%1.epub").arg(++m_downloads));
                    return c.download(102, dest, nullptr, sink<QString>(s));
                },
                false);
        add("thumbnail", "/plugin/catalog/books/1/thumbnail",
            [](QtBookOrbitClient &c, Sink s) { return c.thumbnail(1, sink<QByteArray>(s)); });
        return v;
    }

private slots:

    void initTestCase()
    {
        const QString error = m_server.start();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(m_tmp.isValid());
        m_hash = m_server.state()
                         .value("files")
                         .toObject()
                         .value("101")
                         .toObject()
                         .value("hash")
                         .toString();
        QCOMPARE(m_hash.size(), 32);
    }

    void init()
    {
        m_server.reset();
        m_server.requests(true);
        makeClient();
    }

    void cleanup() { m_client.reset(); }

    // --- The matrix ---------------------------------------------------------------

    void everyCall_data()
    {
        QTest::addColumn<int>("call");
        QTest::addColumn<QString>("scenario");
        const QVector<CallCase> calls = buildCalls();
        for (int i = 0; i < calls.size(); ++i) {
            for (const char *scenario : { "success", "timeout", "401", "500", "malformed" }) {
                if (QByteArray(scenario) == "malformed" && !calls.at(i).malformedApplies)
                    continue;
                QTest::newRow(qPrintable(calls.at(i).name + "/" + scenario))
                        << i << QString::fromLatin1(scenario);
            }
        }
    }

    void everyCall()
    {
        QFETCH(int, call);
        QFETCH(QString, scenario);
        const CallCase c = buildCalls().at(call);

        QJsonObject rule = MockServer::rule(c.path);
        ErrorKind expected = ErrorKind::None;
        if (scenario == "timeout") {
            rule.insert("delay_ms", 2500);
            expected = ErrorKind::Timeout;
        } else if (scenario == "401") {
            rule.insert("status", 401);
            expected = ErrorKind::AuthRejected;
        } else if (scenario == "500") {
            rule.insert("status", 500);
            expected = ErrorKind::ServerError;
        } else if (scenario == "malformed") {
            rule.insert("raw_body", "{not json");
            expected = ErrorKind::BadResponse;
        }
        if (scenario != "success")
            m_server.setRules({ rule });

        bool done = false;
        HttpResult result;
        int calls = 0;
        c.issue(*m_client, [&](const HttpResult &r) {
            result = r;
            done = true;
            ++calls;
        });
        (void)QTest::qWaitFor([&] { return done; }, 8000);
        QTest::qWait(10);
        QVERIFY2(done, "no callback");
        QCOMPARE(calls, 1);
        QVERIFY2(result.error == expected,
                 qPrintable(QStringLiteral("got error %1 (%2), expected %3")
                                    .arg(static_cast<int>(result.error))
                                    .arg(result.message)
                                    .arg(static_cast<int>(expected))));
        if (scenario == "401")
            QCOMPARE(result.httpStatus, 401);
        if (scenario == "500")
            QCOMPARE(result.httpStatus, 500);
        if (scenario == "success")
            QCOMPARE(result.httpStatus == 0 ? 200 : result.httpStatus, 200);
    }

    // --- Typed payloads --------------------------------------------------------------

    void authHeadersAndNoPlaintext()
    {
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QVERIFY(r.result.ok());
        QVERIFY(r.value);
        const QJsonObject req = m_server.requests().at(0).toObject();
        const QJsonObject headers = req.value("headers").toObject();
        QCOMPARE(headers.value("x-auth-user").toString(), QStringLiteral("test-reader"));
        QCOMPARE(headers.value("x-auth-key").toString(),
                 QString::fromLatin1(
                         QCryptographicHash::hash("fake-password", QCryptographicHash::Md5)
                                 .toHex()));
        QVERIFY(!QJsonDocument(req).toJson().contains("fake-password"));
    }

    void wrongCredentialsAreRejected()
    {
        ClientConfig c = config();
        c.keyMd5Hex = QByteArray(32, '0');
        m_client.reset(new QtBookOrbitClient(c));
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::AuthRejected);
        QCOMPARE(r.result.httpStatus, 401);
        QVERIFY(!r.value);
    }

    void missingLoginFailsWithoutARequest()
    {
        ClientConfig c = config();
        c.username.clear();
        m_client.reset(new QtBookOrbitClient(c));
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::AuthRejected);
        QCOMPARE(m_server.requests().size(), 0);
    }

    void badServerUrlFailsWithoutARequest_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("empty") << QString();
        QTest::newRow("ftp") << QStringLiteral("ftp://books.example.com");
        QTest::newRow("userinfo") << QStringLiteral("http://user:pw@127.0.0.1:1");
        QTest::newRow("query") << QStringLiteral("http://127.0.0.1:1/?x=1");
    }

    void badServerUrlFailsWithoutARequest()
    {
        QFETCH(QString, url);
        ClientConfig c = config();
        c.serverUrl = QUrl(url);
        m_client.reset(new QtBookOrbitClient(c));
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::ClientError);
        QCOMPARE(m_server.requests().size(), 0);
    }

    void serverUrlNormalisation()
    {
        ClientConfig c = config();
        c.serverUrl = QUrl(QStringLiteral("https://books.example.com/"));
        QtBookOrbitClient a(c);
        QCOMPARE(a.baseUrlForTest(), QStringLiteral("https://books.example.com/api/v1/koreader/"));
        c.serverUrl = QUrl(QStringLiteral("https://books.example.com/api/v1/koreader/"));
        QtBookOrbitClient b(c);
        QCOMPARE(b.baseUrlForTest(), QStringLiteral("https://books.example.com/api/v1/koreader/"));
        c.serverUrl = QUrl(QStringLiteral("http://nas.local:3000/books/api/v1"));
        QtBookOrbitClient d(c);
        QCOMPARE(d.baseUrlForTest(),
                 QStringLiteral("http://nas.local:3000/books/api/v1/koreader/"));
        // The plain base URL of the mock works end to end, and so does one that already has the path.
        c = config();
        c.serverUrl = QUrl(c.serverUrl.toString() + "/api/v1/koreader/");
        m_client.reset(new QtBookOrbitClient(c));
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->checkAuth(cb);
                }).value);
    }

    void pluginBodiesCarryDeviceFieldsAndKosyncDoesNot()
    {
        QVERIFY(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    QJsonObject counts;
                    m_client->sendSweep(counts, cb);
                }).result.ok());
        RemoteProgress p;
        p.document = m_hash;
        p.percentage = 0.5;
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->putProgress(p, cb);
                }).result.ok());

        const QJsonObject sweep =
                m_server.requestsTo("/plugin/sweeps").at(0).toObject().value("body").toObject();
        QCOMPARE(sweep.value("deviceId").toString(), QStringLiteral("smalt-test-device-1"));
        QCOMPARE(sweep.value("deviceModel").toString(), QStringLiteral("Kobo Test"));
        QCOMPARE(sweep.value("pluginVersion").toString(), QStringLiteral("0.1.0"));
        QVERIFY(QRegularExpression("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$")
                        .match(sweep.value("deviceTime").toString())
                        .hasMatch());
        // Counts default to 0 because the server insists on all three.
        QCOMPARE(sweep.value("booksMatched").toInt(-1), 0);
        QCOMPARE(sweep.value("pageStatsUploaded").toInt(-1), 0);
        QCOMPARE(sweep.value("annotationsUpserted").toInt(-1), 0);

        const QJsonObject put =
                m_server.requestsTo("/syncs/progress").at(0).toObject().value("body").toObject();
        QVERIFY(!put.contains("deviceId")); // kosync is snake_case and has no plugin fields
        QCOMPARE(put.value("device_id").toString(), QStringLiteral("smalt-test-device-1"));
        QCOMPARE(put.value("device").toString(), QStringLiteral("Kobo Test"));
    }

    void invalidDeviceIdFailsLocally()
    {
        ClientConfig c = config();
        c.deviceId = QStringLiteral("bad id!");
        m_client.reset(new QtBookOrbitClient(c));
        const auto r = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->sendSweep(QJsonObject(), cb);
        });
        QCOMPARE(r.result.error, ErrorKind::ClientError);
        QCOMPARE(m_server.requests().size(), 0);
    }

    void matchCheckTypedResults()
    {
        MatchQuery known;
        known.partialMd5 = m_hash.toUpper(); // the server lower-cases; so do we
        known.title = QStringLiteral("The Salt Road");
        known.authors = QStringList{ QStringLiteral("Ada Quill"), QStringLiteral("Cy Reed") };
        known.bookFileId = 101;
        MatchQuery unknown;
        unknown.partialMd5 = QString(32, QLatin1Char('a'));
        MatchQuery invalid;
        invalid.partialMd5 = QStringLiteral("not-a-digest");

        const auto r = await<QVector<MatchResult>>(
                [&](BookOrbitClient::Callback<QVector<MatchResult>> cb) {
                    m_client->matchCheck({ known, unknown, invalid }, cb);
                });
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        QCOMPARE(r.value.size(), 3);
        QVERIFY(r.value[0].matched);
        QCOMPARE(r.value[0].bookId, qint64(1));
        QCOMPARE(r.value[0].bookFileId, qint64(101));
        QVERIFY(r.value[0].positionCompatible); // the server only ever says false
        QCOMPARE(r.value[0].partialMd5, known.partialMd5);
        QVERIFY(!r.value[1].matched);
        QVERIFY(!r.value[2].matched);
        QVERIFY(!m_client->libraryVersion().isEmpty());

        const QJsonObject body = m_server.requestsTo("/plugin/match-check")
                                         .at(0)
                                         .toObject()
                                         .value("body")
                                         .toObject();
        QCOMPARE(body.value("hashes").toArray().size(), 2); // the invalid digest never goes out
        QCOMPARE(body.value("hashes").toArray().at(0).toString(), m_hash);
        const QJsonObject b0 = body.value("books").toArray().at(0).toObject();
        QCOMPARE(b0.value("title").toString(), QStringLiteral("The Salt Road"));
        QCOMPARE(b0.value("authors").toString(),
                 QStringLiteral("Ada Quill\nCy Reed")); // KOReader's separator
        QCOMPARE(b0.value("bookFileId").toInt(), 101);
        QCOMPARE(b0.value("source").toString(), QStringLiteral("file"));
    }

    void matchCheckWithNothingValidSkipsTheNetwork()
    {
        MatchQuery q;
        q.partialMd5 = QStringLiteral("zzz");
        const auto r = await<QVector<MatchResult>>(
                [&](BookOrbitClient::Callback<QVector<MatchResult>> cb) {
                    m_client->matchCheck({ q }, cb);
                });
        QVERIFY(r.result.ok());
        QCOMPARE(r.value.size(), 1);
        QVERIFY(!r.value[0].matched);
        QCOMPARE(m_server.requests().size(), 0);
    }

    void matchCheckReplyWithoutMatchesIsBadResponse()
    {
        QJsonObject rule = MockServer::rule("/plugin/match-check");
        rule.insert("body", QJsonObject());
        m_server.setRules({ rule });
        MatchQuery q;
        q.partialMd5 = m_hash;
        const auto r = await<QVector<MatchResult>>(
                [&](BookOrbitClient::Callback<QVector<MatchResult>> cb) {
                    m_client->matchCheck({ q }, cb);
                });
        QCOMPARE(r.result.error, ErrorKind::BadResponse);
    }

    void progressRoundTrip()
    {
        auto got = await<RemoteProgress>([&](BookOrbitClient::Callback<RemoteProgress> cb) {
            m_client->getProgress(m_hash, cb);
        });
        QVERIFY(got.result.ok());
        QVERIFY(got.value.document.isEmpty()); // the server answers {} for "nothing yet"

        RemoteProgress p;
        p.document = m_hash;
        p.percentage = 0.4375;
        p.progress = QStringLiteral("/body/DocFragment[5]/body/p[2]/text().0");
        p.timestamp = 1700000123;
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->putProgress(p, cb);
                }).value);

        got = await<RemoteProgress>([&](BookOrbitClient::Callback<RemoteProgress> cb) {
            m_client->getProgress(m_hash, cb);
        });
        QVERIFY(got.result.ok());
        QCOMPARE(got.value.document, m_hash);
        QCOMPARE(got.value.percentage, 0.4375);
        QCOMPARE(got.value.progress, p.progress);
        QCOMPARE(got.value.timestamp, qint64(1700000123));
        QCOMPARE(got.value.device, QStringLiteral("Kobo Test"));
        QCOMPARE(got.value.deviceId, QStringLiteral("smalt-test-device-1"));
    }

    void progressForUnknownBookIsNotFound()
    {
        RemoteProgress p;
        p.document = QString(32, QLatin1Char('b'));
        p.percentage = 0.1;
        const auto r = await<bool>(
                [&](BookOrbitClient::Callback<bool> cb) { m_client->putProgress(p, cb); });
        QCOMPARE(r.result.error, ErrorKind::NotFound);
        QVERIFY(!r.value);
    }

    void progressArgumentsAreChecked()
    {
        RemoteProgress p;
        p.document = m_hash;
        p.percentage = 1.5;
        QCOMPARE(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                     m_client->putProgress(p, cb);
                 }).result.error,
                 ErrorKind::ClientError);
        QCOMPARE(await<RemoteProgress>([&](BookOrbitClient::Callback<RemoteProgress> cb) {
                     m_client->getProgress(QStringLiteral("../x"), cb);
                 }).result.error,
                 ErrorKind::ClientError);
        QCOMPARE(m_server.requests().size(), 0);
    }

    void pagedProgressNumberBecomesText()
    {
        QJsonObject rule = MockServer::rule(QStringLiteral("/syncs/progress/") + m_hash);
        QJsonObject body;
        body.insert("document", m_hash);
        body.insert("percentage", 0.5);
        body.insert("progress", 42); // KOReader sends a page number for PDFs and comics
        rule.insert("body", body);
        m_server.setRules({ rule });
        const auto r = await<RemoteProgress>([&](BookOrbitClient::Callback<RemoteProgress> cb) {
            m_client->getProgress(m_hash, cb);
        });
        QCOMPARE(r.value.progress, QStringLiteral("42"));
    }

    void pageStatsAndSweepShapes()
    {
        PageStatsBook b;
        b.hash = m_hash.toUpper();
        b.bookFileId = 101;
        b.events.append(PageEvent{ 3, 1700000000, 40, 300 });
        b.events.append(PageEvent{ 4, 1700000100, 999999, 300 }); // duration is clamped
        const auto r = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->uploadPageStats({ b }, cb);
        });
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        const QJsonObject res = r.value.value("results").toArray().at(0).toObject();
        QCOMPARE(res.value("hash").toString(), m_hash);
        QCOMPARE(res.value("accepted").toInt(), 2);
        QCOMPARE(res.value("watermark").toDouble(), 1700000100.0);

        const QJsonObject body =
                m_server.requestsTo("/plugin/page-stats").at(0).toObject().value("body").toObject();
        const QJsonObject book = body.value("books").toArray().at(0).toObject();
        QCOMPARE(book.value("hash").toString(), m_hash);
        QCOMPARE(book.value("bookFileId").toInt(), 101);
        const QJsonObject e1 = book.value("events").toArray().at(1).toObject();
        QCOMPARE(e1.value("durationSeconds").toInt(), 86400);
        QCOMPARE(e1.value("startTime").toDouble(), 1700000100.0);

        QJsonObject counts;
        counts.insert("booksMatched", 1);
        counts.insert("pageStatsUploaded", 2);
        counts.insert("annotationsUpserted", 0);
        const auto sweep = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->sendSweep(counts, cb);
        });
        QVERIFY(sweep.result.ok());
        QVERIFY(sweep.value.value("ok").toBool());
        QVERIFY(!sweep.value.value("libraryVersion").toString().isEmpty());
        QCOMPARE(m_client->libraryVersion(), sweep.value.value("libraryVersion").toString());
    }

    void pageStatsLimitsAreEnforcedLocally()
    {
        PageStatsBook b;
        b.hash = m_hash;
        for (int i = 0; i < 501; ++i)
            b.events.append(PageEvent{ 1, 1700000000 + i, 1, 10 });
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->uploadPageStats({ b }, cb);
                 }).result.error,
                 ErrorKind::ClientError);
        PageStatsBook bad;
        bad.hash = QStringLiteral("nothex");
        bad.events.append(PageEvent{ 1, 1700000000, 1, 10 });
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->uploadPageStats({ bad }, cb);
                 }).result.error,
                 ErrorKind::ClientError);
        // A book with no events is simply left out; with nothing left there is no request.
        PageStatsBook empty;
        empty.hash = m_hash;
        QVERIFY(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    m_client->uploadPageStats({ empty }, cb);
                }).result.ok());
        QCOMPARE(m_server.requests().size(), 0);
    }

    void annotationsExchangeAndAck()
    {
        const auto r = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->exchangeAnnotations(m_hash, { sampleAnnotation() }, QJsonArray(), false,
                                          QString(), cb);
        });
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        const QJsonObject res = r.value.value("results").toArray().at(0).toObject();
        QCOMPARE(res.value("applied").toObject().value("created").toInt(), 1);
        QVERIFY(res.value("toApply").toObject().contains(
                "add")); // incoming is passed through untouched
        const QJsonObject book = m_server.requestsTo("/plugin/annotations/exchange")
                                         .at(0)
                                         .toObject()
                                         .value("body")
                                         .toObject()
                                         .value("books")
                                         .toArray()
                                         .at(0)
                                         .toObject();
        QCOMPARE(book.value("hash").toString(), m_hash);
        QVERIFY(book.value("keys").toArray().isEmpty());
        QCOMPARE(book.value("keysComplete").toBool(true), false);
        QVERIFY(!book.contains("sidecarState"));

        // The fuller form: a complete key list lets the server detect deletions.
        QJsonObject key;
        key.insert("k", QString(32, QLatin1Char('c')));
        key.insert("dt", "2026-11-16 10:00:00");
        const auto ex = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->exchangeAnnotations(m_hash, QJsonArray(), { key }, true, "known", cb);
        });
        QVERIFY2(ex.result.ok(), qPrintable(ex.result.message));
        const QJsonObject book2 = m_server.requestsTo("/plugin/annotations/exchange")
                                          .at(1)
                                          .toObject()
                                          .value("body")
                                          .toObject()
                                          .value("books")
                                          .toArray()
                                          .at(0)
                                          .toObject();
        QCOMPARE(book2.value("keys").toArray().size(), 1);
        QCOMPARE(book2.value("keysComplete").toBool(), true);
        QCOMPARE(book2.value("sidecarState").toString(), QStringLiteral("known"));

        QJsonObject applied;
        applied.insert("serverId", 7);
        applied.insert("version", 1);
        applied.insert("status", "applied");
        QJsonObject ack;
        ack.insert("applied", QJsonArray{ applied });
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->ackAnnotations(m_hash, ack, cb);
                }).value);
        const QJsonObject ackBook = m_server.requestsTo("/plugin/annotations/exchange-ack")
                                            .at(0)
                                            .toObject()
                                            .value("body")
                                            .toObject()
                                            .value("books")
                                            .toArray()
                                            .at(0)
                                            .toObject();
        QCOMPARE(ackBook.value("applied").toArray().size(), 1);
        QVERIFY(ackBook.value("deleted").toArray().isEmpty()); // required by the server, defaulted
    }

    void annotationLimitsAreEnforcedLocally()
    {
        QJsonArray many;
        for (int i = 0; i < 51; ++i)
            many.append(sampleAnnotation());
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->exchangeAnnotations(m_hash, many, QJsonArray(), false, QString(),
                                                   cb);
                 }).result.error,
                 ErrorKind::ClientError);
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->exchangeAnnotations(QStringLiteral("x"), QJsonArray(), QJsonArray(),
                                                   false, QString(), cb);
                 }).result.error,
                 ErrorKind::ClientError);
    }

    void statusAndRatings()
    {
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->setReadStatus(2, "want_to_read", cb);
                }).value);
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->setRating(2, 5, cb);
                }).value);
        QJsonObject state = m_server.state();
        QCOMPARE(state.value("readStatus").toObject().value("2").toString(),
                 QStringLiteral("want_to_read"));
        QCOMPARE(state.value("ratings").toObject().value("2").toInt(), 5);
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->setRating(2, 0, cb);
                }).value);
        QVERIFY(m_server.state()
                        .value("ratings")
                        .toObject()
                        .value("2")
                        .isNull()); // 0 clears (sends null)
        QCOMPARE(m_server.requestsTo("/plugin/catalog/books/2/rating")
                         .at(1)
                         .toObject()
                         .value("body")
                         .toObject()
                         .value("rating"),
                 QJsonValue(QJsonValue::Null));

        QCOMPARE(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                     m_client->setReadStatus(2, "bogus", cb);
                 }).result.error,
                 ErrorKind::ClientError);
        QCOMPARE(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                     m_client->setRating(2, 9, cb);
                 }).result.error,
                 ErrorKind::ClientError);
        QCOMPARE(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                     m_client->setReadStatus(99, "read", cb);
                 }).result.error,
                 ErrorKind::NotFound);
    }

    void bookStatesAndBulkProgress()
    {
        QJsonObject st;
        st.insert("hash", m_hash);
        st.insert("status", "complete");
        st.insert("rating", 4);
        const auto s = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->bookStates({ st }, cb);
        });
        QVERIFY(s.result.ok());
        QCOMPARE(s.value.value("results").toArray().size(), 1);

        QJsonObject item;
        item.insert("hash", m_hash);
        item.insert("percentage", 0.9);
        QJsonObject unknown;
        unknown.insert("hash", QString(32, QLatin1Char('d')));
        unknown.insert("percentage", 0.1);
        const auto b = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->bulkProgress({ item, unknown }, cb);
        });
        QVERIFY(b.result.ok());
        QCOMPARE(b.value.value("unmatched").toArray().size(), 1);

        QJsonArray many;
        for (int i = 0; i < 101; ++i)
            many.append(item);
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->bulkProgress(many, cb);
                 }).result.error,
                 ErrorKind::ClientError);
    }

    void catalogCalls()
    {
        auto obj = [&](std::function<void(BookOrbitClient::Callback<QJsonObject>)> f) {
            return await<QJsonObject>(f);
        };
        QVERIFY(obj([&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->catalogRoot(cb); })
                        .value.value("sections")
                        .isArray());
        QVERIFY(obj([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    m_client->dashboard(cb);
                }).value.contains("continueReading"));
        const auto series = obj([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->section("series", 0, cb);
        });
        QVERIFY(series.result.ok());
        QCOMPARE(series.value.value("items").toArray().size(), 1);
        QVERIFY(obj([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    m_client->dashboardSection("up-next-in-series", cb);
                }).result.ok());
        const auto detail =
                obj([&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->book(2, cb); });
        QCOMPARE(detail.value.value("title").toString(), QStringLiteral("Salt Harvest"));
        QCOMPARE(detail.value.value("files").toArray().at(0).toObject().value("id").toInt(), 102);
        // book() sends the device ID, as the plugin does.
        QCOMPARE(m_server.requestsTo("/plugin/catalog/books/2")
                         .at(0)
                         .toObject()
                         .value("query")
                         .toObject()
                         .value("deviceId")
                         .toString(),
                 QStringLiteral("smalt-test-device-1"));
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->book(99, cb);
                 }).result.error,
                 ErrorKind::NotFound);
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->section("nope", 1, cb);
                 }).result.error,
                 ErrorKind::ClientError);
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->dashboardSection("nope", cb);
                 }).result.error,
                 ErrorKind::ClientError);
    }

    void booksQuery()
    {
        BookQuery q;
        q.ids = { 1, 2, 3 };
        q.page = 1;
        q.size = 500; // clamped to the server's maximum of 100
        q.sort = QStringLiteral("title");
        q.order = QStringLiteral("desc");
        const auto r = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->books(q, cb); });
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        QCOMPARE(r.value.value("items").toArray().size(), 3);
        QCOMPARE(r.value.value("items").toArray().at(0).toObject().value("title").toString(),
                 QStringLiteral("Tidewrack"));
        const QJsonObject query = m_server.requestsTo("/plugin/catalog/books")
                                          .at(0)
                                          .toObject()
                                          .value("query")
                                          .toObject();
        QCOMPARE(query.value("ids").toString(), QStringLiteral("1,2,3"));
        QCOMPARE(query.value("size").toString(), QStringLiteral("100"));
        QCOMPARE(query.value("sort").toString(), QStringLiteral("title"));
        QCOMPARE(query.value("order").toString(), QStringLiteral("desc"));
        QVERIFY(!query.contains("q"));
        QVERIFY(!query.contains("seriesId"));

        BookQuery s;
        s.q = QStringLiteral("salt & harvest");
        s.seriesId = 10;
        const auto sr = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->books(s, cb); });
        QVERIFY(sr.result.ok());
        QCOMPARE(m_server.requestsTo("/plugin/catalog/books")
                         .at(1)
                         .toObject()
                         .value("query")
                         .toObject()
                         .value("q")
                         .toString(),
                 QStringLiteral("salt & harvest")); // percent-encoding survives the round trip

        BookQuery bad;
        bad.sort = QStringLiteral("bogus");
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->books(bad, cb);
                 }).result.error,
                 ErrorKind::ClientError); // 400 from the server

        BookQuery tooMany;
        for (int i = 1; i <= 201; ++i)
            tooMany.ids.append(i);
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->books(tooMany, cb);
                 }).result.error,
                 ErrorKind::ClientError);
    }

    void thumbnailIsAnImage()
    {
        const auto r = await<QByteArray>(
                [&](BookOrbitClient::Callback<QByteArray> cb) { m_client->thumbnail(1, cb); });
        QVERIFY(r.result.ok());
        QVERIFY(r.value.startsWith("\x89PNG"));
        QJsonObject rule = MockServer::rule("/plugin/catalog/books/1/thumbnail");
        rule.insert("raw_body", "<html>nope</html>");
        rule.insert("content_type", "text/html");
        m_server.setRules({ rule });
        const auto bad = await<QByteArray>(
                [&](BookOrbitClient::Callback<QByteArray> cb) { m_client->thumbnail(1, cb); });
        QCOMPARE(bad.result.error, ErrorKind::BadResponse);
    }

    void jsonArrayWhereAnObjectIsExpectedIsBadResponse()
    {
        QJsonObject rule = MockServer::rule("/plugin/catalog/root");
        rule.insert("body", QJsonArray{ 1, 2 });
        m_server.setRules({ rule });
        QCOMPARE(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                     m_client->catalogRoot(cb);
                 }).result.error,
                 ErrorKind::BadResponse);
    }

    // --- Capabilities ---------------------------------------------------------------------

    void capabilitiesAreFetchedAndCached()
    {
        QVERIFY(!m_client->hasCapability("catalogDashboardSections"));
        QVERIFY(!m_client->capabilitiesKnown());
        const auto r = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->capabilities(cb); });
        QVERIFY(r.result.ok());
        QVERIFY(m_client->capabilitiesKnown());
        QVERIFY(m_client->hasCapability("catalogDashboardSections"));
        QVERIFY(m_client->hasCapability("catalogBulkManifest"));
        QVERIFY(!m_client->hasCapability("somethingElse"));
        QCOMPARE(m_client->serverVersion(), QStringLiteral("9.9.9-mock"));
        QCOMPARE(r.value.value("serverVersion").toString(), QStringLiteral("9.9.9-mock"));
        const QJsonObject headers =
                m_server.requestsTo("/plugin/version").at(0).toObject().value("headers").toObject();
        QCOMPARE(headers.value("x-bookorbit-device-id").toString(),
                 QStringLiteral("smalt-test-device-1"));
        QCOMPARE(headers.value("x-bookorbit-plugin-version").toString(), QStringLiteral("0.1.0"));

        // A gated call now needs no second fetch.
        QVERIFY(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    m_client->dashboardSection("want-to-read", cb);
                }).result.ok());
        QCOMPARE(m_server.requestsTo("/plugin/version").size(), 1);

        // A new config forgets what it knew.
        m_client->setConfig(config());
        QVERIFY(!m_client->capabilitiesKnown());
        QVERIFY(!m_client->hasCapability("catalogDashboardSections"));
    }

    void gatedCallFetchesCapabilitiesFirstAndOnlyOnce()
    {
        bool a = false, b = false;
        m_client->dashboardSection("want-to-read",
                                   [&](const Reply<QJsonObject> &r) { a = r.result.ok(); });
        m_client->manifest(QString(), [&](const Reply<QJsonObject> &r) { b = r.result.ok(); });
        (void)QTest::qWaitFor([&] { return a && b; }, 8000);
        QVERIFY(a);
        QVERIFY(b);
        QCOMPARE(m_server.requestsTo("/plugin/version").size(), 1);
        QCOMPARE(m_server.requestsTo("/plugin/catalog/manifest").size(), 1);
        // The version request came before the gated ones.
        QCOMPARE(m_server.requests().at(0).toObject().value("path").toString(),
                 QStringLiteral("/plugin/version"));
    }

    void missingCapabilityMeansNoRequest()
    {
        QJsonObject cfg;
        cfg.insert("capabilities", QJsonArray{ "catalogListsV2" });
        m_server.config(cfg);
        const auto d = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->dashboardSection("want-to-read", cb);
        });
        QCOMPARE(d.result.error, ErrorKind::NotSupported);
        const auto m = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->manifest(QString(), cb);
        });
        QCOMPARE(m.result.error, ErrorKind::NotSupported);
        BookQuery q;
        q.sort = QStringLiteral("published");
        const auto p = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->books(q, cb); });
        QCOMPARE(p.result.error, ErrorKind::NotSupported);
        QCOMPARE(m_server.requestsTo("/plugin/catalog/dashboard/sections/want-to-read").size(), 0);
        QCOMPARE(m_server.requestsTo("/plugin/catalog/manifest").size(), 0);
        QCOMPARE(m_server.requestsTo("/plugin/catalog/books").size(), 0);
        QCOMPARE(m_server.requestsTo("/plugin/version").size(), 1); // asked once, remembered
        // Ungated calls still work.
        QVERIFY(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    m_client->catalogRoot(cb);
                }).result.ok());
    }

    void oldServerWithoutAVersionEndpointHasNoCapabilities()
    {
        QJsonObject cfg;
        cfg.insert("capabilities", QJsonValue(QJsonValue::Null)); // /plugin/version answers 404
        m_server.config(cfg);
        const auto c = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->capabilities(cb); });
        QCOMPARE(c.result.error, ErrorKind::NotFound);
        QVERIFY(m_client->capabilitiesKnown()); // a 4xx is a definite answer
        QVERIFY(!m_client->hasCapability("catalogDashboardSections"));
        const auto d = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->dashboardSection("want-to-read", cb);
        });
        QCOMPARE(d.result.error, ErrorKind::NotSupported);
    }

    void unknownCapabilitiesStayUnknownOnServerErrorsAndTimeouts()
    {
        m_server.setRules({ statusRule("/plugin/version", 500) });
        const auto d = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->dashboardSection("want-to-read", cb);
        });
        QCOMPARE(d.result.error, ErrorKind::ServerError); // not NotSupported: we don't know
        QVERIFY(!m_client->capabilitiesKnown());

        QJsonObject slow = MockServer::rule("/plugin/version");
        slow.insert("delay_ms", 2500);
        m_server.setRules({ slow });
        const auto t = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->manifest(QString(), cb);
        });
        QCOMPARE(t.result.error, ErrorKind::Timeout);
        QVERIFY(!m_client->capabilitiesKnown());

        m_server.setRules({ statusRule("/plugin/version", 401) });
        const auto a = await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
            m_client->manifest(QString(), cb);
        });
        QCOMPARE(a.result.error, ErrorKind::AuthRejected);
        QVERIFY(!m_client->capabilitiesKnown());

        // Once the server answers, the same client recovers.
        m_server.clearRules();
        QVERIFY(await<QJsonObject>([&](BookOrbitClient::Callback<QJsonObject> cb) {
                    m_client->manifest(QString(), cb);
                }).result.ok());
        QVERIFY(m_client->capabilitiesKnown());
    }

    // --- Threading ---------------------------------------------------------------------------

    void noCallBlocksAndCallbacksComeLaterOnThisThread()
    {
        QJsonObject slow = MockServer::rule("/users/auth");
        slow.insert("delay_ms", 300);
        m_server.setRules({ slow });
        makeClient(2000, 2000);

        QThread *const home = QThread::currentThread();
        bool done = false;
        bool rightThread = false;
        QElapsedTimer timer;
        timer.start();
        m_client->checkAuth([&](const Reply<bool> &r) {
            done = true;
            rightThread = QThread::currentThread() == home && r.result.ok();
        });
        QVERIFY2(timer.elapsed() < 100, "checkAuth blocked the calling thread");
        QVERIFY(!done); // never from inside the call
        (void)QTest::qWaitFor([&] { return done; }, 5000);
        QVERIFY(done);
        QVERIFY(rightThread);
        QVERIFY(timer.elapsed() >= 250); // it really did wait for the slow reply

        // Failures that need no network come back later too, not from inside the call.
        bool failed = false;
        bool syncFailure = true;
        bool inCall = true;
        m_client->setReadStatus(1, "bogus", [&](const Reply<bool> &r) {
            if (inCall)
                syncFailure = true;
            else
                syncFailure = false;
            failed = !r.result.ok();
        });
        inCall = false;
        QVERIFY(!failed);
        (void)QTest::qWaitFor([&] { return failed; }, 2000);
        QVERIFY(failed);
        QVERIFY(!syncFailure);
    }

    void callbackIsNeverSynchronousForAnyLocalFailure()
    {
        const auto r = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->book(-5, cb); });
        QCOMPARE(r.result.error, ErrorKind::ClientError);
        QVERIFY(!m_sync);
        QCOMPARE(m_calls, 1);
    }

    // --- Cancel ----------------------------------------------------------------------------------

    void cancelDeliversCancelledOnce()
    {
        QJsonObject slow = MockServer::rule("/users/auth");
        slow.insert("delay_ms", 2000);
        m_server.setRules({ slow });
        makeClient(3000, 3000);
        int calls = 0;
        ErrorKind kind = ErrorKind::None;
        const RequestId id = m_client->checkAuth([&](const Reply<bool> &r) {
            ++calls;
            kind = r.result.error;
        });
        QTest::qWait(50);
        m_client->cancel(id);
        QCOMPARE(calls, 0); // not from inside cancel()
        (void)QTest::qWaitFor([&] { return calls > 0; }, 2000);
        QTest::qWait(30);
        QCOMPARE(calls, 1);
        QCOMPARE(kind, ErrorKind::Cancelled);
        m_client->cancel(id); // already done: nothing happens
        m_client->cancel(123456);
        QTest::qWait(30);
        QCOMPARE(calls, 1);
    }

    void cancelAllAndCancelWhileWaitingForCapabilities()
    {
        QJsonObject slow = MockServer::rule("/plugin/version");
        slow.insert("delay_ms", 2000);
        QJsonObject slowAuth = MockServer::rule("/users/auth");
        slowAuth.insert("delay_ms", 2000);
        m_server.setRules({ slow, slowAuth });
        makeClient(3000, 3000);
        QVector<ErrorKind> kinds;
        auto note = [&kinds](ErrorKind k) { kinds.append(k); };
        m_client->checkAuth([&](const Reply<bool> &r) { note(r.result.error); });
        m_client->dashboardSection("want-to-read", [&](const Reply<QJsonObject> &r) {
            note(r.result.error);
        }); // gated
        m_client->checkAuth([&](const Reply<bool> &r) { note(r.result.error); });
        QTest::qWait(50);
        m_client->cancelAll();
        (void)QTest::qWaitFor([&] { return kinds.size() >= 3; }, 3000);
        QTest::qWait(30);
        QCOMPARE(kinds.size(), 3);
        for (ErrorKind k : kinds)
            QCOMPARE(k, ErrorKind::Cancelled);
    }

    void destroyingTheClientDropsCallbacks()
    {
        QJsonObject slow = MockServer::rule("/users/auth");
        slow.insert("delay_ms", 500);
        m_server.setRules({ slow });
        makeClient(3000, 3000);
        int calls = 0;
        m_client->checkAuth([&](const Reply<bool> &) { ++calls; });
        m_client->setReadStatus(1, "bogus", [&](const Reply<bool> &) { ++calls; });
        m_client.reset();
        QTest::qWait(700);
        QCOMPARE(calls, 0);
    }

    // --- Network failures (fake servers) --------------------------------------------------------------

    void connectionRefusedIsOffline()
    {
        quint16 port = 0;
        {
            QTcpServer probe; // borrow a free port, then give it back so nothing listens there
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            port = probe.serverPort();
        }
        ClientConfig c = config();
        c.serverUrl = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::Offline);
        QCOMPARE(r.result.httpStatus, 0);
    }

    void positionCompatibleIsTrueUnlessTheServerSaysFalse()
    {
        QJsonObject rule = MockServer::rule("/plugin/match-check");
        rule.insert(
                "body",
                QJsonObject{ { "libraryVersion", "v1" },
                             { "matches",
                               QJsonArray{ QJsonObject{ { "hash", m_hash },
                                                        { "bookId", 1 },
                                                        { "bookFileId", 101 },
                                                        { "positionCompatible", false } },
                                           QJsonObject{ { "hash", QString(32, QLatin1Char('b')) },
                                                        { "bookId", 2 },
                                                        { "bookFileId", 102 } },
                                           QJsonObject{ { "hash", QString(32, QLatin1Char('c')) },
                                                        { "bookId", 3 },
                                                        { "bookFileId", 103 },
                                                        { "positionCompatible", true } } } } });
        m_server.setRules({ rule });
        makeClient();
        MatchQuery a, b, c;
        a.partialMd5 = m_hash;
        b.partialMd5 = QString(32, QLatin1Char('b'));
        c.partialMd5 = QString(32, QLatin1Char('c'));
        const auto r = await<QVector<MatchResult>>(
                [&](BookOrbitClient::Callback<QVector<MatchResult>> cb) {
                    m_client->matchCheck({ a, b, c }, cb);
                });
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        QCOMPARE(r.value.size(), 3);
        QVERIFY(!r.value[0].positionCompatible);
        QVERIFY(r.value[1].positionCompatible); // absent means true
        QVERIFY(r.value[2].positionCompatible);
        QCOMPARE(m_client->libraryVersion(), QStringLiteral("v1"));
    }

    void unresolvableHostIsOffline()
    {
        ClientConfig c = config();
        c.serverUrl = QUrl(QStringLiteral("http://no-such-host.invalid"));
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(3000, 3000);
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QVERIFY2(r.result.error == ErrorKind::Offline || r.result.error == ErrorKind::Timeout,
                 qPrintable(r.result.message));
    }

    void connectionClosedWithoutAnAnswerIsOffline()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        fake.setHandler([](QTcpSocket *s, const FakeHttpServer::Request &) { s->abort(); });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::Offline);
        QCOMPARE(m_calls, 1);
    }

    void connectionDroppedMidBodyIsOffline()
    {
        QJsonObject rule = MockServer::rule("/plugin/catalog/root");
        rule.insert("drop", true);
        m_server.setRules({ rule });
        makeClient(2000, 3000);
        const auto r = await<QJsonObject>(
                [&](BookOrbitClient::Callback<QJsonObject> cb) { m_client->catalogRoot(cb); });
        QCOMPARE(r.result.error, ErrorKind::Offline);
    }

    void aServerThatNeverAnswersTimesOutOnConnect()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        fake.setHandler([](QTcpSocket *, const FakeHttpServer::Request &) { /* say nothing */ });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(250, 5000);
        QElapsedTimer timer;
        timer.start();
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::Timeout);
        QVERIFY(r.result.message.contains("connect"));
        QVERIFY(timer.elapsed() < 2000);
    }

    void aServerThatStallsMidReplyTimesOutOnTheRequest()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        QVector<QTcpSocket *> held;
        fake.setHandler([&held](QTcpSocket *s, const FakeHttpServer::Request &) {
            s->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n{");
            held.append(s); // headers out, then silence
        });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(5000, 300);
        QElapsedTimer timer;
        timer.start();
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::Timeout);
        QVERIFY(r.result.message.contains("request"));
        QVERIFY(timer.elapsed() < 2500);
    }

    void redirectsStayOnTheSameHost()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        fake.setHandler([&fake](QTcpSocket *s, const FakeHttpServer::Request &r) {
            if (r.target.endsWith("/users/auth"))
                FakeHttpServer::reply(s, 302, "", "Location: /moved/auth\r\n");
            else
                FakeHttpServer::reply(s, 200, R"({"authorized":"OK","username":"x"})");
        });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QVERIFY2(r.result.ok(), qPrintable(r.result.message));
        QVERIFY(r.value);
        QCOMPARE(fake.requests.size(), 2);
        QCOMPARE(fake.requests.at(1).target, QByteArray("/moved/auth"));
        // The login goes along on a same-host hop.
        QCOMPARE(fake.requests.at(1).headers.value("x-auth-user"), QByteArray("test-reader"));
    }

    void redirectToAnotherHostIsRefusedAndNoLoginIsSentThere()
    {
        FakeHttpServer elsewhere;
        QVERIFY(elsewhere.listen());
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        // "localhost" and "127.0.0.1" are different hosts as far as the rule goes.
        const QByteArray target = "Location: "
                + elsewhere.url(QStringLiteral("localhost")).toEncoded() + "/steal\r\n";
        fake.setHandler([target](QTcpSocket *s, const FakeHttpServer::Request &) {
            FakeHttpServer::reply(s, 302, "", target);
        });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::BadResponse);
        QVERIFY(r.result.message.contains("refused redirect"));
        QCOMPARE(elsewhere.connections(), 0);
    }

    void redirectToAnotherSchemeIsRefused()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        fake.setHandler([](QTcpSocket *s, const FakeHttpServer::Request &) {
            FakeHttpServer::reply(s, 301, "", "Location: ftp://127.0.0.1/x\r\n");
        });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        QCOMPARE(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                     m_client->checkAuth(cb);
                 }).result.error,
                 ErrorKind::BadResponse);
    }

    void redirectLoopsStopAtFive()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        fake.setHandler([](QTcpSocket *s, const FakeHttpServer::Request &) {
            FakeHttpServer::reply(s, 307, "", "Location: /again\r\n");
        });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::BadResponse);
        QVERIFY(r.result.message.contains("too many"));
        QCOMPARE(fake.requests.size(), 6); // the first request plus five hops
    }

    void redirectsAreNotFollowedForWrites()
    {
        FakeHttpServer fake;
        QVERIFY(fake.listen());
        fake.setHandler([](QTcpSocket *s, const FakeHttpServer::Request &) {
            FakeHttpServer::reply(s, 307, "", "Location: /elsewhere\r\n");
        });
        ClientConfig c = config();
        c.serverUrl = fake.url();
        m_client.reset(new QtBookOrbitClient(c));
        m_client->setTimeouts(2000, 2000);
        RemoteProgress p;
        p.document = m_hash;
        p.percentage = 0.5;
        QCOMPARE(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                     m_client->putProgress(p, cb);
                 }).result.error,
                 ErrorKind::BadResponse);
        QCOMPARE(fake.requests.size(), 1);
    }

    void tlsFailsClosedWithAnEmptyCaBundle()
    {
        QTemporaryFile empty;
        QVERIFY(empty.open());
        empty.write("# no certificates here\n");
        empty.flush();
        ClientConfig c = config();
        c.serverUrl = QUrl(QStringLiteral("https://127.0.0.1:1"));
        c.caBundlePath = empty.fileName();
        m_client.reset(new QtBookOrbitClient(c));
        const auto r =
                await<bool>([&](BookOrbitClient::Callback<bool> cb) { m_client->checkAuth(cb); });
        QCOMPARE(r.result.error, ErrorKind::TlsError);
        QVERIFY2(r.result.message.contains("CA bundle"), qPrintable(r.result.message));
        QCOMPARE(m_calls, 1);

        // Plain HTTP is unaffected by the bundle.
        c = config();
        c.caBundlePath = empty.fileName();
        m_client.reset(new QtBookOrbitClient(c));
        QVERIFY(await<bool>([&](BookOrbitClient::Callback<bool> cb) {
                    m_client->checkAuth(cb);
                }).result.ok());
    }

    void factoryMakesAWorkingClient()
    {
        std::unique_ptr<BookOrbitClient> c = createBookOrbitClient(config());
        bool done = false;
        bool ok = false;
        c->checkAuth([&](const Reply<bool> &r) {
            done = true;
            ok = r.result.ok() && r.value;
        });
        (void)QTest::qWaitFor([&] { return done; }, 5000);
        QVERIFY(ok);
    }

    void specConstants()
    {
        QCOMPARE(kConnectTimeoutMs, 5000);
        QCOMPARE(kRequestTimeoutMs, 15000);
    }
};

QTEST_GUILESS_MAIN(TstClient)
#include "tst_client.moc"
