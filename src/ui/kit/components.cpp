#include "components.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QVBoxLayout>

namespace smalt {
namespace ui {
namespace kit {

using namespace metrics;

namespace {

// ---- Painting helpers ----

void setupPainter(QPainter &p)
{
    // Crisp edges on E Ink; text antialiasing stays greyscale (see Theme::font).
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
}

void drawText(QPainter &p, const QRect &r, int flags, const QString &text, const QFont &font,
              const QColor &colour)
{
    p.save();
    p.setFont(font);
    p.setPen(colour);
    const QString shown = QFontMetrics(font).elidedText(text, Qt::ElideRight, r.width());
    p.drawText(r, flags | Qt::TextSingleLine, shown);
    p.restore();
}

int textWidth(const QFont &font, const QString &text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return QFontMetrics(font).horizontalAdvance(text);
#else
    return QFontMetrics(font).width(text); // The Kobo's Qt 5.2 has no horizontalAdvance().
#endif
}

QPen iconPen(const Theme &t, const QColor &c)
{
    QPen pen(c);
    pen.setWidth(qMax(2, t.px(0.55)));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

void drawSearchIcon(QPainter &p, const Theme &t, const QRect &box, const QColor &c)
{
    const int s = box.height();
    const QPoint centre = box.center();
    const int radius = s * 3 / 10;
    p.save();
    p.setPen(iconPen(t, c));
    p.setBrush(Qt::NoBrush);
    const QPoint lens(centre.x() - radius / 3, centre.y() - radius / 3);
    p.drawEllipse(lens, radius, radius);
    const double d = radius * 0.72;
    p.drawLine(QPointF(lens.x() + d, lens.y() + d),
               QPointF(lens.x() + d + radius * 0.9, lens.y() + d + radius * 0.9));
    p.restore();
}

void drawArrowDown(QPainter &p, const Theme &t, const QRect &box, const QColor &c)
{
    const QPoint m = box.center();
    const int h = box.height() * 3 / 10;
    p.save();
    p.setPen(iconPen(t, c));
    p.drawLine(m.x(), m.y() - h, m.x(), m.y() + h);
    p.drawLine(m.x() - h * 7 / 10, m.y() + h * 3 / 10, m.x(), m.y() + h);
    p.drawLine(m.x() + h * 7 / 10, m.y() + h * 3 / 10, m.x(), m.y() + h);
    p.drawLine(m.x() - h, m.y() + h + t.px(0.9), m.x() + h, m.y() + h + t.px(0.9));
    p.restore();
}

void drawCheck(QPainter &p, const Theme &t, const QRect &box, const QColor &c)
{
    const QPoint m = box.center();
    const int h = box.height() / 5;
    p.save();
    p.setPen(iconPen(t, c));
    p.drawLine(m.x() - h, m.y(), m.x() - h / 3, m.y() + h * 2 / 3);
    p.drawLine(m.x() - h / 3, m.y() + h * 2 / 3, m.x() + h, m.y() - h * 2 / 3);
    p.restore();
}

void drawChevron(QPainter &p, const Theme &t, const QRect &box, bool right, const QColor &c)
{
    const QPoint m = box.center();
    const int h = box.height() / 5;
    const int dir = right ? 1 : -1;
    p.save();
    p.setPen(iconPen(t, c));
    p.drawLine(m.x() - dir * h / 2, m.y() - h, m.x() + dir * h / 2, m.y());
    p.drawLine(m.x() + dir * h / 2, m.y(), m.x() - dir * h / 2, m.y() + h);
    p.restore();
}

int badgeWidth(const Theme &t, BadgeState state, const QString &text)
{
    const int side = t.px(kBadgeSizeMm);
    if (state == BadgeState::Percent)
        return qMax(side, textWidth(t.font(FontRole::Small), text) + t.px(3.0));
    return side;
}

void paintBadge(QPainter &p, const Theme &t, const QRect &r, BadgeState state, const QString &text,
                bool pressed)
{
    const Palette &pal = t.palette();
    p.save();
    p.setPen(Qt::NoPen);
    switch (state) {
    case BadgeState::Download:
        p.setBrush(pressed ? pal.rule : pal.accentTint);
        p.drawRoundedRect(r, t.px(1.0), t.px(1.0));
        drawArrowDown(p, t, r, pal.accentDark);
        break;
    case BadgeState::OnDevice:
        drawCheck(p, t, r, pal.secondary);
        break;
    case BadgeState::Percent:
        // Live values stay in black text; the tint behind rarely changes.
        p.setBrush(pal.accentTint);
        p.drawRoundedRect(r, t.px(1.0), t.px(1.0));
        drawText(p, r, Qt::AlignCenter, text, t.font(FontRole::Small), t.textColour());
        break;
    case BadgeState::None:
        break;
    }
    p.restore();
}

void paintCover(QPainter &p, const Theme &t, const QRect &r, const CoverSpec &spec)
{
    const Palette &pal = t.palette();
    p.save();
    p.setClipRect(r);
    if (spec.pixmap.isNull()) {
        p.fillRect(r, pal.rule);
        p.setPen(QPen(pal.secondary, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    } else {
        const QPixmap cover = t.coverForDevice(spec.pixmap);
        const QPixmap scaled =
                cover.scaled(r.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint offset((scaled.width() - r.width()) / 2, (scaled.height() - r.height()) / 2);
        p.drawPixmap(r.topLeft(), scaled, QRect(offset, r.size()));
        // Hairline so a pale cover still reads on white paper.
        p.setPen(QPen(pal.rule, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
    if (!spec.format.isEmpty()) {
        QFont f = t.font(FontRole::Small);
        f.setPixelSize(qMax(8, f.pixelSize() * 4 / 5));
        f.setBold(true);
        const int padX = t.px(1.2);
        const int w = textWidth(f, spec.format) + 2 * padX;
        const int h = QFontMetrics(f).height() + t.px(0.8);
        const QRect label(r.left(), r.top(), qMin(w, r.width()), h);
        p.fillRect(label, pal.ink);
        drawText(p, label, Qt::AlignCenter, spec.format, f, t.onFillTextColour());
    }
    if (spec.progress >= 0) {
        const int h = qMax(3, t.px(kProgressBarMm));
        const QRect track(r.left(), r.bottom() + 1 - h, r.width(), h);
        p.fillRect(track, pal.rule);
        p.fillRect(QRect(track.left(), track.top(),
                         int(track.width() * qMin(1.0, spec.progress) + 0.5), h),
                   pal.accent);
    }
    p.restore();
}

} // namespace

// ---- KitWidget ----

KitWidget::KitWidget(const Theme &theme, QWidget *parent) : QWidget(parent), theme_(theme)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int KitWidget::regionAt(const QPoint &) const
{
    return -1;
}

void KitWidget::activated(int) { }

void KitWidget::fixHeightMm(double mm)
{
    setFixedHeight(theme_.px(mm));
}

void KitWidget::paintPressed(QPainter &p, const QRect &r) const
{
    p.fillRect(r, theme_.palette().rule);
}

void KitWidget::mousePressEvent(QMouseEvent *event)
{
    const int region = isEnabled() ? regionAt(event->pos()) : -1;
    if (region < 0) {
        event->ignore();
        return;
    }
    pressed_ = region;
    update();
    theme_.requestPartial(this);
    event->accept();
}

void KitWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const int was = pressed_;
    pressed_ = -1;
    if (was < 0) {
        event->ignore();
        return;
    }
    update();
    theme_.requestPartial(this);
    if (regionAt(event->pos()) == was)
        activated(was);
    event->accept();
}

// ---- HeaderBar ----

HeaderBar::HeaderBar(const Theme &theme, const QString &title, QWidget *parent)
    : KitWidget(theme, parent), title_(title)
{
    fixHeightMm(kHeaderHeightMm);
}

void HeaderBar::setTitle(const QString &title)
{
    title_ = title;
    update();
}

QRect HeaderBar::searchRect() const
{
    const int w = theme_.px(kHeaderHeightMm + 3.0);
    return QRect(width() - w, 0, w, height() - kHeaderRulePx);
}

int HeaderBar::regionAt(const QPoint &pos) const
{
    return searchRect().contains(pos) ? 0 : -1;
}

void HeaderBar::activated(int)
{
    emit searchTapped();
}

void HeaderBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    const QRect search = searchRect();
    if (pressedRegion() == 0)
        paintPressed(p, search);
    drawSearchIcon(p, theme_, search.adjusted(0, 0, 0, 0), pal.ink);
    const int margin = theme_.px(kPageMarginMm);
    drawText(p, QRect(margin, 0, search.left() - margin, height() - kHeaderRulePx),
             Qt::AlignLeft | Qt::AlignVCenter, title_, theme_.font(FontRole::Title),
             theme_.textColour());
    p.fillRect(QRect(0, height() - kHeaderRulePx, width(), kHeaderRulePx), pal.ink);
}

// ---- StatusLine ----

StatusLine::StatusLine(const Theme &theme, const QString &text, QWidget *parent)
    : KitWidget(theme, parent), text_(text)
{
    fixHeightMm(kStatusLineHeightMm);
}

void StatusLine::setText(const QString &text)
{
    text_ = text;
    update();
}

int StatusLine::regionAt(const QPoint &) const
{
    return 0;
}

void StatusLine::activated(int)
{
    emit tapped();
}

void StatusLine::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.fillRect(rect(), theme_.palette().paper);
    if (pressedRegion() == 0)
        paintPressed(p, rect());
    const int margin = theme_.px(kPageMarginMm);
    drawText(p, rect().adjusted(margin, 0, -margin, 0), Qt::AlignLeft | Qt::AlignVCenter, text_,
             theme_.font(FontRole::Small), theme_.secondaryTextColour());
}

// ---- SectionHeader ----

SectionHeader::SectionHeader(const Theme &theme, const QString &title, int count, QWidget *parent)
    : KitWidget(theme, parent), title_(title), count_(count)
{
    fixHeightMm(kSectionHeaderHeightMm);
}

void SectionHeader::setCount(int count)
{
    count_ = count;
    update();
}

void SectionHeader::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.fillRect(rect(), theme_.palette().paper);
    const int margin = theme_.px(kPageMarginMm);
    const QString count = QString::number(count_);
    const QFont small = theme_.font(FontRole::Small);
    const int countW = textWidth(small, count);
    QFont titleFont = theme_.font(FontRole::Body);
    titleFont.setWeight(kWeightTitle);
    drawText(p, QRect(width() - margin - countW, 0, countW, height()),
             Qt::AlignRight | Qt::AlignVCenter, count, small, theme_.secondaryTextColour());
    drawText(p, QRect(margin, 0, width() - 2 * margin - countW - theme_.px(3), height()),
             Qt::AlignLeft | Qt::AlignVCenter, title_, titleFont, theme_.textColour());
}

// ---- Button ----

Button::Button(const Theme &theme, Kind kind, const QString &text, QWidget *parent)
    : KitWidget(theme, parent), kind_(kind), text_(text)
{
    fixHeightMm(kButtonHeightMm);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void Button::setText(const QString &text)
{
    text_ = text;
    updateGeometry();
    update();
}

QSize Button::sizeHint() const
{
    return QSize(textWidth(theme_.font(FontRole::Body), text_) + 2 * theme_.px(kButtonPadMm),
                 theme_.px(kButtonHeightMm));
}

int Button::regionAt(const QPoint &pos) const
{
    return rect().contains(pos) ? 0 : -1;
}

void Button::activated(int)
{
    emit tapped();
}

void Button::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    const bool pressed = pressedRegion() == 0;
    const int radius = theme_.px(kButtonRadiusMm);
    p.fillRect(rect(), pal.paper);
    QColor text = theme_.onFillTextColour();
    p.setPen(Qt::NoPen);
    switch (kind_) {
    case Primary:
        p.setBrush(pressed ? pal.secondary : pal.ink);
        p.drawRoundedRect(rect(), radius, radius);
        break;
    case Accent:
        p.setBrush(pressed ? pal.accentDark : pal.accent);
        p.drawRoundedRect(rect(), radius, radius);
        break;
    case Outline: {
        text = theme_.textColour();
        p.setBrush(pressed ? pal.rule : pal.paper);
        QPen pen(pal.ink);
        pen.setWidth(kOutlinePx);
        p.setPen(pen);
        const int half = kOutlinePx / 2;
        p.drawRoundedRect(rect().adjusted(half, half, -half, -half), radius, radius);
        break;
    }
    }
    if (!isEnabled())
        text = theme_.secondaryTextColour();
    drawText(p, rect().adjusted(theme_.px(2), 0, -theme_.px(2), 0), Qt::AlignCenter, text_,
             theme_.font(FontRole::Body), text);
}

// ---- ChipRow ----

ChipRow::ChipRow(const Theme &theme, const QStringList &chips, QWidget *parent)
    : KitWidget(theme, parent), chips_(chips)
{
    fixHeightMm(kChipHeightMm);
}

QList<QRect> ChipRow::chipRects() const
{
    QList<QRect> out;
    int x = theme_.px(kPageMarginMm);
    const QFont f = theme_.font(FontRole::Small);
    for (int i = 0; i < chips_.size(); ++i) {
        const int w = textWidth(f, chips_.at(i)) + 2 * theme_.px(kChipPadMm);
        out.append(QRect(x, 0, w, height()));
        x += w + theme_.px(kChipGapMm);
    }
    return out;
}

void ChipRow::setSelected(const QList<int> &indexes)
{
    selected_ = indexes;
    update();
}

void ChipRow::clearSelection()
{
    selected_.clear();
    update();
}

int ChipRow::regionAt(const QPoint &pos) const
{
    const QList<QRect> rects = chipRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (rects.at(i)
                    .adjusted(-theme_.px(kChipGapMm) / 2, 0, theme_.px(kChipGapMm) / 2, 0)
                    .contains(pos))
            return i;
    }
    return -1;
}

void ChipRow::activated(int region)
{
    const bool nowSelected = !selected_.contains(region);
    if (nowSelected)
        selected_.append(region);
    else
        selected_.removeAll(region);
    update();
    emit chipToggled(region, nowSelected);
}

void ChipRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    const QList<QRect> rects = chipRects();
    const int radius = height() / 2;
    for (int i = 0; i < rects.size(); ++i) {
        const bool on = selected_.contains(i);
        const bool pressed = pressedRegion() == i;
        QPen pen(pal.ink);
        pen.setWidth(kOutlinePx);
        p.setPen(pen);
        p.setBrush(on ? pal.ink : (pressed ? pal.rule : pal.paper));
        const int half = kOutlinePx / 2;
        p.drawRoundedRect(rects.at(i).adjusted(half, half, -half, -half), radius, radius);
        drawText(p, rects.at(i), Qt::AlignCenter, chips_.at(i), theme_.font(FontRole::Small),
                 on ? theme_.onFillTextColour() : theme_.textColour());
    }
}

// ---- BottomTabs ----

BottomTabs::BottomTabs(const Theme &theme, const QStringList &tabs, int active, QWidget *parent)
    : KitWidget(theme, parent), tabs_(tabs), active_(active)
{
    fixHeightMm(kTabsHeightMm);
}

void BottomTabs::setActive(int index)
{
    active_ = index;
    update();
}

QRect BottomTabs::tabRect(int index) const
{
    const int n = qMax(1, tabs_.size());
    const int left = width() * index / n;
    const int right = width() * (index + 1) / n;
    return QRect(left, 0, right - left, height());
}

int BottomTabs::regionAt(const QPoint &pos) const
{
    for (int i = 0; i < tabs_.size(); ++i) {
        if (tabRect(i).contains(pos))
            return i;
    }
    return -1;
}

void BottomTabs::activated(int region)
{
    if (region != active_) {
        active_ = region;
        update();
        theme_.requestFull(this); // A tab change is a screen change.
        emit tabTapped(region);
    }
}

void BottomTabs::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    p.fillRect(QRect(0, 0, width(), kListRulePx), pal.rule);
    for (int i = 0; i < tabs_.size(); ++i) {
        const QRect r = tabRect(i);
        const bool active = i == active_;
        if (pressedRegion() == i)
            paintPressed(p, r.adjusted(0, kListRulePx, 0, 0));
        drawText(p, r.adjusted(0, kListRulePx, 0, -theme_.px(kTabUnderlineMm)), Qt::AlignCenter,
                 tabs_.at(i), theme_.font(FontRole::Body),
                 active ? theme_.textColour() : theme_.secondaryTextColour());
        if (active) {
            const int h = qMax(3, theme_.px(kTabUnderlineMm));
            const int inset = theme_.px(4.0);
            p.fillRect(QRect(r.left() + inset, r.bottom() + 1 - h, r.width() - 2 * inset, h),
                       pal.accent);
        }
    }
}

// ---- Pager ----

Pager::Pager(const Theme &theme, int page, int pageCount, QWidget *parent)
    : KitWidget(theme, parent), page_(page), pageCount_(pageCount)
{
    fixHeightMm(kPagerHeightMm);
}

void Pager::setPage(int page, int pageCount)
{
    page_ = page;
    pageCount_ = pageCount;
    update();
}

QRect Pager::previousRect() const
{
    return QRect(0, 0, theme_.px(24.0), height());
}

QRect Pager::nextRect() const
{
    const int w = theme_.px(24.0);
    return QRect(width() - w, 0, w, height());
}

int Pager::regionAt(const QPoint &pos) const
{
    if (previousRect().contains(pos))
        return page_ > 1 ? 0 : -1;
    if (nextRect().contains(pos))
        return page_ < pageCount_ ? 2 : -1;
    return 1;
}

void Pager::activated(int region)
{
    if (region == 0)
        emit previousTapped();
    else if (region == 2)
        emit nextTapped();
    else
        emit counterTapped();
}

void Pager::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    const QRect prev = previousRect();
    const QRect next = nextRect();
    if (pressedRegion() == 0)
        paintPressed(p, prev);
    if (pressedRegion() == 1)
        paintPressed(p, QRect(prev.right() + 1, 0, next.left() - prev.right() - 1, height()));
    if (pressedRegion() == 2)
        paintPressed(p, next);
    drawChevron(p, theme_, prev, false, page_ > 1 ? pal.ink : pal.rule);
    drawChevron(p, theme_, next, true, page_ < pageCount_ ? pal.ink : pal.rule);
    drawText(p, QRect(prev.right() + 1, 0, next.left() - prev.right() - 1, height()),
             Qt::AlignCenter, QStringLiteral("Page %1 of %2").arg(page_).arg(pageCount_),
             theme_.font(FontRole::Body), theme_.textColour());
}

// ---- LetterGrid ----

LetterGrid::LetterGrid(const Theme &theme, QWidget *parent) : KitWidget(theme, parent)
{
    const int rows = (letters().size() + kLetterColumns - 1) / kLetterColumns;
    setFixedSize(theme_.px(kLetterCellMm) * kLetterColumns, theme_.px(kLetterCellMm) * rows);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QString LetterGrid::letters()
{
    return QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ#");
}

QRect LetterGrid::cellRect(int index) const
{
    const int cell = theme_.px(kLetterCellMm);
    return QRect((index % kLetterColumns) * cell, (index / kLetterColumns) * cell, cell, cell);
}

int LetterGrid::regionAt(const QPoint &pos) const
{
    for (int i = 0; i < letters().size(); ++i) {
        if (cellRect(i).contains(pos))
            return i;
    }
    return -1;
}

void LetterGrid::activated(int region)
{
    emit letterTapped(letters().mid(region, 1));
}

void LetterGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.fillRect(rect(), theme_.palette().paper);
    for (int i = 0; i < letters().size(); ++i) {
        if (pressedRegion() == i)
            paintPressed(p, cellRect(i));
        drawText(p, cellRect(i), Qt::AlignCenter, letters().mid(i, 1), theme_.font(FontRole::Body),
                 theme_.textColour());
    }
}

// ---- SearchField ----

SearchField::SearchField(const Theme &theme, QWidget *parent) : KitWidget(theme, parent)
{
    fixHeightMm(kSearchHeightMm);
}

void SearchField::setText(const QString &text)
{
    text_ = text;
    update();
}

int SearchField::regionAt(const QPoint &pos) const
{
    return rect().contains(pos) ? 0 : -1;
}

void SearchField::activated(int)
{
    emit tapped();
}

void SearchField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    const int margin = theme_.px(kPageMarginMm);
    p.fillRect(rect(), pal.paper);
    const QRect box = rect().adjusted(margin, 0, -margin, 0);
    QPen pen(pal.ink);
    pen.setWidth(kOutlinePx);
    p.setPen(pen);
    p.setBrush(pressedRegion() == 0 ? pal.rule : pal.paper);
    const int half = kOutlinePx / 2;
    p.drawRoundedRect(box.adjusted(half, half, -half, -half), theme_.px(kButtonRadiusMm),
                      theme_.px(kButtonRadiusMm));
    const QRect icon(box.left() + theme_.px(1.0), box.top(), box.height(), box.height());
    drawSearchIcon(p, theme_, icon, pal.ink);
    const bool empty = text_.isEmpty();
    drawText(p,
             QRect(icon.right() + theme_.px(1.0), box.top(),
                   box.right() - icon.right() - theme_.px(3.0), box.height()),
             Qt::AlignLeft | Qt::AlignVCenter, empty ? QStringLiteral("Search") : text_,
             theme_.font(FontRole::Body),
             empty ? theme_.secondaryTextColour() : theme_.textColour());
}

// ---- Banner ----

Banner::Banner(const Theme &theme, const QString &text, const QString &action, QWidget *parent)
    : KitWidget(theme, parent), text_(text), action_(action)
{
    fixHeightMm(kBannerHeightMm);
}

QRect Banner::actionRect() const
{
    const int w = textWidth(theme_.font(FontRole::Body), action_) + 2 * theme_.px(kPageMarginMm);
    return QRect(width() - w, kHeaderRulePx, w, height() - 2 * kHeaderRulePx);
}

int Banner::regionAt(const QPoint &pos) const
{
    return actionRect().contains(pos) ? 0 : -1;
}

void Banner::activated(int)
{
    emit actionTapped();
}

void Banner::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    p.fillRect(QRect(0, 0, width(), kHeaderRulePx), pal.ink);
    p.fillRect(QRect(0, height() - kHeaderRulePx, width(), kHeaderRulePx), pal.ink);
    const QRect action = actionRect();
    if (pressedRegion() == 0)
        paintPressed(p, action);
    const int margin = theme_.px(kPageMarginMm);
    QFont bold = theme_.font(FontRole::Body);
    bold.setWeight(kWeightTitle);
    bold.setUnderline(true);
    drawText(p, action, Qt::AlignCenter, action_, bold, theme_.textColour());
    drawText(p, QRect(margin, kHeaderRulePx, action.left() - margin, height() - 2 * kHeaderRulePx),
             Qt::AlignLeft | Qt::AlignVCenter, text_, theme_.font(FontRole::Small),
             theme_.textColour());
}

