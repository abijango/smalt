// Library, Series, Series detail, book lists, choices and Shelves.
#include "copy.h"
#include "pages_common.h"
#include "smalt/nickel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>

#include <algorithm>
#include <memory>

namespace smalt {
namespace screens {

using namespace ui::kit;

namespace {

const QStringList kFormatChips = QStringList()
        << QStringLiteral("Books") << QStringLiteral("Comics") << QStringLiteral("PDFs")
        << QStringLiteral("On Kobo");
const QStringList kStatusChips = QStringList()
        << QStringLiteral("Unread") << QStringLiteral("Reading") << QStringLiteral("Finished")
        << QStringLiteral("Sort");

constexpr double kSeriesRowMm = 28.0;
constexpr double kSeriesSummaryMm = 14.0;

SortOrder sortFromMemory(Shell *sh)
{
    const QList<int> &m = sh->chipMemory(QStringLiteral("library.sort"));
    if (m.isEmpty())
        return SortOrder::Title;
    return SortOrder(qBound(0, m.first(), 3));
}

void toggleMemory(QList<int> &list, int index, bool selected)
{
    if (selected && !list.contains(index))
        list.append(index);
    else if (!selected)
        list.removeAll(index);
}

// A series in the list: cover stack, name, "3 of 8 read" and a segmented bar.
class SeriesRow : public QWidget
{
    Q_OBJECT
public:
    SeriesRow(Shell *shell, const SeriesItem &series, QWidget *parent)
        : QWidget(parent), m_look(shell->look()), m_name(series.name)
    {
        setFixedHeight(m_look.px(kSeriesRowMm));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_sub = QStringLiteral("%1 of %2 read").arg(series.read).arg(series.books.size());
        ui::ComponentFactory *kit = m_look.kit();
        ui::CoverSpec front;
        if (!series.books.isEmpty())
            front = shell->catalog()->coverSpec(series.books.first(), false);
        m_stack = kit->coverStack(front, this);
        m_bar = kit->segmentedProgress(series.books.size(), series.read, this);
    }

signals:
    void tapped();

protected:
    void resizeEvent(QResizeEvent *) override
    {
        const int margin = m_look.margin();
        m_stack->move(margin, (height() - m_stack->height()) / 2);
        const int left = margin + m_stack->width() + m_look.px(4.0);
        m_textLeft = left;
        m_bar->setGeometry(left, height() - m_look.px(5.5), width() - left - margin,
                           m_bar->height());
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        m_pressed = true;
        update();
        m_look.theme().requestPartial(this);
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const bool was = m_pressed;
        m_pressed = false;
        update();
        m_look.theme().requestPartial(this);
        if (was && rect().contains(e->pos()))
            emit tapped();
        e->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        const ui::Palette &pal = m_look.palette();
        p.fillRect(rect(), m_pressed ? pal.rule : pal.paper);
        const int margin = m_look.margin();
        const int w = width() - m_textLeft - margin;
        const QFont body = m_look.font(FontRole::Body);
        const QFont small = m_look.font(FontRole::Small);
        p.setFont(body);
        p.setPen(pal.ink);
        p.drawText(QRect(m_textLeft, m_look.px(4.0), w, QFontMetrics(body).height()),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   QFontMetrics(body).elidedText(m_name, Qt::ElideRight, w));
        p.setFont(small);
        p.setPen(pal.secondary);
        p.drawText(QRect(m_textLeft, m_look.px(4.0) + QFontMetrics(body).height() + m_look.px(1.0),
                         w, QFontMetrics(small).height()),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_sub);
        p.fillRect(QRect(margin, height() - metrics::kListRulePx, width() - 2 * margin,
                         metrics::kListRulePx),
                   pal.rule);
    }

private:
    Look m_look;
    QString m_name;
    QString m_sub;
    QWidget *m_stack;
    QWidget *m_bar;
    int m_textLeft = 0;
    bool m_pressed = false;
};

// "3 of 8 read" and the segmented bar, at the top of a series.
class SeriesSummary : public QWidget
{
    Q_OBJECT
public:
    SeriesSummary(Shell *shell, const SeriesItem &s, QWidget *parent)
        : QWidget(parent), m_look(shell->look())
    {
        setFixedHeight(m_look.px(kSeriesSummaryMm));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_text = QStringLiteral("%1 of %2 read").arg(s.read).arg(s.books.size());
        m_bar = m_look.kit()->segmentedProgress(s.books.size(), s.read, this);
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        m_bar->setGeometry(m_look.margin(), height() - m_look.px(4.0),
                           width() - 2 * m_look.margin(), m_bar->height());
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.fillRect(rect(), m_look.palette().paper);
        p.setFont(m_look.font(FontRole::Small));
        p.setPen(m_look.palette().secondary);
        p.drawText(
                QRect(m_look.margin(), 0, width() - 2 * m_look.margin(), height() - m_look.px(5.0)),
                Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_text);
    }

private:
    Look m_look;
    QString m_text;
    QWidget *m_bar;
};

// Opens one series from a SeriesRow.
class SeriesOpener : public QObject
{
    Q_OBJECT
public:
    SeriesOpener(Shell *shell, qint64 id, QObject *parent)
        : QObject(parent), m_shell(shell), m_id(id)
    {
    }

private slots:

