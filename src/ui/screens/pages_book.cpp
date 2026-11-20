// Book detail.
#include "copy.h"
#include "pages_common.h"
#include "smalt/ledger.h"

#include <QJsonArray>
#include <QPainter>
#include <QPointer>

namespace smalt {
namespace screens {

using namespace ui::kit;

namespace {

constexpr double kDetailCoverMm = 30.0;
constexpr double kDetailPadMm = 3.0;
constexpr double kDetailGapMm = 5.0;

const ReadStatus kStatusChoices[6] = { ReadStatus::Unread,  ReadStatus::WantToRead,
                                       ReadStatus::Reading, ReadStatus::OnHold,
                                       ReadStatus::Read,    ReadStatus::Abandoned };

int choiceIndexOf(ReadStatus s)
{
    for (int i = 0; i < 6; ++i)
        if (kStatusChoices[i] == s)
            return i;
    return 0; // Unknown shows as Unread.
}

QString sizeText(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
}

// Cover on the left; title, author, series position and file facts on the right.
class DetailHead : public QWidget
{
    Q_OBJECT
public:
    DetailHead(Shell *shell, const ui::CoverSpec &cover, const QString &title,
               const QString &author, const QString &series, const QString &meta, QWidget *parent)
        : QWidget(parent),
          m_look(shell->look()),
          m_title(title),
          m_author(author),
          m_series(series),
          m_meta(meta)
    {
        m_cover = m_look.kit()->cover(cover, kDetailCoverMm, this);
        setFixedHeight(m_cover->height() + 2 * m_look.px(kDetailPadMm));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        m_cover->move(m_look.margin(), m_look.px(kDetailPadMm));
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        const ui::Palette &pal = m_look.palette();
        p.fillRect(rect(), pal.paper);
        const int left = m_look.margin() + m_cover->width() + m_look.px(kDetailGapMm);
        const int w = qMax(1, width() - left - m_look.margin());
        int y = m_look.px(kDetailPadMm);
        const QFont titleFont = m_look.font(FontRole::Title);
        const QFont body = m_look.font(FontRole::Body);
        const QFont small = m_look.font(FontRole::Small);
        // The title wraps to at most three lines.
        const int titleMax = QFontMetrics(titleFont).lineSpacing() * 3;
        QRect titleRect = QFontMetrics(titleFont).boundingRect(
                QRect(0, 0, w, titleMax), Qt::AlignLeft | Qt::TextWordWrap, m_title);
        titleRect.setHeight(qMin(titleRect.height(), titleMax));
        p.setFont(titleFont);
        p.setPen(pal.ink);
        p.drawText(QRect(left, y, w, titleRect.height()),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_title);
        y += titleRect.height() + m_look.px(2.0);
        if (!m_author.isEmpty()) {
            p.setFont(body);
            p.setPen(pal.secondary);
            const int h = QFontMetrics(body).height();
            p.drawText(QRect(left, y, w, h), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       QFontMetrics(body).elidedText(m_author, Qt::ElideRight, w));
            y += h + m_look.px(1.5);
        }
        p.setFont(small);
        p.setPen(pal.secondary);
        const int sh = QFontMetrics(small).height();
        if (!m_series.isEmpty()) {
            p.drawText(QRect(left, y, w, sh), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       QFontMetrics(small).elidedText(m_series, Qt::ElideRight, w));
            y += sh + m_look.px(1.0);
        }
        if (!m_meta.isEmpty())
            p.drawText(QRect(left, y, w, sh), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       QFontMetrics(small).elidedText(m_meta, Qt::ElideRight, w));
    }

private:
    Look m_look;
    QString m_title;
    QString m_author;
    QString m_series;
    QString m_meta;
    QWidget *m_cover;
};

} // namespace

BookDetailScreen::BookDetailScreen(Shell *shell, const BookItem &book, QWidget *parent)
    : ColumnScreen(shell, parent), m_book(book)
{
}

void BookDetailScreen::rebuild()
{
    ColumnScreen::rebuild();
    loadDetail();
}

void BookDetailScreen::loadDetail()
{
    if (m_detailRequested)
        return;
    m_detailRequested = true;
    QPointer<BookDetailScreen> self(this);
    shell()->catalog()->detail(m_book.id, [self](const QJsonObject &d, bool, ErrorKind) {
        if (!self || d.isEmpty())
            return;
        self->m_detail = d;
        // Fill in what a search hit left out.
        if (self->m_book.title.isEmpty())
            self->m_book = parseBookItem(d);
        self->ColumnScreen::rebuild();
    });
}

bool BookDetailScreen::updateAvailable() const
{
    BookMapEntry e;
    if (!shell()->catalog()->bookMap(m_book.id, &e) || e.bookFileId <= 0)
        return false;
    const QJsonArray files = m_detail.value(QStringLiteral("files")).toArray();
    qint64 current = 0;
    for (const QJsonValue &v : files) {
        const QJsonObject f = v.toObject();
        if (current == 0 || f.value(QStringLiteral("role")).toString() == QLatin1String("content"))
            current = qint64(f.value(QStringLiteral("id")).toDouble());
        if (f.value(QStringLiteral("role")).toString() == QLatin1String("content"))
            break;
    }
    return current > 0 && current != e.bookFileId;
}

QList<QWidget *> BookDetailScreen::buildBlocks()
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    ui::ComponentFactory *kit = sh->look().kit();
    QWidget *parent = column();
    QList<QWidget *> out;

