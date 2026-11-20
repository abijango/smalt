// Acceptance test: offline mode shows cached pages and marks undownloaded items
// unavailable (docs/tasks/22-screens.md; SPEC, "Large-library UX").
#include "screens/pages.h"
#include "support.h"

using namespace screenstest;
using namespace smalt::ui::kit;

namespace {

ColumnScreen *column(Shell *shell)
{
    return qobject_cast<ColumnScreen *>(shell->currentScreen());
}

// A kit list row for a book, as the screens build it.
ListRow *rowFor(Shell *shell, int bookId, QWidget *parent)
{
    const BookItem *b = shell->catalog()->find(bookId);
    if (!b)
        return nullptr;
    return qobject_cast<ListRow *>(makeBookRow(shell, *b, QString(), parent));
}

} // namespace

class TstOffline : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void cachedPagesShowWithoutAnyRequest()
    {
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.seedKobo();
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        QCoreApplication::processEvents();
        QCOMPARE(shell->catalog()->books().size(), 60); // From remote_cache.
        QVERIFY(!shell->catalog()->online());
        QCOMPARE(env.fake.booksCalls, 0); // Nothing asked of the network.
        QCOMPARE(env.fake.thumbnailCalls, 0); // Covers come from the cache too.
        // Home shows Reading now from the cache.
        QVERIFY(column(shell)->column()->blocks().at(0)->findChild<CoverRow *>());
        shell->switchTab(1);
        QCOMPARE(column(shell)->column()->blocks().size(), 60);
        shell->switchTab(2);
        QVERIFY(column(shell)->column()->blocks().size() >= 6); // Six series.
        delete shell;
    }

    void undownloadedItemsAreMarkedUnavailable()
    {
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.seedKobo();
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        QWidget host;
        // On the Kobo: a check. Not downloaded: no download badge at all.
        ListRow *here = rowFor(shell, 2, &host);
        ListRow *away = rowFor(shell, 3, &host);
        QVERIFY(here && away);
        QVERIFY(!here->badgeRect().isNull());
        QVERIFY(away->badgeRect().isNull());
        delete shell;
    }

    void theBookPageSaysItIsUnavailableAndDoesNotDownload()
    {
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.seedKobo();
        QJsonObject detail = fakeBook(3);
        detail.insert("description", QStringLiteral("Cached description of the book."));
        env.ledger->cachePut(QStringLiteral("book:3"), "book",
                             QJsonDocument(detail).toJson(QJsonDocument::Compact),
                             QDateTime::currentDateTimeUtc());
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        shell->openBook(3);
        // The cached detail is on the page.
        bool found = false;
        const QList<QWidget *> blocks = column(shell)->column()->blocks();
        for (QWidget *w : blocks) {
            TextBlock *t = qobject_cast<TextBlock *>(w);
            found = found || (t && t->text().contains(QLatin1String("Cached description")));
        }
        QVERIFY(found);
        Button *main = buttonNamed(shell->currentScreen(), QStringLiteral("main"));
        QVERIFY(main);
        tap(main);
        QCOMPARE(int(shell->downloads()->info(3).state), int(DownloadState::None));
        // A download started from a list says why not.
        shell->startDownload(*shell->catalog()->find(3));
        QVERIFY(shell->dialogOpen());
        delete shell;
    }

    void comingBackOnlineBringsTheDownloadBadgesBack()
    {
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.seedKobo();
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        QWidget host;
        QVERIFY(rowFor(shell, 3, &host)->badgeRect().isNull());
        env.fake.offline = false;
        env.nickel->simulateNetwork(true); // Wi-Fi is back: the shell refreshes.
        QCoreApplication::processEvents();
        QVERIFY(shell->catalog()->online());
        QVERIFY(env.fake.booksCalls > 0);
        QVERIFY(!rowFor(shell, 3, &host)->badgeRect().isNull());
        delete shell;
    }

    void shelvesWorkFromTheCache()
    {
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.ledger->cachePut(QStringLiteral("shelves:v1"), "shelves",
                             QByteArray("[{\"id\":\"collection-7\",\"title\":\"Summer reading\"}]"),
                             QDateTime::currentDateTimeUtc());
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        shell->openShelves();
        QCOMPARE(column(shell)->column()->blocks().size(), 1);
        TextRow *row = qobject_cast<TextRow *>(column(shell)->column()->blocks().first());
        QVERIFY(row);
        QCOMPARE(row->title(), QStringLiteral("Summer reading"));
        delete shell;
    }

    // First run, no cache, no network: a plain message instead of an empty screen.
    void nothingCachedSaysSo()
    {
        Env env(DeviceModel::ClaraColour);
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        const QList<QWidget *> blocks = column(shell)->column()->blocks();
        QCOMPARE(blocks.size(), 1);
        TextBlock *note = qobject_cast<TextBlock *>(blocks.first());
        QVERIFY(note);
        QCOMPARE(note->text(),
                 QStringLiteral("Nothing to show yet. Connect to Wi-Fi to load your library."));
        delete shell;
    }

    // Pages are cached as they are fetched, for the next start.
    void aRefreshFillsTheCache()
    {
        Env env(DeviceModel::ClaraColour);
        Shell *shell = env.makeShell();
        QTRY_COMPARE(shell->catalog()->books().size(), 60);
        QByteArray meta, page;
        QDateTime when;
        QVERIFY(env.ledger->cacheGet(QStringLiteral("catalog:books:meta"), &meta, &when).ok());
        QVERIFY(env.ledger->cacheGet(QStringLiteral("catalog:books:1"), &page, &when).ok());
        delete shell;
        // A second shell on the same ledger starts from the cache with the network down.
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *again = env.makeShell();
        QCOMPARE(again->catalog()->books().size(), 60);
        delete again;
    }
};

QTEST_MAIN(TstOffline)
#include "tst_offline.moc"