    void open() { m_shell->openSeries(m_id); }

private:
    Shell *m_shell;
    qint64 m_id;
};

QWidget *seriesRow(Shell *shell, const SeriesItem &s, QWidget *parent)
{
    LazyBlock *block = new LazyBlock(
            shell->look().px(kSeriesRowMm),
            [shell, s](QWidget *p) {
                SeriesRow *row = new SeriesRow(shell, s, p);
                SeriesOpener *opener = new SeriesOpener(shell, s.id, row);
                QObject::connect(row, SIGNAL(tapped()), opener, SLOT(open()));
                return static_cast<QWidget *>(row);
            },
            parent);
    setSortKey(block, s.name);
    return block;
}

// A tappable row that calls back with its index (choices, shelves).
class IndexedRow : public TextRow
{
    Q_OBJECT
public:
    IndexedRow(const Look &look, const QString &title, const QString &value, int index,
               QWidget *parent)
        : TextRow(look, title, value, parent)
    {
        setProperty("index", index);
    }
};

} // namespace

// ---- Library ----

LibraryScreen::LibraryScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent) { }

LibraryFilter LibraryScreen::filter() const
{
    Shell *sh = shell();
    const QList<int> a = sh->chipMemory(QStringLiteral("library.a"));
    const QList<int> b = sh->chipMemory(QStringLiteral("library.b"));
    LibraryFilter f;
    f.books = a.contains(0);
    f.comics = a.contains(1);
    f.pdfs = a.contains(2);
    f.onKobo = a.contains(3);
    f.unread = b.contains(0);
    f.reading = b.contains(1);
    f.finished = b.contains(2);
    f.sort = sortFromMemory(sh);
    return f;
}

QList<QWidget *> LibraryScreen::buildTop()
{
    Shell *sh = shell();
    ui::ComponentFactory *kit = sh->look().kit();
    QList<QWidget *> top;
    QWidget *a = kit->chipRow(kFormatChips, this);
    setChipSelection(a, sh->chipMemory(QStringLiteral("library.a")));
    connect(a, SIGNAL(chipToggled(int, bool)), this, SLOT(onChipA(int, bool)));
    QWidget *b = kit->chipRow(kStatusChips, this);
    QList<int> selected = sh->chipMemory(QStringLiteral("library.b"));
    selected.removeAll(3);
    if (sortFromMemory(sh) != SortOrder::Title)
        selected.append(3); // The Sort chip is lit while a sort other than Title is on.
    setChipSelection(b, selected);
    connect(b, SIGNAL(chipToggled(int, bool)), this, SLOT(onChipB(int, bool)));
    top << a << b;
    return top;
}

QList<QWidget *> LibraryScreen::buildBlocks()
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    QWidget *parent = column();
    QList<QWidget *> out;
    if (cat->indexEmpty()) {
        out << textBlock(sh, emptyLibraryMessage(sh), TextBlock::Secondary, parent);
        column()->setLetterJump(false);
        return out;
    }
    const LibraryFilter f = filter();
    const QVector<BookItem> list = cat->filtered(f);
    if (f.active()) {
        QWidget *banner = sh->look().kit()->banner(
                copy::showingOf(list.size(), cat->books().size()), copy::clearFilters(), parent);
        connect(banner, SIGNAL(actionTapped()), this, SLOT(onClear()));
        out << banner;
    }
    if (list.isEmpty())
        out << textBlock(sh, copy::noMatches(), TextBlock::Secondary, parent);
    const bool byAuthor = f.sort == SortOrder::Author;
    for (const BookItem &b : list)
        out << lazyBookRow(sh, b, bookSubtitle(sh, b, true), byAuthor ? b.firstAuthor() : b.title,
                           parent);
    column()->setLetterJump(f.sort == SortOrder::Title || byAuthor);
    return out;
}