// ---- DownloadBadge ----

DownloadBadge::DownloadBadge(const Theme &theme, BadgeState state, const QString &text,
                             QWidget *parent)
    : KitWidget(theme, parent), state_(state), text_(text)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setState(state, text);
}

void DownloadBadge::setState(BadgeState state, const QString &text)
{
    state_ = state;
    text_ = text;
    setFixedSize(badgeWidth(theme_, state_, text_), theme_.px(kBadgeSizeMm));
    update();
}

int DownloadBadge::regionAt(const QPoint &) const
{
    return state_ == BadgeState::Download ? 0 : -1;
}

void DownloadBadge::activated(int)
{
    emit tapped();
}

void DownloadBadge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.fillRect(rect(), theme_.palette().paper);
    paintBadge(p, theme_, rect(), state_, text_, pressedRegion() == 0);
}

// ---- CoverView ----

CoverView::CoverView(const Theme &theme, const CoverSpec &spec, double widthMm, QWidget *parent)
    : KitWidget(theme, parent), spec_(spec)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const int w = theme_.px(widthMm);
    setFixedSize(w, int(w * kCoverRatio + 0.5));
}

void CoverView::setSpec(const CoverSpec &spec)
{
    spec_ = spec;
    update();
}

void CoverView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.fillRect(rect(), theme_.palette().paper);
    paintCover(p, theme_, rect(), spec_);
}

