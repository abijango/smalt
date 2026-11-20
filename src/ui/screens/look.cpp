#include "look.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QTextLayout>

namespace smalt {
namespace screens {

using namespace ui::kit;

namespace {

constexpr double kTextRowMm = 13.0; // One line: well above the 9 mm touch minimum.
constexpr double kTextRowTwoLineMm = 17.0;
constexpr double kBackBarMm = 11.0;
constexpr double kBlockPadMm = 1.5;

void setup(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
}

} // namespace

// ---- Look ----

Look::Look(ui::ComponentFactory *kit) : m_kit(kit), m_theme(kit->profile(), nullptr)
{
    // The kit's own factory carries the refresh requester inside its theme;
    // reuse it so presses on the widgets here refresh like the kit's.
    if (KitFactory *f = dynamic_cast<KitFactory *>(kit))
        m_theme = f->theme();
}

int Look::textWidth(const QFont &font, const QString &text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return QFontMetrics(font).horizontalAdvance(text);
#else
    return QFontMetrics(font).width(text);
#endif
}

// ---- TextRow ----

TextRow::TextRow(const Look &look, const QString &title, const QString &value, QWidget *parent)
    : QWidget(parent), m_look(look), m_title(title), m_value(value)
{
    setFixedHeight(look.px(kTextRowMm));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TextRow::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void TextRow::setValue(const QString &value)
{
    m_value = value;
    update();
}

void TextRow::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
    setFixedHeight(m_look.px(subtitle.isEmpty() ? kTextRowMm : kTextRowTwoLineMm));
    update();
}

void TextRow::setDimmed(bool dimmed)
{
    m_dimmed = dimmed;
    update();
}

void TextRow::mousePressEvent(QMouseEvent *event)
{
    m_pressed = true;
    update();
    m_look.theme().requestPartial(this);
    event->accept();
}

void TextRow::mouseReleaseEvent(QMouseEvent *event)
{
    const bool was = m_pressed;
    m_pressed = false;
    update();
    m_look.theme().requestPartial(this);
    if (was && rect().contains(event->pos()))
        emit tapped();
    event->accept();
}

void TextRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setup(p);
    const ui::Palette &pal = m_look.palette();
    p.fillRect(rect(), m_pressed ? pal.rule : pal.paper);
    const int margin = m_look.margin();
    const QFont body = m_look.font(FontRole::Body);
    const QFont small = m_look.font(FontRole::Small);
    const QColor titleColour = m_dimmed ? pal.secondary : pal.ink;
    int valueWidth = 0;
    if (!m_value.isEmpty()) {
        valueWidth = qMin(Look::textWidth(small, m_value) + 4, width() / 2);
        p.setFont(small);
        p.setPen(pal.secondary);
        p.drawText(QRect(width() - margin - valueWidth, 0, valueWidth, height()),
                   Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine,
                   QFontMetrics(small).elidedText(m_value, Qt::ElideRight, valueWidth));
    }
    const int textRight = width() - margin - (valueWidth ? valueWidth + m_look.px(3.0) : 0);
    const int textWidth = textRight - margin;
    const int bodyH = QFontMetrics(body).height();
    const int smallH = QFontMetrics(small).height();
    const int gap = m_look.px(1.0);
    const bool two = !m_subtitle.isEmpty();
    const int top = (height() - (two ? bodyH + gap + smallH : bodyH)) / 2;
    p.setFont(body);
    p.setPen(titleColour);
    p.drawText(QRect(margin, top, textWidth, bodyH),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
               QFontMetrics(body).elidedText(m_title, Qt::ElideRight, textWidth));
    if (two) {
        p.setFont(small);
        p.setPen(pal.secondary);
        p.drawText(QRect(margin, top + bodyH + gap, textWidth, smallH),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   QFontMetrics(small).elidedText(m_subtitle, Qt::ElideRight, textWidth));
    }
    p.fillRect(QRect(margin, height() - metrics::kListRulePx, width() - 2 * margin,
                     metrics::kListRulePx),
               pal.rule);
}

// ---- TextBlock ----

TextBlock::TextBlock(const Look &look, const QString &text, Style style, QWidget *parent)
    : QWidget(parent), m_look(look), m_text(text), m_style(style)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QFont TextBlock::fontFor() const
{
    switch (m_style) {
    case Title:
        return m_look.font(FontRole::Title);
    case Small:
    case Secondary:
        return m_look.font(FontRole::Small);
    case Body:
    default:
        return m_look.font(FontRole::Body);
    }
}

QColor TextBlock::colourFor() const
{
    return m_style == Secondary ? m_look.palette().secondary : m_look.palette().ink;
}

void TextBlock::setText(const QString &text)
{
    m_text = text;
    updateGeometry();
    update();
}

int TextBlock::heightForWidth(int w) const
{
    const int inner = qMax(1, w - 2 * m_look.margin());
    const QRect r = QFontMetrics(fontFor()).boundingRect(QRect(0, 0, inner, 100000),
                                                         Qt::AlignLeft | Qt::TextWordWrap, m_text);
    return r.height() + 2 * m_look.px(kBlockPadMm);
}

void TextBlock::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setup(p);
    p.fillRect(rect(), m_look.palette().paper);
    p.setFont(fontFor());
    p.setPen(colourFor());
    const int pad = m_look.px(kBlockPadMm);
    p.drawText(QRect(m_look.margin(), pad, width() - 2 * m_look.margin(), height() - 2 * pad),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);
}