void LibraryScreen::onChipA(int index, bool selected)
{
    toggleMemory(shell()->chipMemory(QStringLiteral("library.a")), index, selected);
    rebuild();
}

void LibraryScreen::onChipB(int index, bool selected)
{
    Shell *sh = shell();
    if (index == 3) {
        QStringList options;
        for (int i = 0; i < 4; ++i)
            options << sortLabel(SortOrder(i));
        sh->openChoice(QStringLiteral("Sort by"), options, int(sortFromMemory(sh)), [sh](int i) {
            QList<int> &m = sh->chipMemory(QStringLiteral("library.sort"));
            m.clear();
            m.append(i);
        });
        return;
    }
    toggleMemory(sh->chipMemory(QStringLiteral("library.b")), index, selected);
    rebuild();
}

void LibraryScreen::onClear()
{
    Shell *sh = shell();
    sh->chipMemory(QStringLiteral("library.a")).clear();
    sh->chipMemory(QStringLiteral("library.b")).clear();
    rebuild();
}

// ---- Series ----

SeriesScreen::SeriesScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent) { }

QList<QWidget *> SeriesScreen::buildTop()
{
    Shell *sh = shell();
    QWidget *chips = sh->look().kit()->chipRow(
            QStringList() << QStringLiteral("Reading") << QStringLiteral("Unread")
                          << QStringLiteral("Comics") << QStringLiteral("Sort"),
            this);
    QList<int> selected = sh->chipMemory(QStringLiteral("series"));
    selected.removeAll(3);
    const QList<int> sortMem = sh->chipMemory(QStringLiteral("series.sort"));
    if (!sortMem.isEmpty() && sortMem.first() != 0)
        selected.append(3);
    setChipSelection(chips, selected);
    connect(chips, SIGNAL(chipToggled(int, bool)), this, SLOT(onChip(int, bool)));
    return QList<QWidget *>() << chips;
}

QList<QWidget *> SeriesScreen::buildBlocks()
{
    Shell *sh = shell();
    QWidget *parent = column();
    QList<QWidget *> out;
    QVector<SeriesItem> all = sh->catalog()->series();
    if (sh->catalog()->indexEmpty()) {
        out << textBlock(sh, emptyLibraryMessage(sh), TextBlock::Secondary, parent);
        column()->setLetterJump(false);
        return out;
    }
    const QList<int> chips = sh->chipMemory(QStringLiteral("series"));
    const bool reading = chips.contains(0), unread = chips.contains(1), comics = chips.contains(2);
    QVector<SeriesItem> list;
    for (const SeriesItem &s : all) {
        if (reading || unread || comics) {
            const bool isReading = s.reading > 0 || (s.read > 0 && s.read < s.books.size());
            const bool isUnread = s.read == 0 && s.reading == 0;
            const bool ok =
                    (reading && isReading) || (unread && isUnread) || (comics && s.allComics);
            if (!ok)
                continue;
        }
        list.append(s);
    }
    const QList<int> sortMem = sh->chipMemory(QStringLiteral("series.sort"));
    const int sort = sortMem.isEmpty() ? 0 : sortMem.first();
    if (sort == 1) {
        std::stable_sort(list.begin(), list.end(), [](const SeriesItem &a, const SeriesItem &b) {
            if (a.lastRead.isValid() != b.lastRead.isValid())
                return a.lastRead.isValid();
            return a.lastRead > b.lastRead;
        });
    } else if (sort == 2) {
        std::stable_sort(list.begin(), list.end(), [](const SeriesItem &a, const SeriesItem &b) {
            return a.added > b.added;
        });
    }
    if (list.isEmpty())
        out << textBlock(sh, copy::noMatches(), TextBlock::Secondary, parent);
    for (const SeriesItem &s : list)
        out << seriesRow(sh, s, parent);
    column()->setLetterJump(sort == 0);
    return out;
}