    // Prefer the index's fresher local state, then fill from the detail.
    if (const BookItem *indexed = cat->find(m_book.id))
        m_book = *indexed;

    QString series;
    if (m_book.seriesId > 0 && !m_book.seriesName.isEmpty()) {
        int total = 0;
        const QVector<SeriesItem> all = cat->series();
        for (const SeriesItem &s : all)
            if (s.id == m_book.seriesId)
                total = s.books.size();
        const QString n =
                m_book.seriesIndex >= 0 ? QString::number(m_book.seriesIndex, 'g', 4) : QString();
        if (!n.isEmpty() && total > 0)
            series = QStringLiteral("Book %1 of %2 in %3").arg(n).arg(total).arg(m_book.seriesName);
        else if (!n.isEmpty())
            series = QStringLiteral("Book %1 in %2").arg(n, m_book.seriesName);
        else
            series = m_book.seriesName;
    }
    QStringList meta;
    const QJsonArray files = m_detail.value(QStringLiteral("files")).toArray();
    qint64 size = 0;
    for (const QJsonValue &v : files) {
        const QJsonObject f = v.toObject();
        if (f.value(QStringLiteral("role")).toString() == QLatin1String("content") || size == 0)
            size = qint64(f.value(QStringLiteral("sizeBytes")).toDouble());
    }
    if (!m_book.formats.isEmpty())
        meta << m_book.formats.first().toUpper();
    const int year = m_detail.value(QStringLiteral("publishedYear")).toInt();
    if (year > 0)
        meta << QString::number(year);
    const int pages = m_detail.value(QStringLiteral("pageCount")).toInt();
    if (pages > 0)
        meta << QStringLiteral("%1 pages").arg(pages);
    if (!sizeText(size).isEmpty())
        meta << sizeText(size);
    out << new DetailHead(sh, cat->coverSpec(m_book, true), m_book.title, m_book.authorLine(),
                          series, meta.join(QStringLiteral(" · ")), parent);

    // The one main button, which changes with state.
    ButtonStack *stack = new ButtonStack(sh->look(), parent);
    const DownloadInfo d = sh->downloads()->info(m_book.id);
    QWidget *button = nullptr;
    m_action = ActionNone;
    if (d.state == DownloadState::Downloading) {
        button = kit->outlineButton(QStringLiteral("Downloading %1%").arg(d.percent), stack);
    } else if (d.state == DownloadState::Queued || d.state == DownloadState::Preparing) {
        button = kit->outlineButton(QStringLiteral("Queued"), stack);
    } else if (cat->onKobo(m_book.id)) {
        if (updateAvailable()) {
            button = kit->accentButton(QStringLiteral("Update available"), stack);
            m_action = ActionUpdate;
        } else {
            button = kit->primaryButton(QStringLiteral("Open"), stack);
            m_action = ActionOpen;
        }
    } else if (cat->online()) {
        button = kit->accentButton(QStringLiteral("Download"), stack);
        m_action = ActionDownload;
    } else {
        button = kit->outlineButton(copy::unavailableOffline(), stack);
    }
    button->setObjectName(QStringLiteral("main"));
    connect(button, SIGNAL(tapped()), this, SLOT(onMainButton()));
    stack->add(button);
    out << stack;
    if (d.state == DownloadState::Failed && !d.error.isEmpty())
        out << textBlock(sh, d.error, TextBlock::Secondary, parent);

    // Status and rating.
    const ReadStatus status = cat->statusOf(m_book);
    TextRow *statusRow =
            new TextRow(sh->look(), QStringLiteral("Status"), copy::statusLabel(status), parent);
    connect(statusRow, SIGNAL(tapped()), this, SLOT(onStatus()));
    out << statusRow;
    int rating = qBound(0, int(m_detail.value(QStringLiteral("rating")).toDouble() + 0.5), 5);
    BookMapEntry map;
    if (sh->deps().ledger && cat->bookMap(m_book.id, &map)) {
        BookStateRecord rec;
        if (sh->deps().ledger->bookState(map.contentId, &rec).ok() && rec.rating > 0)
            rating = rec.rating;
    }
    TextRow *ratingRow = new TextRow(sh->look(), QStringLiteral("Rating"),
                                     rating > 0 ? QStringLiteral("%1 of 5").arg(rating)
                                                : QStringLiteral("Not rated"),
                                     parent);
    connect(ratingRow, SIGNAL(tapped()), this, SLOT(onRating()));
    out << ratingRow;