QList<TextBlock *> TextBlock::split(const Look &look, const QString &text, Style style, int width,
                                    int maxLines, QWidget *parent)
{
    QList<TextBlock *> out;
    TextBlock probe(look, QString(), style, nullptr);
    const QFont font = probe.fontFor();
    const int inner = qMax(1, width - 2 * look.margin());
    QStringList pieces;
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    QStringList lines;
    for (const QString &para : paragraphs) {
        if (para.trimmed().isEmpty()) {
            lines << QString();
            continue;
        }
        QTextLayout layout(para, font);
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(inner);
            lines << para.mid(line.textStart(), line.textLength()).trimmed();
        }
        layout.endLayout();
    }
    for (int i = 0; i < lines.size(); i += maxLines) {
        QStringList chunk = lines.mid(i, maxLines);
        while (!chunk.isEmpty() && chunk.last().isEmpty())
            chunk.removeLast();
        if (!chunk.isEmpty())
            pieces << chunk.join(QLatin1Char('\n'));
    }
    for (const QString &piece : pieces)
        out.append(new TextBlock(look, piece, style, parent));
    return out;
}

// ---- BackBar ----

BackBar::BackBar(const Look &look, QWidget *parent) : QWidget(parent), m_look(look)
{
    setFixedHeight(look.px(kBackBarMm));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BackBar::mousePressEvent(QMouseEvent *event)
{
    m_pressed = true;
    update();
    m_look.theme().requestPartial(this);
    event->accept();
}

void BackBar::mouseReleaseEvent(QMouseEvent *event)
{
    const bool was = m_pressed;
    m_pressed = false;
    update();
    m_look.theme().requestPartial(this);
    if (was && rect().contains(event->pos()))
        emit tapped();
    event->accept();
}

void BackBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setup(p);
    const ui::Palette &pal = m_look.palette();
    p.fillRect(rect(), m_pressed ? pal.rule : pal.paper);
    const int margin = m_look.margin();
    // A chevron pointing left, then "Back".
    QPen pen(pal.ink);
    pen.setWidth(qMax(2, m_look.px(0.55)));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const int cy = height() / 2;
    const int s = m_look.px(2.2);
    p.drawLine(margin + s, cy - s, margin, cy);
    p.drawLine(margin, cy, margin + s, cy + s);
    p.setFont(m_look.font(FontRole::Body));
    p.setPen(pal.ink);
    p.drawText(QRect(margin + 2 * s + m_look.px(1.5), 0, width() - 2 * margin, height()),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, QStringLiteral("Back"));
    p.fillRect(QRect(0, height() - metrics::kListRulePx, width(), metrics::kListRulePx), pal.rule);
}

// ---- LazyBlock ----