void SeriesScreen::onChip(int index, bool selected)
{
    Shell *sh = shell();
    if (index == 3) {
        sh->openChoice(QStringLiteral("Sort by"),
                       QStringList() << QStringLiteral("Name") << QStringLiteral("Recently read")
                                     << QStringLiteral("Recently added"),
                       sh->chipMemory(QStringLiteral("series.sort")).isEmpty()
                               ? 0
                               : sh->chipMemory(QStringLiteral("series.sort")).first(),
                       [sh](int i) {
                           QList<int> &m = sh->chipMemory(QStringLiteral("series.sort"));
                           m.clear();
                           m.append(i);
                       });
        return;
    }
    toggleMemory(sh->chipMemory(QStringLiteral("series")), index, selected);
    rebuild();
}

// ---- Series detail ----

SeriesDetailScreen::SeriesDetailScreen(Shell *shell, qint64 seriesId, QWidget *parent)
    : ColumnScreen(shell, parent), m_seriesId(seriesId)
{
}

bool SeriesDetailScreen::findSeries(SeriesItem *out) const
{
    const QVector<SeriesItem> all = shell()->catalog()->series();
    for (const SeriesItem &s : all) {
        if (s.id == m_seriesId) {
            *out = s;
            return true;
        }
    }
    return false;
}

QString SeriesDetailScreen::title() const
{
    SeriesItem s;
    return findSeries(&s) && !s.name.isEmpty() ? s.name : QStringLiteral("Series");
}

QVector<BookItem> SeriesDetailScreen::nextToDownload(const SeriesItem &s, int count) const
{
    Catalog *cat = shell()->catalog();
    int last = -1;
    for (int i = 0; i < s.books.size(); ++i) {
        const ReadStatus st = cat->statusOf(s.books.at(i));
        if (st == ReadStatus::Read || st == ReadStatus::Reading || st == ReadStatus::OnHold)
            last = i;
    }
    QVector<BookItem> out;
    for (int i = last + 1; i < s.books.size() && out.size() < count; ++i) {
        const BookItem &b = s.books.at(i);
        const ReadStatus st = cat->statusOf(b);
        const bool unread = st == ReadStatus::Unread || st == ReadStatus::WantToRead
                || st == ReadStatus::Unknown;
        if (unread && !cat->onKobo(b.id))
            out.append(b);
    }
    return out;
}

QList<QWidget *> SeriesDetailScreen::buildBlocks()
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    QWidget *parent = column();
    QList<QWidget *> out;
    SeriesItem s;
    if (!findSeries(&s)) {
        out << textBlock(sh, copy::noMatches(), TextBlock::Secondary, parent);
        return out;
    }
    out << new SeriesSummary(sh, s, parent);

    ButtonStack *stack = new ButtonStack(sh->look(), parent);
    int buttons = 0;
    // Continue: the book in progress.
    for (const BookItem &b : s.books) {
        const ReadStatus st = cat->statusOf(b);
        if (st == ReadStatus::Reading || st == ReadStatus::OnHold) {
            QWidget *c = sh->look().kit()->primaryButton(QStringLiteral("Continue"), stack);
            c->setObjectName(QStringLiteral("continue"));
            connect(c, SIGNAL(tapped()), this, SLOT(onContinue()));
            stack->add(c);
            ++buttons;
            break;
        }
    }
    if (!nextToDownload(s, 3).isEmpty() && cat->online()) {
        QWidget *d = sh->look().kit()->accentButton(QStringLiteral("Download next 3"), stack);
        d->setObjectName(QStringLiteral("downloadNext"));
        connect(d, SIGNAL(tapped()), this, SLOT(onDownloadNext()));
        stack->add(d);
        ++buttons;
    }
    if (buttons > 0)
        out << stack;
    else
        delete stack;

    for (const BookItem &b : s.books) {
        QStringList parts;
        if (b.seriesIndex >= 0)
            parts << QStringLiteral("Book %1").arg(QString::number(b.seriesIndex, 'g', 4));
        const ReadStatus st = cat->statusOf(b);
        if (st != ReadStatus::Unknown && st != ReadStatus::Unread)
            parts << copy::statusLabel(st);
        else
            parts << copy::statusLabel(ReadStatus::Unread);
        out << makeBookRow(sh, b, parts.join(QStringLiteral(" · ")), parent);
    }
    return out;
}