// ---- CoverRow ----

CoverRow::CoverRow(const Theme &theme, const QList<CoverSpec> &covers, QWidget *parent)
    : KitWidget(theme, parent), covers_(covers)
{
    const double coverH = kCoverWidthMm * kCoverRatio;
    fixHeightMm(coverH + kCoverRowLabelMm);
}

QRect CoverRow::coverRect(int index) const
{
    const int w = theme_.px(kCoverWidthMm);
    const int h = int(w * kCoverRatio + 0.5);
    return QRect(theme_.px(kPageMarginMm) + index * (w + theme_.px(kCoverGapMm)), 0, w, h);
}

int CoverRow::regionAt(const QPoint &pos) const
{
    const int visibleCovers = qMin(covers_.size(), theme_.profile().readingNowCovers);
    for (int i = 0; i < visibleCovers; ++i) {
        QRect r = coverRect(i);
        r.setBottom(height() - 1);
        if (r.contains(pos))
            return i;
    }
    return -1;
}

void CoverRow::activated(int region)
{
    emit coverTapped(region);
}

void CoverRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.fillRect(rect(), theme_.palette().paper);
    const int visibleCovers = qMin(covers_.size(), theme_.profile().readingNowCovers);
    for (int i = 0; i < visibleCovers; ++i) {
        const QRect r = coverRect(i);
        paintCover(p, theme_, r, covers_.at(i));
        if (pressedRegion() == i) {
            p.fillRect(r, QColor(0, 0, 0, 60));
        }
        drawText(p,
                 QRect(r.left(), r.bottom() + theme_.px(1.5), r.width(),
                       theme_.px(kCoverRowLabelMm - 1.5)),
                 Qt::AlignLeft | Qt::AlignTop, covers_.at(i).title, theme_.font(FontRole::Small),
                 theme_.textColour());
    }
}

