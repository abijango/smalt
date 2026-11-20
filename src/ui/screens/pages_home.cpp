// Home, Stats (host) and More.
#include "copy.h"
#include "pages_common.h"
#include "smalt/nickel.h"

namespace smalt {
namespace screens {

namespace {

constexpr int kOtherDevicesRows = 3;
constexpr int kRecentlyAddedRows = 6;

LibraryFilter formatFilter(const QList<int> &chips)
{
    LibraryFilter f;
    f.books = chips.contains(0);
    f.comics = chips.contains(1);
    f.pdfs = chips.contains(2);
    return f;
}

} // namespace

// ---- Home ----

HomeScreen::HomeScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent) { }

QList<QWidget *> HomeScreen::buildBlocks()
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    ui::ComponentFactory *kit = sh->look().kit();
    const DeviceProfile &profile = sh->look().profile();
    QWidget *parent = column();
    QList<QWidget *> out;

    const int removals = sh->pendingRemovals();
    if (removals > 0) {
        QWidget *banner = kit->banner(copy::removalBanner(removals), copy::removalReview(), parent);
        connect(banner, SIGNAL(actionTapped()), this, SLOT(onReview()));
        out << banner;
    }
    if (cat->indexEmpty()) {
        out << textBlock(sh, emptyLibraryMessage(sh), TextBlock::Secondary, parent);
        return out;
    }

    // Each short section is one block, so it moves to the next page whole.
    // Reading now: everything in progress on this Kobo, newest first.
    m_readingNow = cat->readingNow();
    StackBlock *reading = new StackBlock(parent);
    reading->add(kit->sectionHeader(QStringLiteral("Reading now"), m_readingNow.size(), reading));
    if (m_readingNow.isEmpty()) {
        reading->add(textBlock(sh, copy::nothingInProgress(), TextBlock::Secondary, reading));
    } else {
        QList<ui::CoverSpec> covers;
        for (int i = 0; i < m_readingNow.size() && i < profile.readingNowCovers; ++i)
            covers << cat->coverSpec(m_readingNow.at(i), true);
        QWidget *row = kit->coverRow(covers, reading);
        connect(row, SIGNAL(coverTapped(int)), this, SLOT(onCoverTapped(int)));
        reading->add(row);
    }
    out << reading;

    // Up next in your series: the next unread book in each series you're reading.
    const QVector<BookItem> next = cat->upNext();
    if (!next.isEmpty()) {
        StackBlock *upNext = new StackBlock(parent);
        upNext->add(
                kit->sectionHeader(QStringLiteral("Up next in your series"), next.size(), upNext));
        for (int i = 0; i < next.size() && i < profile.upNextRows; ++i) {
            const BookItem &b = next.at(i);
            QString sub = b.seriesName;
            if (b.seriesIndex >= 0)
                sub += QStringLiteral(" · Book %1").arg(QString::number(b.seriesIndex, 'g', 4));
            upNext->add(makeBookRow(sh, b, sub, upNext));
        }
        out << upNext;
    }

    // Continue from other devices.
    const QVector<BookItem> others = cat->fromOtherDevices();
    if (!others.isEmpty()) {
        StackBlock *other = new StackBlock(parent);
        other->add(kit->sectionHeader(QStringLiteral("Continue from other devices"), others.size(),
                                      other));
        for (int i = 0; i < others.size() && i < kOtherDevicesRows; ++i) {
            const BookItem &b = others.at(i);
            QStringList parts;
            if (!b.authors.isEmpty())
                parts << b.authorLine();
            if (b.progress > 0)
                parts << QStringLiteral("%1%").arg(int(b.progress * 100 + 0.5));
            other->add(makeBookRow(sh, b, parts.join(QStringLiteral(" · ")), other));
        }
        out << other;
    }

    // Recently added, with format chips.
    QList<int> &chips = sh->chipMemory(QStringLiteral("home"));
    const QVector<BookItem> recent = cat->recentlyAdded(formatFilter(chips));
    QWidget *h = kit->sectionHeader(QStringLiteral("Recently added"), recent.size(), parent);
    keepWithNext(h);
    out << h;
    QWidget *chipRow =
            kit->chipRow(QStringList() << QStringLiteral("Books") << QStringLiteral("Comics")
                                       << QStringLiteral("PDFs"),
                         parent);
    setChipSelection(chipRow, chips);
    connect(chipRow, SIGNAL(chipToggled(int, bool)), this, SLOT(onChip(int, bool)));
    keepWithNext(chipRow);
    out << chipRow;
    if (recent.isEmpty())
        out << textBlock(sh, copy::noMatches(), TextBlock::Secondary, parent);
    for (int i = 0; i < recent.size() && i < kRecentlyAddedRows; ++i)
        out << makeBookRow(sh, recent.at(i), bookSubtitle(sh, recent.at(i), true), parent);
    return out;
}

void HomeScreen::onCoverTapped(int index)
{
    if (index >= 0 && index < m_readingNow.size())
        shell()->openBook(m_readingNow.at(index).id);
}

void HomeScreen::onReview()
{
    shell()->requestRemovalReview();
}

void HomeScreen::onChip(int index, bool selected)
{
    QList<int> &chips = shell()->chipMemory(QStringLiteral("home"));
    if (selected && !chips.contains(index))
        chips.append(index);
    else if (!selected)
        chips.removeAll(index);
    rebuild();
}

// ---- Stats (task 23 fills it) ----

StatsScreen::StatsScreen(Shell *shell, QWidget *parent) : Screen(shell, parent) { }

void StatsScreen::rebuild()
{
    if (m_content)
        return;
    const ShellDeps &deps = shell()->deps();
    if (deps.statsTab)
        m_content = deps.statsTab(this);
    if (!m_content)
        m_content = textBlock(shell(), QStringLiteral("Stats aren't available yet."),
                              TextBlock::Secondary, this);
    m_content->setGeometry(rect());
    m_content->show();
}

void StatsScreen::resizeEvent(QResizeEvent *)
{
    if (m_content)
        m_content->setGeometry(rect());
}

// ---- More ----

MoreScreen::MoreScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent) { }

QList<QWidget *> MoreScreen::buildBlocks()
{
    Shell *sh = shell();
    QWidget *parent = column();
    QList<QWidget *> out;

    struct Item
    {
        const char *title;
        const char *subtitle;
        const char *slot;
    };

    const Item items[] = {
        { "Kobo home", "Kobo's own library and store", SLOT(onKoboHome()) },
        { "Shelves", "Collections from BookOrbit", SLOT(onShelves()) },
        { "Settings", "Login, syncing and downloads", SLOT(onSettings()) },
        { "Sync status", "Last sync, what is waiting, Sync now", SLOT(onSyncStatus()) },
    };
    for (const Item &item : items) {
        TextRow *row = new TextRow(sh->look(), QLatin1String(item.title), QString(), parent);
        row->setSubtitle(QLatin1String(item.subtitle));
        connect(row, SIGNAL(tapped()), this, item.slot);
        out << row;
    }
    return out;
}

void MoreScreen::onKoboHome()
{
    shell()->showKoboHome();
}

void MoreScreen::onShelves()
{
    shell()->openShelves();
}

void MoreScreen::onSettings()
{
    shell()->openSettings();
}

void MoreScreen::onSyncStatus()
{
    shell()->openSyncStatus();
}

} // namespace screens
} // namespace smalt
