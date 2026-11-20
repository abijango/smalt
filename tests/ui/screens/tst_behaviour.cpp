// Behaviour of the shell and its screens: device layouts, paged lists with a
// counter and jump-to-letter, page-turn keys, chips that stay put and clear in
// one tap, grouped search, tab navigation, the removal banner, the main button
// of the book page, status and rating changes.
#include "screens/pages.h"
#include "support.h"

#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>

using namespace screenstest;
using namespace smalt::ui::kit;

namespace {

ColumnScreen *column(Shell *shell)
{
    return qobject_cast<ColumnScreen *>(shell->currentScreen());
}

// The first visible block's sort key (the title of its first row).
QString firstKey(Shell *shell)
{
    const QList<QWidget *> blocks = column(shell)->column()->visibleBlocks();
    return blocks.isEmpty() ? QString() : blocks.first()->property("sortKey").toString();
}

void prepare(Env &env)
{
    env.seedCache();
    env.seedKobo();
}

} // namespace

class TstBehaviour : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    // "Reading now: 3 covers on the Claras, 4 on the Libra. Up next: 1 row on
    // the Claras, 2 on the Libra."
    void deviceLayouts_data()
    {
        QTest::addColumn<int>("model");
        QTest::addColumn<int>("covers");
        QTest::addColumn<int>("upNextRows");
        QTest::newRow("clara colour") << int(DeviceModel::ClaraColour) << 3 << 1;
        QTest::newRow("clara bw") << int(DeviceModel::ClaraBW) << 3 << 1;
        QTest::newRow("libra colour") << int(DeviceModel::LibraColour) << 4 << 2;
    }

    void deviceLayouts()
    {
        QFETCH(int, model);
        QFETCH(int, covers);
        QFETCH(int, upNextRows);
        Env env{ DeviceModel(model) };
        prepare(env);
        Shell *shell = env.makeShell();
        ColumnScreen *home = column(shell);
        const QList<QWidget *> blocks = home->column()->blocks();
        QVERIFY(blocks.size() >= 2);
        // Block 0 is Reading now, block 1 is Up next.
        CoverRow *row = blocks.at(0)->findChild<CoverRow *>();
        QVERIFY(row);
        QSignalSpy tapped(row, SIGNAL(coverTapped(int)));
        for (int i = 0; i < 4; ++i) {
            QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier,
                              row->coverRect(i).center() + QPoint(0, 2));
        }
        QCOMPARE(tapped.count(), covers); // Only the covers that are drawn answer taps.
        QCOMPARE(blocks.at(1)->findChildren<ListRow *>().size(), upNextRows);
        delete shell;
    }

    // Long lists are pages with a counter and the page buttons flip them.
    void pagedLibrary()
    {
        Env env(DeviceModel::LibraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(1);
        PagedColumn *list = column(shell)->column();
        QVERIFY(list->pagerShown());
        QVERIFY(list->pageCount() > 3);
        QCOMPARE(list->page(), 0);
        const QString first = firstKey(shell);
        // Libra page-turn buttons are routed to flipPage(); on the desktop PageDown/PageUp.
        QTest::keyClick(shell, Qt::Key_PageDown);
        QCOMPARE(list->page(), 1);
        QVERIFY(firstKey(shell) != first);
        QTest::keyClick(shell, Qt::Key_PageUp);
        QCOMPARE(list->page(), 0);
        QCOMPARE(firstKey(shell), first);
        QVERIFY(shell->flipPage(1));
        QCOMPARE(list->page(), 1);
        // The pager's arrows flip too.
        Pager *pager = findFirst<Pager>(list);
        QVERIFY(pager);
        QCOMPARE(pager->page(), 2);
        QTest::mouseClick(pager, Qt::LeftButton, Qt::NoModifier,
                          QPoint(pager->width() - 10, pager->height() / 2));
        QCOMPARE(list->page(), 2);
        delete shell;
    }

    // Tapping the counter opens the letter grid; a letter jumps to its first title.
    void jumpToLetter()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(1);
        PagedColumn *list = column(shell)->column();
        Pager *pager = findFirst<Pager>(list);
        QVERIFY(pager);
        tap(pager); // The centre is the counter.
        LetterGrid *grid = findFirst<LetterGrid>(list);
        QVERIFY2(grid, "the letter grid should open");
        const int index = LetterGrid::letters().indexOf(QLatin1Char('H'));
        QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, grid->cellRect(index).center());
        QCoreApplication::processEvents();
        QVERIFY(!findFirst<LetterGrid>(list)); // The grid closes.
        QVERIFY2(firstKey(shell).startsWith(QLatin1Char('H'))
                         || firstKey(shell).at(0) < QLatin1Char('H'),
                 qPrintable(firstKey(shell)));
        // The page before ends before H; this page holds the first H title or the one
        // that shares its page.
        const int page = list->page();
        QVERIFY(page > 0);
        QCOMPARE(list->pageForLetter(QStringLiteral("H")), page);
        // "#" goes to the start.
        QCOMPARE(list->pageForLetter(QStringLiteral("#")), 0);
        delete shell;
    }

    // Chips stay put per tab and one tap clears them all.
    void chipsAreRememberedAndClear()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(1);
        ColumnScreen *library = column(shell);
        const int total = library->column()->blocks().size();
        QCOMPARE(total, 60);
        QList<ChipRow *> rows = library->findChildren<ChipRow *>();
        QCOMPARE(rows.size(), 2);
        ChipRow *formats = rows.first()->chipRects().size() == 4 ? rows.first() : rows.last();
        // "Comics" is the second chip of the first row.
        QTest::mouseClick(formats, Qt::LeftButton, Qt::NoModifier,
                          formats->chipRects().at(1).center());
        QCoreApplication::processEvents();
        QVERIFY(shell->chipMemory(QStringLiteral("library.a")).contains(1));
        // Filtered: the comics (every seventh book) and the banner with Clear.
        const QList<QWidget *> filtered = column(shell)->column()->blocks();
        QCOMPARE(filtered.size(), 1 + 8);
        QVERIFY(qobject_cast<Banner *>(filtered.first()));

        // Another tab and back: the chip is still on.
        shell->switchTab(2);
        shell->switchTab(1);
        QVERIFY(shell->chipMemory(QStringLiteral("library.a")).contains(1));
        QCOMPARE(column(shell)->column()->blocks().size(), 1 + 8);

        // One tap clears everything.
        Banner *banner = qobject_cast<Banner *>(column(shell)->column()->blocks().first());
        QVERIFY(banner);
        QTest::mouseClick(banner, Qt::LeftButton, Qt::NoModifier, banner->actionRect().center());
        QCoreApplication::processEvents();
        QVERIFY(shell->chipMemory(QStringLiteral("library.a")).isEmpty());
        QCOMPARE(column(shell)->column()->blocks().size(), 60);
        delete shell;
    }

    // "On Kobo shows only downloaded items."
    void onKoboChip()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(1);
        shell->chipMemory(QStringLiteral("library.a")).append(3);
        column(shell)->rebuild();
        // Five books were put on the Kobo, plus the banner.
        QCOMPARE(column(shell)->column()->blocks().size(), 1 + 5);
        delete shell;
    }

    void statusFilterUsesBookOrbitsStatuses()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        const Catalog *cat = shell->catalog();
        LibraryFilter f;
        f.finished = true;
        QCOMPARE(cat->filtered(f).size(), 3); // 1, 5 and 6
        f = LibraryFilter();
        f.reading = true;
        // 2, 30, 31 from the server, 7 and 21 from this Kobo's own status.
        QCOMPARE(cat->filtered(f).size(), 5);
        f = LibraryFilter();
        f.unread =
                true; // Unread and Want to read both count; Abandoned and the reading ones don't.
        QCOMPARE(cat->filtered(f).size(), 60 - 3 - 5 - 1);
        delete shell;
    }

    // Search is one tap away in the header of every tab and prompts for the words.
    void headerSearchPromptsAndShowsGroupedResults()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(2);
        HeaderBar *header = qobject_cast<HeaderBar *>(shell->headerWidget());
        QVERIFY(header);
        QTest::mouseClick(header, Qt::LeftButton, Qt::NoModifier, header->searchRect().center());
        QCoreApplication::processEvents();
        QVERIFY(shell->promptOpen());
        QLineEdit *edit = shell->prompt()->findChild<QLineEdit *>();
        QVERIFY(edit);
        QTest::keyClicks(edit, QStringLiteral("amber"));
        QList<Button *> buttons = shell->prompt()->findChildren<Button *>();
        QCOMPARE(buttons.size(), 2);
        tap(buttons.last()); // Done
        QVERIFY(!shell->promptOpen());
        auto *search = qobject_cast<SearchScreen *>(shell->currentScreen());
        QVERIFY(search);
        QCOMPARE(search->query(), QStringLiteral("amber"));
        // Series, then Books: a header and one series, a header and the five Amber books.
        QCOMPARE(search->column()->blocks().size(), 2 + 5 + 1);
        delete shell;
    }

    void searchGroupsSeriesAuthorsAndBooks()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openSearch(QStringLiteral("ada"), false);
        auto *search = qobject_cast<SearchScreen *>(shell->currentScreen());
        QVERIFY(search);
        // Authors: a header and Ada Quill; then a Books header and her ten books.
        QCOMPARE(search->column()->blocks().size(), 2 + 1 + 10);
        // Tapping the author searches for the name on the same screen.
        QCOMPARE(shell->depth(), 2);
        delete shell;
    }

    void offlineSearchOnlyFindsBooksOnTheKoboAndSaysSo()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        shell->openSearch(QStringLiteral("amber"), false);
        auto *search = qobject_cast<SearchScreen *>(shell->currentScreen());
        QVERIFY(search);
        const QList<QWidget *> blocks = search->column()->blocks();
        // The note, the series that has a book here, and the one Amber book on the Kobo.
        QCOMPARE(blocks.size(), 1 + 2 + 2);
        TextBlock *note = qobject_cast<TextBlock *>(blocks.first());
        QVERIFY(note);
        QCOMPARE(note->text(),
                 QStringLiteral("You're offline. Searching only the books on this Kobo."));
        delete shell;
    }

    void tabsNavigateAndTheStatusLineIsOnHomeAndMore()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        QVERIFY(shell->statusWidget()); // Home
        BottomTabs *tabs = qobject_cast<BottomTabs *>(shell->tabsWidget());
        QVERIFY(tabs);
        QCOMPARE(tabs->tabRect(0).width() > 0, true);
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(1).center());
        QCOMPARE(shell->currentTab(), 1);
        QVERIFY(!shell->statusWidget());
        tabs = qobject_cast<BottomTabs *>(shell->tabsWidget());
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(4).center());
        QCOMPARE(shell->currentTab(), 4);
        QVERIFY(shell->statusWidget());
        // Back from a pushed page.
        shell->openBook(3);
        QCOMPARE(shell->depth(), 2);
        shell->pop();
        QCOMPARE(shell->depth(), 1);
        delete shell;
    }

    void removalBannerAndReview()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        for (int i = 0; i < 3; ++i) {
            PendingDecision d;
            d.contentId = QStringLiteral("file:///removed/%1.epub").arg(i);
            d.createdUtc = QDateTime::currentDateTimeUtc();
            QVERIFY(env.ledger->addPendingDecision(d).ok());
        }
        Shell *shell = env.makeShell();
        QCOMPARE(shell->pendingRemovals(), 3);
        Banner *banner = qobject_cast<Banner *>(column(shell)->column()->blocks().first());
        QVERIFY(banner);
        // Nobody listens yet (task 33): a plain message, no crash.
        QTest::mouseClick(banner, Qt::LeftButton, Qt::NoModifier, banner->actionRect().center());
        QCoreApplication::processEvents();
        QVERIFY(shell->dialogOpen());
        Button *ok = findFirst<Button>(shell->dialogWidget());
        QVERIFY(ok);
        tap(ok);
        QVERIFY(!shell->dialogOpen());
        // With the review screen connected, the signal fires instead.
        QSignalSpy review(shell, SIGNAL(reviewRemovalsRequested()));
        banner = qobject_cast<Banner *>(column(shell)->column()->blocks().first());
        QTest::mouseClick(banner, Qt::LeftButton, Qt::NoModifier, banner->actionRect().center());
        QCOMPARE(review.count(), 1);
        QVERIFY(!shell->dialogOpen());
        delete shell;
    }

    // The main button: Open for a book on the Kobo (through Nickel), Download otherwise.
    void mainButtonOpensBooksOnTheKobo()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openBook(2);
        Button *main = buttonNamed(shell->currentScreen(), QStringLiteral("main"));
        QVERIFY(main);
        tap(main);
        QCOMPARE(env.nickel->openedBooks.size(), 1);
        BookMapEntry e;
        QVERIFY(env.ledger->bookMapByBookId(2, &e).ok());
        QCOMPARE(env.nickel->openedBooks.first(), e.contentId);
        delete shell;
    }

    // On the Kobo the Nickel calls aren't there until task 30: a plain message.
    void openWithoutNickelSupportShowsAMessage()
    {
        UnimplementedNickel nickel;
        Env env(DeviceModel::ClaraColour, &nickel);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openBook(2);
        tap(buttonNamed(shell->currentScreen(), QStringLiteral("main")));
        QVERIFY(shell->dialogOpen());
        tap(findFirst<Button>(shell->dialogWidget()));
        QVERIFY(!shell->dialogOpen());
        // Kobo home in More says the same, without crashing.
        shell->switchTab(4);
        TextRow *kobo = rowTitled(shell->currentScreen(), QStringLiteral("Kobo home"));
        QVERIFY(kobo);
        tap(kobo);
        QVERIFY(shell->dialogOpen());
        delete shell;
    }

    void mainButtonDownloadsBooksNotOnTheKobo()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openBook(3);
        Button *main = buttonNamed(shell->currentScreen(), QStringLiteral("main"));
        QVERIFY(main);
        tap(main);
        // The fake client can't serve files, so the download stops with a message on the page.
        QCOMPARE(shell->downloads()->info(3).state, DownloadState::Failed);
        delete shell;
    }

    void kobosHomeIsAtTheTopOfMore()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(4);
        const QList<QWidget *> blocks = column(shell)->column()->blocks();
        QCOMPARE(blocks.size(), 4);
        TextRow *first = qobject_cast<TextRow *>(blocks.first());
        QVERIFY(first);
        QCOMPARE(first->title(), QStringLiteral("Kobo home"));
        tap(first);
        QCOMPARE(env.nickel->nickelHomeShown, 1);
        delete shell;
    }

    void statusChangeOnTheKoboGoesToTheLedger()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openBook(13); // On the Kobo, no status yet.
        TextRow *status = rowTitled(shell->currentScreen(), QStringLiteral("Status"));
        // Status and rating sit below the first page of a Clara; flip until they show.
        while (!status && shell->flipPage(1))
            status = rowTitled(shell->currentScreen(), QStringLiteral("Status"));
        QVERIFY(status);
        tap(status);
        // The choices: Unread, Want to read, Reading, On hold, Read, Abandoned.
        auto *choice = qobject_cast<ChoiceScreen *>(shell->currentScreen());
        QVERIFY(choice);
        QCOMPARE(choice->column()->blocks().size(), 6);
        tap(qobject_cast<TextRow *>(choice->column()->blocks().at(4))); // Read
        QCOMPARE(shell->depth(), 2);
        BookMapEntry e;
        QVERIFY(env.ledger->bookMapByBookId(13, &e).ok());
        BookStateRecord rec;
        QVERIFY(env.ledger->bookState(e.contentId, &rec).ok());
        QCOMPARE(int(rec.status), int(ReadStatus::Read));
        delete shell;
    }

    void statusChangeForABookNotHereGoesToBookOrbit()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openBook(3);
        TextRow *status = rowTitled(shell->currentScreen(), QStringLiteral("Status"));
        while (!status && shell->flipPage(1))
            status = rowTitled(shell->currentScreen(), QStringLiteral("Status"));
        QVERIFY(status);
        tap(status);
        auto *choice = qobject_cast<ChoiceScreen *>(shell->currentScreen());
        QVERIFY(choice);
        tap(qobject_cast<TextRow *>(choice->column()->blocks().at(5))); // Abandoned
        QCOMPARE(env.fake.statusCalls, QStringList() << QStringLiteral("abandoned"));
        QCOMPARE(int(shell->catalog()->find(3)->status), int(ReadStatus::Abandoned));
        delete shell;
    }

    void seriesDetailQueuesTheNextThree()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->openSeries(1); // 1 read, 2 reading here, 3 and 4 to come.
        Button *next = buttonNamed(shell->currentScreen(), QStringLiteral("downloadNext"));
        QVERIFY(next);
        Button *cont = buttonNamed(shell->currentScreen(), QStringLiteral("continue"));
        QVERIFY(cont);
        tap(cont); // Continue opens the book in progress through Nickel.
        QCOMPARE(env.nickel->openedBooks.size(), 1);
        tap(next);
        QVERIFY(shell->downloads()->info(3).state != DownloadState::None);
        QVERIFY(shell->downloads()->info(4).state != DownloadState::None);
        delete shell;
    }

    // Task 23's widgets arrive through ShellDeps: the Stats tab, and "Your reading" on the
    // book page of a book read here. Without them: a placeholder and nothing.
    void statsTabAndYourReadingComeFromTheDeps()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        QStringList asked;
        env.deps.statsTab = [](QWidget *parent) {
            QLabel *l = new QLabel(QStringLiteral("STATS"), parent);
            l->setObjectName(QStringLiteral("statsWidget"));
            return static_cast<QWidget *>(l);
        };
        env.deps.yourReading = [&asked](const QString &contentId, QWidget *parent) {
            asked << contentId;
            QLabel *l = new QLabel(QStringLiteral("YOUR READING"), parent);
            l->setObjectName(QStringLiteral("yourReading"));
            l->setFixedHeight(80);
            return static_cast<QWidget *>(l);
        };
        Shell *shell = env.makeShell();
        shell->switchTab(3);
        QVERIFY(shell->currentScreen()->findChild<QLabel *>(QStringLiteral("statsWidget")));
        // On the Kobo: the section is asked for with the book's content ID.
        shell->switchTab(1);
        shell->openBook(2);
        BookMapEntry e;
        QVERIFY(env.ledger->bookMapByBookId(2, &e).ok());
        QVERIFY(asked.contains(e.contentId));
        // Not on the Kobo: no section.
        asked.clear();
        shell->pop();
        shell->openBook(3);
        QVERIFY(asked.isEmpty());
        delete shell;

        // Without the callbacks: a placeholder, and the book page still builds.
        Env bare(DeviceModel::ClaraColour);
        prepare(bare);
        Shell *plain = bare.makeShell();
        plain->switchTab(3);
        QVERIFY(plain->currentScreen()->findChild<TextBlock *>());
        plain->switchTab(1);
        plain->openBook(2);
        QCOMPARE(plain->depth(), 2);
        delete plain;
    }

    void coversComeFromTheCacheWithoutTheNetwork()
    {
        Env env(DeviceModel::ClaraColour);
        prepare(env);
        Shell *shell = env.makeShell();
        shell->switchTab(1);
        QCoreApplication::processEvents();
        QCOMPARE(env.fake.thumbnailCalls, 0);
        QVERIFY(!shell->catalog()->cover(3).isNull());
        delete shell;
    }
};

QTEST_MAIN(TstBehaviour)
#include "tst_behaviour.moc"