// ---- ListRow ----

ListRow::ListRow(const Theme &theme, const ListRowSpec &spec, QWidget *parent)
    : KitWidget(theme, parent), spec_(spec)
{
    fixHeightMm(kListRowHeightMm);
}

QRect ListRow::badgeRect() const
{
    if (spec_.badge == BadgeState::None)
        return QRect();
    const int w = badgeWidth(theme_, spec_.badge, spec_.badgeText);
    const int h = theme_.px(kBadgeSizeMm);
    return QRect(width() - theme_.px(kPageMarginMm) - w, (height() - h) / 2, w, h);
}

int ListRow::regionAt(const QPoint &pos) const
{
    if (spec_.badge == BadgeState::Download
        && badgeRect()
                   .adjusted(-theme_.px(2), -theme_.px(2), theme_.px(2), theme_.px(2))
                   .contains(pos))
        return 1;
    return rect().contains(pos) ? 0 : -1;
}

void ListRow::activated(int region)
{
    if (region == 1)
        emit badgeTapped();
    else
        emit tapped();
}

void ListRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    if (pressedRegion() == 0)
        paintPressed(p, rect());
    const int margin = theme_.px(kPageMarginMm);
    const int cw = theme_.px(kListCoverWidthMm);
    const int ch = theme_.px(kListCoverHeightMm);
    const QRect cover(margin, (height() - ch) / 2, cw, ch);
    paintCover(p, theme_, cover, spec_.cover);
    const QRect badge = badgeRect();
    const int textLeft = cover.right() + 1 + theme_.px(4.0);
    const int textRight = (badge.isNull() ? width() - margin : badge.left()) - theme_.px(3.0);
    const QFont body = theme_.font(FontRole::Body);
    const QFont small = theme_.font(FontRole::Small);
    const int lineGap = theme_.px(1.0);
    const int bodyH = QFontMetrics(body).height();
    const int smallH = QFontMetrics(small).height();
    const int top = (height() - (bodyH + lineGap + smallH)) / 2;
    const QColor titleColour =
            spec_.unavailable ? theme_.secondaryTextColour() : theme_.textColour();
    drawText(p, QRect(textLeft, top, textRight - textLeft, bodyH), Qt::AlignLeft | Qt::AlignVCenter,
             spec_.title, body, titleColour);
    drawText(p, QRect(textLeft, top + bodyH + lineGap, textRight - textLeft, smallH),
             Qt::AlignLeft | Qt::AlignVCenter, spec_.subtitle, small, theme_.secondaryTextColour());
    if (!badge.isNull())
        paintBadge(p, theme_, badge, spec_.badge, spec_.badgeText, pressedRegion() == 1);
    p.fillRect(QRect(margin, height() - kListRulePx, width() - 2 * margin, kListRulePx), pal.rule);
}

