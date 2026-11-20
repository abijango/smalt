// Acceptance test: the download flow against the mock BookOrbit server. Queue
// several books, resume after a dropped connection, save under
// <folder>/<Author>/<Title>.<ext>, record book_map, ask Nickel to import, then
// Open through Nickel.
#include "bookorbit/mock_server.h"
#include "screens/pages.h"
#include "support.h"

using namespace screenstest;
using namespace smalt::ui::kit;

class TstDownload : public QObject
{
    Q_OBJECT

private:
    ::testing::MockServer m_server;

    // A shell on a fresh partition whose catalogue comes from the mock server.
    struct Setup
    {
        std::unique_ptr<BookOrbitClient> client;
        std::unique_ptr<Env> env;
        Shell *shell = nullptr;
    };

    Setup start(smalt::testing::FakeNickel *nickel = nullptr)
    {
        Setup s;
        s.client = createBookOrbitClient(m_server.clientConfig());
        s.env.reset(new Env(DeviceModel::ClaraColour, nickel, s.client.get()));
        s.shell = s.env->makeShell();
        return s;
    }

    static void waitForIndex(Shell *shell)
    {
        QTRY_COMPARE_WITH_TIMEOUT(shell->catalog()->books().size(), 4, 15000);
    }

    QJsonObject fileInfo(int id)
    {
        return m_server.state().value("files").toObject().value(QString::number(id)).toObject();
    }

private slots:

    void initTestCase()
    {
        QVERIFY(pinTestFont());
        const QString error = m_server.start();
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void init() { m_server.reset(); }

    void cleanupTestCase() { m_server.stop(); }

    void theCatalogComesFromTheServer()
    {
        Setup s = start();
        waitForIndex(s.shell);
        const BookItem *b = s.shell->catalog()->find(1);
        QVERIFY(b);
        QCOMPARE(b->title, QStringLiteral("The Salt Road"));
        QCOMPARE(b->seriesName, QStringLiteral("Salt Trilogy"));
        QCOMPARE(s.shell->catalog()->find(3)->formatLabel(), QStringLiteral("CBZ"));
        QCOMPARE(s.shell->catalog()->find(4)->formatLabel(), QStringLiteral("PDF"));
        delete s.shell;
    }

    // Download, save at the spec's path, record book_map, import, then Open.
    void downloadSavesRecordsImportsAndOpens()
    {
        Setup s = start();
        waitForIndex(s.shell);
        Env &env = *s.env;
        s.shell->openBook(1);
        Button *main = buttonNamed(s.shell->currentScreen(), QStringLiteral("main"));
        QVERIFY(main);
        tap(main);
        QTRY_COMPARE_WITH_TIMEOUT(int(s.shell->downloads()->info(1).state),
                                  int(DownloadState::Done), 20000);

        // <downloads>/<Author>/<Title>.<ext>
        const QString path =
                env.paths.downloadsDir() + QStringLiteral("/Ada Quill/The Salt Road.epub");
        QVERIFY2(QFile::exists(path), qPrintable(path));
        QCOMPARE(QFileInfo(path).size(), qint64(fileInfo(101).value("size").toInt()));
        QVERIFY(!QFile::exists(path + QStringLiteral(".part")));

        // book_map: BookOrbit's IDs and the file hash, origin Downloaded.
        BookMapEntry e;
        QVERIFY(env.ledger->bookMapByBookId(1, &e).ok());
        QCOMPARE(e.bookId, qint64(1));
        QCOMPARE(e.bookFileId, qint64(101));
        QCOMPARE(e.partialMd5, partialMd5(path));
        QCOMPARE(e.partialMd5, fileInfo(101).value("hash").toString());
        QCOMPARE(int(e.origin), int(BookOrigin::Downloaded));
        QCOMPARE(e.statsWatermark, qint64(0));
        QVERIFY(e.contentId.endsWith(QStringLiteral("/Ada Quill/The Salt Road.epub")));

        // Nickel was asked to import it.
        QCOMPARE(env.nickel->importedFiles, QStringList() << path);

        // The page now offers Open, which goes through Nickel with that content ID.
        ContentRow row;
        row.contentId = e.contentId;
        env.nickel->putContent(row); // What the import would have added.
        main = buttonNamed(s.shell->currentScreen(), QStringLiteral("main"));
        QVERIFY(main);
        tap(main);
        QCOMPARE(env.nickel->openedBooks, QStringList() << e.contentId);
        delete s.shell;
    }

    // Queue several at once with static progress text: "Downloading 1 of 2".
    void severalBooksQueueWithStaticProgressText()
    {
        Setup s = start();
        waitForIndex(s.shell);
        QStringList texts;
        DownloadManager *dm = s.shell->downloads();
        QObject::connect(dm, &DownloadManager::changed, [&texts, dm]() {
            if (!dm->statusText().isEmpty() && !texts.contains(dm->statusText()))
                texts << dm->statusText();
        });
        s.shell->openSeries(10); // Salt Trilogy: two unread books.
        Button *next = buttonNamed(s.shell->currentScreen(), QStringLiteral("downloadNext"));
        QVERIFY(next);
        tap(next);
        QTRY_VERIFY_WITH_TIMEOUT(!dm->busy(), 30000);
        QCOMPARE(int(dm->info(1).state), int(DownloadState::Done));
        QCOMPARE(int(dm->info(2).state), int(DownloadState::Done));
        QVERIFY2(texts.contains(QStringLiteral("Downloading 1 of 2")), qPrintable(texts.join('|')));
        QVERIFY2(texts.contains(QStringLiteral("Downloading 2 of 2")), qPrintable(texts.join('|')));
        QVERIFY(QFile::exists(s.env->paths.downloadsDir()
                              + QStringLiteral("/Ada Quill/Salt Harvest.epub")));
        QVERIFY(QFile::exists(s.env->paths.downloadsDir()
                              + QStringLiteral("/Ada Quill/The Salt Road.epub")));
        QCOMPARE(s.env->nickel->importedFiles.size(), 2);
        delete s.shell;
    }

    // A dropped connection: the next request continues from the .part file.
    void aDroppedConnectionResumes()
    {
        QJsonObject rule = ::testing::MockServer::rule("/plugin/catalog/files/103/download");
        rule.insert("drop_after_bytes", 200000);
        rule.insert("times", 1);
        m_server.setRules({ rule });
        Setup s = start();
        waitForIndex(s.shell);
        DownloadManager *dm = s.shell->downloads();
        dm->setRetry(2, 50);
        dm->enqueue(*s.shell->catalog()->find(3));
        QTRY_COMPARE_WITH_TIMEOUT(int(dm->info(3).state), int(DownloadState::Done), 30000);

        const QString path =
                s.env->paths.downloadsDir() + QStringLiteral("/Bo Marsh/Tidewrack.cbz");
        QVERIFY2(QFile::exists(path), qPrintable(path));
        QCOMPARE(QFileInfo(path).size(), qint64(1200000));
        QCOMPARE(partialMd5(path), fileInfo(103).value("hash").toString());
        // Two requests: the second asked only for the rest.
        const QJsonArray log = m_server.requestsTo("/plugin/catalog/files/103/download");
        QCOMPARE(log.size(), 2);
        QVERIFY(!log.at(0).toObject().value("headers").toObject().contains("range"));
        QVERIFY(log.at(1)
                        .toObject()
                        .value("headers")
                        .toObject()
                        .value("range")
                        .toString()
                        .startsWith(QStringLiteral("bytes=")));
        // Comics and PDFs are recorded like books; Kobo's reader opens them (the bridge decides).
        BookMapEntry e;
        QVERIFY(s.env->ledger->bookMapByBookId(3, &e).ok());
        QCOMPARE(e.bookFileId, qint64(103));
        delete s.shell;
    }

    // When the drop repeats beyond the retries the book shows an error and can be tried again.
    void aFailedDownloadCanBeTriedAgain()
    {
        QJsonObject rule = ::testing::MockServer::rule("/plugin/catalog/files/104/download");
        rule.insert("drop_after_bytes", 5000);
        rule.insert("times", 3);
        m_server.setRules({ rule });
        Setup s = start();
        waitForIndex(s.shell);
        DownloadManager *dm = s.shell->downloads();
        dm->setRetry(1, 20);
        dm->enqueue(*s.shell->catalog()->find(4));
        QTRY_COMPARE_WITH_TIMEOUT(int(dm->info(4).state), int(DownloadState::Failed), 20000);
        QVERIFY(!dm->info(4).error.isEmpty());
        BookMapEntry e;
        QVERIFY(!s.env->ledger->bookMapByBookId(4, &e).ok()); // Nothing recorded for a failure.
        // Tap Download again: the rule has one drop left, the retry gets through.
        dm->enqueue(*s.shell->catalog()->find(4));
        QTRY_COMPARE_WITH_TIMEOUT(int(dm->info(4).state), int(DownloadState::Done), 20000);
        QVERIFY(s.env->ledger->bookMapByBookId(4, &e).ok());
        delete s.shell;
    }

    // On the Kobo the import call isn't there until task 30: the download still
    // completes, is recorded, and a plain message is shown.
    void anImportThatIsNotThereYetIsAPlainMessage()
    {
        UnimplementedNickel nickel;
        Setup s = start(&nickel);
        waitForIndex(s.shell);
        s.shell->downloads()->enqueue(*s.shell->catalog()->find(1));
        QTRY_COMPARE_WITH_TIMEOUT(int(s.shell->downloads()->info(1).state),
                                  int(DownloadState::Done), 20000);
        BookMapEntry e;
        QVERIFY(s.env->ledger->bookMapByBookId(1, &e).ok());
        QVERIFY(s.shell->dialogOpen());
        delete s.shell;
    }

    void fileNamesAreSafe()
    {
        QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("A/B: C?"), QStringLiteral("x")),
                 QStringLiteral("A B C"));
        QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("  ..Dots.. "),
                                                   QStringLiteral("x")),
                 QStringLiteral("Dots"));
        QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("***"),
                                                   QStringLiteral("Untitled")),
                 QStringLiteral("Untitled"));
        QVERIFY(DownloadManager::sanitizeFileName(QString(300, QLatin1Char('x')),
                                                  QStringLiteral("x"))
                        .size()
                <= 100);

        Setup s = start();
        BookItem b;
        b.id = 77;
        b.title = QStringLiteral("What If? Part 1/2");
        b.authors << QStringLiteral("R. Munroe/Co");
        const QString path = s.shell->downloads()->destinationFor(b, QStringLiteral("EPUB"));
        QVERIFY(path.endsWith(QStringLiteral("/R. Munroe Co/What If Part 1 2.epub")));
        // A name another book already owns gets the book ID added.
        BookMapEntry other;
        other.contentId = QStringLiteral("file://") + path;
        other.bookId = 5;
        other.bookFileId = 5;
        other.partialMd5 = QStringLiteral("00000000000000000000000000000005");
        QVERIFY(s.env->ledger->putBookMap(other).ok());
        QVERIFY(s.shell->downloads()
                        ->destinationFor(b, QStringLiteral("epub"))
                        .endsWith(QStringLiteral("/What If Part 1 2 (77).epub")));
        delete s.shell;
    }

    // Settings > Download folder decides where books go (absolute or under the partition).
    void theDownloadFolderSettingIsUsed()
    {
        Setup s = start();
        waitForIndex(s.shell);
        Settings settings = s.env->settings->current();
        settings.downloadFolder = QStringLiteral("MyBooks");
        QVERIFY(s.env->settings->save(settings).ok());
        BookItem b = *s.shell->catalog()->find(1);
        QCOMPARE(s.shell->downloads()->destinationFor(b, QStringLiteral("epub")),
                 s.env->paths.onboard() + QStringLiteral("/MyBooks/Ada Quill/The Salt Road.epub"));
        settings.downloadFolder = s.env->dir.path() + QStringLiteral("/elsewhere");
        QVERIFY(s.env->settings->save(settings).ok());
        QCOMPARE(s.shell->downloads()->destinationFor(b, QStringLiteral("epub")),
                 s.env->dir.path() + QStringLiteral("/elsewhere/Ada Quill/The Salt Road.epub"));
        delete s.shell;
    }

    // A newer file on the server: "Update available" fetches it and records the new file.
    void updateAvailableReplacesTheFile()
    {
        Setup s = start();
        waitForIndex(s.shell);
        s.env->putOnKobo(1); // book_map says file 1001; the server's file is 101.
        BookMapEntry old;
        QVERIFY(s.env->ledger->bookMapByBookId(1, &old).ok());
        old.bookFileId = 55; // Not the server's current file.
        QVERIFY(s.env->ledger->putBookMap(old).ok());
        s.shell->catalog()->invalidateLocal();
        s.shell->openBook(1);
        QTRY_VERIFY_WITH_TIMEOUT(
                buttonNamed(s.shell->currentScreen(), QStringLiteral("main")) != nullptr, 5000);
        // Wait for the detail to arrive, then the button is "Update available".
        QTest::qWait(300);
        Button *main = buttonNamed(s.shell->currentScreen(), QStringLiteral("main"));
        QVERIFY(main);
        tap(main);
        QTRY_COMPARE_WITH_TIMEOUT(int(s.shell->downloads()->info(1).state),
                                  int(DownloadState::Done), 20000);
        BookMapEntry now;
        QVERIFY(s.env->ledger->bookMapByBookId(1, &now).ok());
        QCOMPARE(now.bookFileId, qint64(101));
        delete s.shell;
    }
};

QTEST_MAIN(TstDownload)
#include "tst_download.moc"