    // "Your reading" (task 23) for books read on this Kobo.
    if (sh->deps().yourReading && cat->bookMap(m_book.id, &map)) {
        if (QWidget *w = sh->deps().yourReading(map.contentId, parent))
            out << w;
    }

    const QString description = m_detail.value(QStringLiteral("description")).toString();
    if (!description.trimmed().isEmpty()) {
        const int w = column()->width() > 0 ? column()->width() : sh->width();
        const QList<TextBlock *> pieces =
                TextBlock::split(sh->look(), description, TextBlock::Body, w, 8, parent);
        for (TextBlock *t : pieces)
            out << t;
    }
    return out;
}

void BookDetailScreen::onMainButton()
{
    Shell *sh = shell();
    switch (m_action) {
    case ActionDownload:
        sh->startDownload(m_book);
        break;
    case ActionOpen:
        sh->openInReader(m_book.id);
        break;
    case ActionUpdate:
        sh->downloads()->enqueueUpdate(m_book);
        break;
    case ActionNone:
        break;
    }
}

void BookDetailScreen::onStatus()
{
    Shell *sh = shell();
    QStringList options;
    for (int i = 0; i < 6; ++i)
        options << copy::statusLabel(kStatusChoices[i]);
    QPointer<BookDetailScreen> self(this);
    sh->openChoice(QStringLiteral("Status"), options,
                   choiceIndexOf(sh->catalog()->statusOf(m_book)), [self](int i) {
                       if (self && i >= 0 && i < 6)
                           self->setStatus(kStatusChoices[i]);
                   });
}

void BookDetailScreen::onRating()
{
    Shell *sh = shell();
    QStringList options;
    options << QStringLiteral("Not rated");
    for (int i = 1; i <= 5; ++i)
        options << QStringLiteral("%1 of 5").arg(i);
    QPointer<BookDetailScreen> self(this);
    sh->openChoice(QStringLiteral("Rating"), options, 0, [self](int i) {
        if (self)
            self->setRating(i);
    });
}

void BookDetailScreen::setStatus(ReadStatus status)
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    BookMapEntry map;
    if (sh->deps().ledger && cat->bookMap(m_book.id, &map)) {
        // On this Kobo: the ledger keeps it and the sync pushes it.
        const Status s = sh->deps().ledger->setLocalStatus(map.contentId, status,
                                                           QDateTime::currentDateTimeUtc());
        if (!s.ok())
            sh->showMessage(copy::openFailed());
        cat->invalidateLocal();
        return;
    }
    if (!sh->deps().client || !cat->online()) {
        sh->showMessage(copy::offlineChange());
        return;
    }
    QPointer<BookDetailScreen> self(this);
    const qint64 id = m_book.id;
    sh->deps().client->setReadStatus(id, readStatusWire(status),
                                     [self, sh, id, status](const Reply<bool> &r) {
                                         if (!r.result.ok()) {
                                             sh->showMessage(copy::errorText(r.result.error));
                                             return;
                                         }
                                         sh->catalog()->setServerStatus(id, status);
                                         if (self)
                                             self->ColumnScreen::rebuild();
                                     });
}

void BookDetailScreen::setRating(int rating)
{
    Shell *sh = shell();
    Catalog *cat = sh->catalog();
    BookMapEntry map;
    if (sh->deps().ledger && cat->bookMap(m_book.id, &map)) {
        const Status s = sh->deps().ledger->setLocalRating(map.contentId, rating,
                                                           QDateTime::currentDateTimeUtc());
        if (!s.ok())
            sh->showMessage(copy::openFailed());
        return;
    }
    if (!sh->deps().client || !cat->online()) {
        sh->showMessage(copy::offlineChange());
        return;
    }
    QPointer<BookDetailScreen> self(this);
    sh->deps().client->setRating(m_book.id, rating, [self, sh, rating](const Reply<bool> &r) {
        if (!r.result.ok()) {
            sh->showMessage(copy::errorText(r.result.error));
            return;
        }
        if (self) {
            self->m_detail.insert(QStringLiteral("rating"), rating);
            self->ColumnScreen::rebuild();
        }
    });
}

} // namespace screens
} // namespace smalt

#include "pages_book.moc"