// ---- SegmentedProgress ----

SegmentedProgress::SegmentedProgress(const Theme &theme, int total, int done, QWidget *parent)
    : KitWidget(theme, parent), total_(total), done_(done)
{
    fixHeightMm(kSegmentBarHeightMm);
}

void SegmentedProgress::setProgress(int total, int done)
{
    total_ = total;
    done_ = done;
    update();
}

void SegmentedProgress::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    if (total_ <= 0)
        return;
    const int gap = qMax(2, theme_.px(kSegmentGapMm));
    for (int i = 0; i < total_; ++i) {
        // Integer edges so every segment is crisp.
        const int left = (width() + gap) * i / total_;
        const int right = (width() + gap) * (i + 1) / total_ - gap;
        p.fillRect(QRect(left, 0, right - left, height()), i < done_ ? pal.accent : pal.rule);
    }
}

// ---- CoverStack ----

CoverStack::CoverStack(const Theme &theme, const CoverSpec &front, QWidget *parent)
    : KitWidget(theme, parent), front_(front)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const int step = theme_.px(kCoverStackStepMm);
    const int w = theme_.px(kListCoverWidthMm * 1.6);
    setFixedSize(w + 2 * step, int(w * kCoverRatio + 0.5) + 2 * step);
}

void CoverStack::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    const Palette &pal = theme_.palette();
    p.fillRect(rect(), pal.paper);
    const int step = theme_.px(kCoverStackStepMm);
    const int w = width() - 2 * step;
    const int h = height() - 2 * step;
    // Two cards behind, offset up and to the right, then the front cover.
    for (int i = 2; i >= 1; --i) {
        const QRect r(i * step, 2 * step - i * step, w, h);
        p.fillRect(r, pal.rule);
        p.setPen(QPen(pal.secondary, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
    paintCover(p, theme_, QRect(0, 2 * step, w, h), front_);
}

// ---- Dialog ----

class Dialog::Box : public QWidget
{
public:
    Box(const Theme &theme, QWidget *parent) : QWidget(parent), theme_(theme) { }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), theme_.palette().paper);
        // Exactly kDialogBorderPx of Ink on every side; no shadow.
        const QColor ink = theme_.palette().ink;
        p.fillRect(QRect(0, 0, width(), kDialogBorderPx), ink);
        p.fillRect(QRect(0, height() - kDialogBorderPx, width(), kDialogBorderPx), ink);
        p.fillRect(QRect(0, 0, kDialogBorderPx, height()), ink);
        p.fillRect(QRect(width() - kDialogBorderPx, 0, kDialogBorderPx, height()), ink);
    }