LazyBlock::LazyBlock(int heightPx, const Factory &factory, QWidget *parent)
    : QWidget(parent), m_factory(factory)
{
    setFixedHeight(heightPx);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LazyBlock::showEvent(QShowEvent *)
{
    if (m_inner || !m_factory)
        return;
    m_inner = m_factory(this);
    if (m_inner) {
        m_inner->setGeometry(rect());
        m_inner->show();
    }
}

void LazyBlock::hideEvent(QHideEvent *)
{
    if (!m_inner)
        return;
    m_inner->hide();
    m_inner->deleteLater();
    m_inner = nullptr;
}

void LazyBlock::resizeEvent(QResizeEvent *)
{
    if (m_inner)
        m_inner->setGeometry(rect());
}

// ---- StackBlock ----

StackBlock::StackBlock(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

static int childHeight(QWidget *c, int w)
{
    return c->hasHeightForWidth() ? c->heightForWidth(w) : c->height();
}

void StackBlock::add(QWidget *child)
{
    m_children.append(child);
    child->show();
    layoutChildren();
}

int StackBlock::heightForWidth(int w) const
{
    int h = 0;
    for (QWidget *c : m_children)
        h += childHeight(c, w);
    return h;
}

void StackBlock::layoutChildren()
{
    int y = 0;
    for (QWidget *c : m_children) {
        const int h = childHeight(c, width());
        c->setGeometry(0, y, width(), h);
        y += h;
    }
}

void StackBlock::resizeEvent(QResizeEvent *)
{
    layoutChildren();
}

// ---- ButtonStack ----

ButtonStack::ButtonStack(const Look &look, QWidget *parent) : QWidget(parent), m_look(look)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(look.px(2.0));
}

void ButtonStack::add(QWidget *button)
{
    m_buttons.append(button);
    int h = m_look.px(2.0);
    for (QWidget *b : m_buttons)
        h += b->height() + m_look.px(2.0);
    setFixedHeight(h);
    layoutButtons();
}

void ButtonStack::layoutButtons()
{
    int y = m_look.px(2.0);
    for (QWidget *b : m_buttons) {
        b->setGeometry(m_look.margin(), y, qMax(0, width() - 2 * m_look.margin()), b->height());
        b->show();
        y += b->height() + m_look.px(2.0);
    }
}

void ButtonStack::resizeEvent(QResizeEvent *)
{
    layoutButtons();
}

// ---- PagedColumn ----

PagedColumn::PagedColumn(const Look &look, QWidget *parent) : QWidget(parent), m_look(look)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

int PagedColumn::usableHeight() const
{
    const int pager = m_look.px(metrics::kPagerHeightMm);
    return height() - (m_pagerShown ? pager : 0);
}

int PagedColumn::blockHeight(QWidget *w) const
{
    if (w->hasHeightForWidth())
        return w->heightForWidth(width());
    if (w->minimumHeight() > 0 && w->minimumHeight() == w->maximumHeight())
        return w->minimumHeight();
    return qMax(w->sizeHint().height(), w->height());
}

QList<QPair<int, int>> PagedColumn::paginate(int availableHeight) const
{
    QList<QPair<int, int>> pages;
    int start = 0;
    int y = 0;
    int i = 0;
    while (i < m_blocks.size()) {
        const int h = blockHeight(m_blocks.at(i));
        if (y + h > availableHeight && i > start) {
            // Close the page before block i, but never strand a block that
            // wants to stay with the next one (a section header).
            int end = i;
            while (end - 1 > start && m_blocks.at(end - 1)->property("keepWithNext").toBool())
                --end;
            pages.append(qMakePair(start, end));
            start = end;
            y = 0;
            i = end;
            continue;
        }
        y += h;
        ++i;
    }
    if (start < m_blocks.size() || pages.isEmpty())
        pages.append(qMakePair(start, int(m_blocks.size())));
    return pages;
}

void PagedColumn::setBlocks(const QList<QWidget *> &blocks, bool keepPage)
{
    for (QWidget *w : m_blocks) {
        w->hide();
        w->deleteLater();
    }
    m_blocks = blocks;
    for (QWidget *w : m_blocks) {
        w->setParent(this);
        w->hide();
    }
    if (!keepPage)
        m_page = 0;
    layoutPage();
}

void PagedColumn::layoutPage()
{
    const int pagerH = m_look.px(metrics::kPagerHeightMm);
    m_pagerShown = false;
    m_pages = paginate(height());
    if (m_pages.size() > 1) {
        m_pagerShown = true;
        m_pages = paginate(height() - pagerH);
    }
    m_page = qBound(0, m_page, m_pages.size() - 1);
    for (QWidget *w : m_blocks)
        w->hide();
    const QPair<int, int> range = m_pages.at(m_page);
    int y = 0;
    for (int i = range.first; i < range.second; ++i) {
        QWidget *w = m_blocks.at(i);
        const int h = blockHeight(w);
        w->setGeometry(0, y, width(), h);
        w->show();
        y += h;
    }
    if (m_pager) {
        m_pager->hide();
        m_pager->deleteLater();
        m_pager = nullptr;
    }
    if (m_pagerShown) {
        m_pager = m_look.kit()->pager(m_page + 1, m_pages.size(), this);
        connect(m_pager, SIGNAL(previousTapped()), this, SLOT(onPrevious()));
        connect(m_pager, SIGNAL(nextTapped()), this, SLOT(onNext()));
        connect(m_pager, SIGNAL(counterTapped()), this, SLOT(onCounter()));
        m_pager->setGeometry(0, height() - pagerH, width(), pagerH);
        m_pager->show();
    }
    emit pageChanged(m_page, m_pages.size());
}

void PagedColumn::resizeEvent(QResizeEvent *)
{
    // Stay on the content the reader was looking at.
    int firstBlock = 0;
    if (m_page < m_pages.size())
        firstBlock = m_pages.at(m_page).first;
    m_pages = paginate(height());
    if (m_pages.size() > 1)
        m_pages = paginate(height() - m_look.px(metrics::kPagerHeightMm));
    for (int p = 0; p < m_pages.size(); ++p) {
        if (firstBlock >= m_pages.at(p).first && firstBlock < m_pages.at(p).second) {
            m_page = p;
            break;
        }
    }
    layoutPage();
}

void PagedColumn::setPage(int page)
{
    const int clamped = qBound(0, page, pageCount() - 1);
    if (clamped == m_page)
        return;
    m_page = clamped;
    layoutPage();
    m_look.theme().requestPartial(this);
}

bool PagedColumn::flip(int delta)
{
    const int before = m_page;
    setPage(m_page + delta);
    return m_page != before;
}

QList<QWidget *> PagedColumn::visibleBlocks() const
{
    QList<QWidget *> out;
    if (m_page >= m_pages.size())
        return out;
    for (int i = m_pages.at(m_page).first; i < m_pages.at(m_page).second; ++i)
        out.append(m_blocks.at(i));
    return out;
}

void PagedColumn::onPrevious()
{
    flip(-1);
}

void PagedColumn::onNext()
{
    flip(1);
}

int PagedColumn::pageForLetter(const QString &letter) const
{
    if (letter.isEmpty() || letter.at(0) == QLatin1Char('#'))
        return 0; // Digits and symbols sort before A.
    const QChar target = letter.at(0).toUpper();
    int found = -1;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const QString key = m_blocks.at(i)->property("sortKey").toString();
        if (key.isEmpty())
            continue;
        const QChar c = key.at(0).toUpper();
        if (c.isLetter() && c >= target) {
            found = i;
            break;
        }
    }
    if (found < 0)
        return pageCount() - 1; // Every key sorts before the letter.
    for (int p = 0; p < m_pages.size(); ++p)
        if (found >= m_pages.at(p).first && found < m_pages.at(p).second)
            return p;
    return 0;
}