void SeriesDetailScreen::onContinue()
{
    SeriesItem s;
    if (!findSeries(&s))
        return;
    Catalog *cat = shell()->catalog();
    for (const BookItem &b : s.books) {
        const ReadStatus st = cat->statusOf(b);
        if (st == ReadStatus::Reading || st == ReadStatus::OnHold) {
            if (cat->onKobo(b.id))
                shell()->openInReader(b.id);
            else
                shell()->openBook(b.id);
            return;
        }
    }
}

void SeriesDetailScreen::onDownloadNext()
{
    SeriesItem s;
    if (findSeries(&s))
        shell()->startDownloads(nextToDownload(s, 3));
}

// ---- Book list (a shelf) ----

BookListScreen::BookListScreen(Shell *shell, const QString &title, const QVector<BookItem> &books,
                               QWidget *parent)
    : ColumnScreen(shell, parent), m_title(title), m_books(books)
{
    for (const BookItem &b : m_books)
        shell->rememberBook(b);
}

QList<QWidget *> BookListScreen::buildBlocks()
{
    Shell *sh = shell();
    QList<QWidget *> out;
    if (m_books.isEmpty())
        out << textBlock(sh, copy::noMatches(), TextBlock::Secondary, column());
    for (const BookItem &b : m_books)
        out << lazyBookRow(sh, b, bookSubtitle(sh, b, true), b.title, column());
    column()->setLetterJump(false);
    return out;
}

// ---- Choice ----

ChoiceScreen::ChoiceScreen(Shell *shell, const QString &title, const QStringList &options,
                           int current, std::function<void(int)> chosen, QWidget *parent)
    : ColumnScreen(shell, parent),
      m_title(title),
      m_options(options),
      m_current(current),
      m_chosen(chosen)
{
}

QList<QWidget *> ChoiceScreen::buildBlocks()
{
    QList<QWidget *> out;
    for (int i = 0; i < m_options.size(); ++i) {
        IndexedRow *row = new IndexedRow(shell()->look(), m_options.at(i),
                                         i == m_current ? QStringLiteral("Selected") : QString(), i,
                                         column());
        connect(row, SIGNAL(tapped()), this, SLOT(onRow()));
        out << row;
    }
    return out;
}

void ChoiceScreen::onRow()
{
    QWidget *row = qobject_cast<QWidget *>(sender());
    if (!row)
        return;
    const int index = row->property("index").toInt();
    std::function<void(int)> chosen = m_chosen;
    Shell *sh = shell();
    sh->pop(); // This screen is deleted later; `chosen` is a copy.
    if (chosen)
        chosen(index);
    if (Screen *s = sh->currentScreen())
        s->rebuild();
}

// ---- Shelves ----

ShelvesScreen::ShelvesScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent) { }