private:
    Theme theme_;
};

Dialog::Dialog(const Theme &theme, const QString &text, const QStringList &actions, QWidget *parent)
    : KitWidget(theme, parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    box_ = new Box(theme_, this);
    QVBoxLayout *column = new QVBoxLayout(box_);
    const int pad = theme_.px(kDialogPadMm) + kDialogBorderPx;
    column->setContentsMargins(pad, pad, pad, pad);
    column->setSpacing(theme_.px(kDialogGapMm));

    QLabel *label = new QLabel(text, box_);
    label->setWordWrap(true);
    label->setFont(theme_.font(FontRole::Title));
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, theme_.textColour());
    label->setPalette(pal);
    column->addWidget(label);

    const bool stacked = actions.size() > 2;
    QBoxLayout *buttons = stacked ? static_cast<QBoxLayout *>(new QVBoxLayout())
                                  : static_cast<QBoxLayout *>(new QHBoxLayout());
    buttons->setSpacing(theme_.px(kDialogGapMm));
    for (int i = 0; i < actions.size(); ++i) {
        // The first action is the decision (black); the rest are outlined.
        Button *b =
                new Button(theme_, i == 0 ? Button::Primary : Button::Outline, actions.at(i), box_);
        b->setProperty("actionIndex", i);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, SIGNAL(tapped()), this, SLOT(onActionTapped()));
        buttons_.append(b);
        buttons->addWidget(b);
    }
    column->addLayout(buttons);

    if (parent) {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
    layoutBox();
}

