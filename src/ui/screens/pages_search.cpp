// Search: results grouped into series, authors and books.
#include "copy.h"
#include "pages_common.h"
#include "smalt/nickel.h"

#include <QHash>
#include <QPointer>
#include <QSet>

namespace smalt {
namespace screens {

namespace {

struct AuthorHit
{
    QString name;
    BookItem first;
    int count = 0;
};

} // namespace

SearchScreen::SearchScreen(Shell *shell, const QString &query, QWidget *parent)
    : ColumnScreen(shell, parent), m_query(query.trimmed())
{
}

QList<QWidget *> SearchScreen::buildTop()
{
    QWidget *field = shell()->look().kit()->searchField(this);
    if (ui::kit::SearchField *f = qobject_cast<ui::kit::SearchField *>(field))
        f->setText(m_query);
    connect(field, SIGNAL(tapped()), this, SLOT(onFieldTapped()));
    return QList<QWidget *>() << field;
}

void SearchScreen::onFieldTapped()
{
    shell()->openSearch(QString(), true);
}

void SearchScreen::setQuery(const QString &query)
{
    m_query = query.trimmed();
    m_onlineHits.clear();
    m_onlineDone = false;
    ++m_generation;
    rebuild();
    runOnline();
}

void SearchScreen::runOnline()
{
    Shell *sh = shell();
    if (m_query.isEmpty() || !sh->deps().client)
        return;
    if (sh->deps().nickel && !sh->deps().nickel->networkConnected())
        return;
    QPointer<SearchScreen> self(this);
    const int generation = m_generation;
    sh->catalog()->searchOnline(
            m_query, [self, sh, generation](const QVector<BookItem> &hits, ErrorKind error) {
                if (!self || self->m_generation != generation)
                    return;
                if (error == ErrorKind::None) {
                    for (const BookItem &b : hits)
                        if (!sh->catalog()->find(b.id))
                            sh->rememberBook(b);
                    self->m_onlineHits = hits;
                    self->m_onlineDone = true;
                }
                self->rebuild();
            });
}

QList<QWidget *> SearchScreen::buildBlocks()
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    ui::ComponentFactory *kit = sh->look().kit();
    QWidget *parent = column();
    QList<QWidget *> out;
    column()->setLetterJump(false);
    if (m_query.isEmpty()) {
        out << textBlock(sh, QStringLiteral("Search by title, author or series."),
                         TextBlock::Secondary, parent);
        return out;
    }
    const bool online = cat->online();
    if (!online)
        out << textBlock(sh, copy::offlineSearchNote(), TextBlock::Secondary, parent);

    // Books: the server's hits (when it answered) plus what the index knows.
    QVector<BookItem> books;
    QSet<qint64> seen;
    if (online && m_onlineDone) {
        for (const BookItem &b : m_onlineHits) {
            books.append(b);
            seen.insert(b.id);
        }
    }
    const QVector<BookItem> local = cat->searchLocal(m_query, !online);
    for (const BookItem &b : local) {
        if (!seen.contains(b.id)) {
            books.append(b);
            seen.insert(b.id);
        }
    }

    // Series whose name matches (offline: only series with a book on the Kobo).
    QVector<SeriesItem> series;
    const QVector<SeriesItem> allSeries = cat->series();
    for (const SeriesItem &s : allSeries) {
        if (!s.name.contains(m_query, Qt::CaseInsensitive))
            continue;
        if (!online) {
            bool any = false;
            for (const BookItem &b : s.books)
                any = any || cat->onKobo(b.id);
            if (!any)
                continue;
        }
        series.append(s);
    }

    // Authors whose name matches, from the books found.
    QVector<AuthorHit> authors;
    QHash<QString, int> authorAt;
    const QVector<BookItem> authorSource = online ? cat->books() : books;
    for (const BookItem &b : authorSource) {
        for (const QString &a : b.authors) {
            if (!a.contains(m_query, Qt::CaseInsensitive))
                continue;
            if (!online && !cat->onKobo(b.id))
                continue;
            int i = authorAt.value(a.toLower(), -1);
            if (i < 0) {
                i = authors.size();
                authorAt.insert(a.toLower(), i);
                AuthorHit h;
                h.name = a;
                h.first = b;
                authors.append(h);
            }
            ++authors[i].count;
        }
    }

    if (series.isEmpty() && authors.isEmpty() && books.isEmpty()) {
        out << textBlock(sh, copy::noResults(), TextBlock::Secondary, parent);
        return out;
    }

    const int rowHeight = sh->look().px(ui::kit::metrics::kListRowHeightMm);
    if (!series.isEmpty()) {
        QWidget *h = kit->sectionHeader(QStringLiteral("Series"), series.size(), parent);
        keepWithNext(h);
        out << h;
        for (const SeriesItem &s : series) {
            LazyBlock *block = new LazyBlock(
                    rowHeight,
                    [this, sh, s](QWidget *p) {
                        ui::ListRowSpec spec;
                        if (!s.books.isEmpty())
                            spec.cover = sh->catalog()->coverSpec(s.books.first(), false);
                        spec.title = s.name;
                        spec.subtitle = copy::plural(s.books.size(), "book", "books");
                        QWidget *row = sh->look().kit()->listRow(spec, p);
                        row->setProperty("seriesId", s.id);
                        connect(row, SIGNAL(tapped()), this, SLOT(onSeriesTapped()));
                        return row;
                    },
                    parent);
            out << block;
        }
    }
    if (!authors.isEmpty()) {
        QWidget *h = kit->sectionHeader(QStringLiteral("Authors"), authors.size(), parent);
        keepWithNext(h);
        out << h;
        for (const AuthorHit &a : authors) {
            LazyBlock *block = new LazyBlock(
                    rowHeight,
                    [this, sh, a](QWidget *p) {
                        ui::ListRowSpec spec;
                        spec.cover = sh->catalog()->coverSpec(a.first, false);
                        spec.title = a.name;
                        spec.subtitle = copy::plural(a.count, "book", "books");
                        QWidget *row = sh->look().kit()->listRow(spec, p);
                        row->setProperty("author", a.name);
                        connect(row, SIGNAL(tapped()), this, SLOT(onAuthorTapped()));
                        return row;
                    },
                    parent);
            out << block;
        }
    }
    if (!books.isEmpty()) {
        QWidget *h = kit->sectionHeader(QStringLiteral("Books"), books.size(), parent);
        keepWithNext(h);
        out << h;
        for (const BookItem &b : books)
            out << lazyBookRow(sh, b, bookSubtitle(sh, b, true), b.title, parent);
    }
    return out;
}

void SearchScreen::onSeriesTapped()
{
    QObject *row = sender();
    if (!row)
        return;
    shell()->openSeries(row->property("seriesId").toLongLong());
}

void SearchScreen::onAuthorTapped()
{
    QObject *row = sender();
    if (!row)
        return;
    const QString name = row->property("author").toString();
    // Search for the author by name, on this same screen.
    QMetaObject::invokeMethod(this, "setQuery", Qt::QueuedConnection, Q_ARG(QString, name));
}

} // namespace screens
} // namespace smalt
