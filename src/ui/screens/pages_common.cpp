#include "pages_common.h"

#include "copy.h"

namespace smalt {
namespace screens {

namespace {

// Wires one list row to the shell.
class BookRowHandler : public QObject
{
    Q_OBJECT
public:
    BookRowHandler(Shell *shell, const BookItem &book, QObject *parent)
        : QObject(parent), m_shell(shell), m_book(book)
    {
    }

private slots:

    void onTapped() { m_shell->openBook(m_book.id); }

    void onBadge() { m_shell->startDownload(m_book); }

private:
    Shell *m_shell;
    BookItem m_book;
};

QString number(double n)
{
    return QString::number(n, 'g', 4);
}

} // namespace

QString bookSubtitle(Shell *, const BookItem &book, bool withSeries)
{
    QStringList parts;
    if (!book.authors.isEmpty())
        parts << book.authorLine();
    if (withSeries && book.seriesIndex >= 0)
        parts << QStringLiteral("Book %1").arg(number(book.seriesIndex));
    return parts.join(QStringLiteral(" · "));
}

QWidget *makeBookRow(Shell *shell, const BookItem &book, const QString &subtitle, QWidget *parent)
{
    Catalog *cat = shell->catalog();
    ui::ListRowSpec spec;
    spec.cover = cat->coverSpec(book, true);
    spec.title = book.title;
    spec.subtitle = subtitle;
    const DownloadInfo d = shell->downloads()->info(book.id);
    if (d.state == DownloadState::Queued || d.state == DownloadState::Preparing
        || d.state == DownloadState::Downloading) {
        spec.badge = ui::BadgeState::Percent;
        spec.badgeText = shell->downloads()->badgeText(book.id);
    } else if (cat->onKobo(book.id)) {
        spec.badge = ui::BadgeState::OnDevice;
    } else if (!cat->online()) {
        spec.unavailable = true; // Offline and not downloaded.
    } else {
        spec.badge = ui::BadgeState::Download;
    }
    QWidget *row = shell->look().kit()->listRow(spec, parent);
    BookRowHandler *handler = new BookRowHandler(shell, book, row);
    QObject::connect(row, SIGNAL(tapped()), handler, SLOT(onTapped()));
    QObject::connect(row, SIGNAL(badgeTapped()), handler, SLOT(onBadge()));
    return row;
}

void setChipSelection(QWidget *chipRow, const QList<int> &selected)
{
    if (ui::kit::ChipRow *c = qobject_cast<ui::kit::ChipRow *>(chipRow))
        c->setSelected(selected);
}

void keepWithNext(QWidget *block)
{
    block->setProperty("keepWithNext", true);
}

void setSortKey(QWidget *block, const QString &key)
{
    block->setProperty("sortKey", key);
}

QWidget *lazyBookRow(Shell *shell, const BookItem &book, const QString &subtitle,
                     const QString &sortKey, QWidget *parent)
{
    LazyBlock *block = new LazyBlock(
            shell->look().px(ui::kit::metrics::kListRowHeightMm),
            [shell, book, subtitle](QWidget *p) { return makeBookRow(shell, book, subtitle, p); },
            parent);
    setSortKey(block, sortKey);
    return block;
}

QString emptyLibraryMessage(Shell *shell)
{
    Catalog *cat = shell->catalog();
    if (cat->lastError() == ErrorKind::AuthRejected)
        return copy::keyRejected();
    if (cat->refreshing())
        return copy::loadingLibrary();
    if (!cat->online())
        return copy::nothingCachedYet();
    return copy::emptyLibrary();
}

QWidget *textBlock(Shell *shell, const QString &text, int style, QWidget *parent)
{
    return new TextBlock(shell->look(), text, TextBlock::Style(style), parent);
}

} // namespace screens
} // namespace smalt

#include "pages_common.moc"