void Dialog::onActionTapped()
{
    QObject *s = sender();
    if (s)
        emit actionTapped(s->property("actionIndex").toInt());
}

QRect Dialog::boxRect() const
{
    return box_->geometry();
}

void Dialog::layoutBox()
{
    const int margin = theme_.px(kPageMarginMm);
    const int w = qMin(width() - 2 * margin, theme_.px(kDialogMaxWidthMm));
    if (w <= 0)
        return;
    box_->setFixedWidth(w);
    box_->layout()->activate();
    const int h = box_->layout()->heightForWidth(w);
    box_->setFixedHeight(h > 0 ? h : box_->layout()->sizeHint().height());
    box_->move((width() - box_->width()) / 2, (height() - box_->height()) / 2);
}

int Dialog::regionAt(const QPoint &) const
{
    return 0; // Modal: presses on the dimmed page go nowhere.
}

bool Dialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize) {
        setGeometry(static_cast<QWidget *>(watched)->rect());
    }
    return KitWidget::eventFilter(watched, event);
}

void Dialog::resizeEvent(QResizeEvent *)
{
    layoutBox();
}

void Dialog::showEvent(QShowEvent *)
{
    raise();
    layoutBox();
    theme_.requestFull(this); // A dialog is a screen change.
}

void Dialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, kDimAlpha));
}

} // namespace kit
} // namespace ui
} // namespace smalt