void PagedColumn::onCounter()
{
    if (!m_letterJump || m_letters)
        return;
    QWidget *overlay = new QWidget(this);
    overlay->setAutoFillBackground(true);
    QPalette pal = overlay->palette();
    pal.setColor(QPalette::Window, m_look.palette().paper);
    overlay->setPalette(pal);
    overlay->setGeometry(rect());
    QWidget *grid = m_look.kit()->letterGrid(overlay);
    connect(grid, SIGNAL(letterTapped(QString)), this, SLOT(onLetter(QString)));
    QWidget *close = m_look.kit()->outlineButton(QStringLiteral("Close"), overlay);
    connect(close, SIGNAL(tapped()), this, SLOT(closeLetters()));
    const int gap = m_look.px(4.0);
    const int closeW = grid->width();
    const int total = grid->height() + gap + close->height();
    const int top = qMax(0, (height() - total) / 2);
    grid->move((width() - grid->width()) / 2, top);
    close->setGeometry((width() - closeW) / 2, top + grid->height() + gap, closeW, close->height());
    overlay->show();
    overlay->raise();
    m_letters = overlay;
    m_look.theme().requestFull(this);
}

void PagedColumn::onLetter(const QString &letter)
{
    const int target = pageForLetter(letter);
    closeLetters();
    setPage(target);
}

void PagedColumn::closeLetters()
{
    if (!m_letters)
        return;
    m_letters->hide();
    m_letters->deleteLater();
    m_letters = nullptr;
    m_look.theme().requestFull(this);
}

} // namespace screens
} // namespace smalt