void ShelvesScreen::parse(const QByteArray &json)
{
    m_shelves.clear();
    const QJsonArray items = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        Shelf s;
        s.title = o.value(QStringLiteral("title")).toString();
        s.collectionId = qint64(o.value(QStringLiteral("collectionId")).toDouble());
        if (s.collectionId <= 0) {
            // "collection-12": the number after the last dash.
            const QString id = o.value(QStringLiteral("id")).toString();
            s.collectionId = id.mid(id.lastIndexOf(QLatin1Char('-')) + 1).toLongLong();
            if (s.collectionId <= 0)
                s.collectionId = qint64(o.value(QStringLiteral("id")).toDouble());
        }
        if (!s.title.isEmpty() && s.collectionId > 0)
            m_shelves.append(s);
    }
}

void ShelvesScreen::load()
{
    if (m_loaded)
        return;
    m_loaded = true;
    Shell *sh = shell();
    Ledger *ledger = sh->deps().ledger;
    if (ledger) {
        QByteArray bytes;
        QDateTime when;
        if (ledger->cacheGet(QStringLiteral("shelves:v1"), &bytes, &when).ok())
            parse(bytes);
    }
    BookOrbitClient *client = sh->deps().client;
    if (!client || (sh->deps().nickel && !sh->deps().nickel->networkConnected()))
        return;
    QPointer<ShelvesScreen> self(this);

    // Collections first, then shelves; both end up in one list.
    struct Collected
    {
        QJsonArray items;
        int pending = 2;
        bool failed = false;
    };

    std::shared_ptr<Collected> got(new Collected);
    const char *kinds[2] = { "collections", "shelves" };
    for (const char *kind : kinds) {
        client->section(QLatin1String(kind), 1, [self, got, ledger](const Reply<QJsonObject> &r) {
            if (r.result.ok()) {
                const QJsonArray items = r.value.value(QStringLiteral("items")).toArray();
                for (const QJsonValue &v : items)
                    got->items.append(v);
            } else {
                got->failed = true;
            }
            if (--got->pending > 0 || !self)
                return;
            if (got->failed && got->items.isEmpty()) {
                self->m_failed = true;
            } else {
                const QByteArray json = QJsonDocument(got->items).toJson(QJsonDocument::Compact);
                if (ledger)
                    ledger->cachePut(QStringLiteral("shelves:v1"), QStringLiteral("shelves"), json,
                                     QDateTime::currentDateTimeUtc());
                self->parse(json);
            }
            self->rebuild();
        });
    }
}

QList<QWidget *> ShelvesScreen::buildBlocks()
{
    load();
    Shell *sh = shell();
    QList<QWidget *> out;
    if (m_shelves.isEmpty()) {
        out << textBlock(sh,
                         m_failed || !sh->catalog()->online() ? copy::nothingCachedYet()
                                                              : copy::noResults(),
                         TextBlock::Secondary, column());
        return out;
    }
    for (int i = 0; i < m_shelves.size(); ++i) {
        IndexedRow *row = new IndexedRow(sh->look(), m_shelves.at(i).title, QString(), i, column());
        connect(row, SIGNAL(tapped()), this, SLOT(onShelfTapped()));
        out << row;
    }
    return out;
}

void ShelvesScreen::onShelfTapped()
{
    QWidget *row = qobject_cast<QWidget *>(sender());
    if (!row)
        return;
    const int index = row->property("index").toInt();
    if (index < 0 || index >= m_shelves.size())
        return;
    Shell *sh = shell();
    BookOrbitClient *client = sh->deps().client;
    if (!client || !sh->catalog()->online()) {
        sh->showMessage(copy::unavailableOffline());
        return;
    }
    const Shelf shelf = m_shelves.at(index);
    BookQuery q;
    q.page = 1;
    q.size = 100;
    q.sort = QStringLiteral("title");
    q.collectionId = shelf.collectionId;
    client->books(q, [sh, shelf](const Reply<QJsonObject> &r) {
        QVector<BookItem> books;
        if (r.result.ok()) {
            const QJsonArray items = r.value.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &v : items) {
                const BookItem b = parseBookItem(v.toObject());
                if (b.id > 0 && !b.isAudio())
                    books.append(b);
            }
        } else {
            sh->showMessage(copy::errorText(r.result.error));
            return;
        }
        sh->openBookList(shelf.title, books);
    });
}

} // namespace screens
} // namespace smalt

#include "pages_library.moc"
