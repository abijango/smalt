#include "blocks.h"

#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace smalt {
namespace ui {
namespace stats {

QString scopeAllDevices()
{
    return QStringLiteral("All devices");
}

QString scopeThisKobo()
{
    return QStringLiteral("This Kobo");
}

QStringList wrapLines(const QFont &font, const QString &text, int width, int maxLines)
{
    // (QString::SkipEmptyParts is deprecated in new Qt and Qt::SkipEmptyParts is not in 5.2.)
    QStringList words = text.split(QLatin1Char(' '));
    words.removeAll(QString());
    QStringList lines;
    QString cur;
    for (int i = 0; i < words.size(); ++i) {
        const QString t = cur.isEmpty() ? words.at(i) : cur + QLatin1Char(' ') + words.at(i);
        if (cur.isEmpty() || textWidth(font, t) <= width) {
            cur = t;
        } else {
            lines.append(cur);
            cur = words.at(i);
        }
    }
    if (!cur.isEmpty())
        lines.append(cur);
    if (maxLines > 0 && lines.size() > maxLines) {
        QString last = lines.at(maxLines - 1);
        for (int i = maxLines; i < lines.size(); ++i)
            last += QLatin1Char(' ') + lines.at(i);
        lines = lines.mid(0, maxLines - 1);
        lines.append(QFontMetrics(font).elidedText(last, Qt::ElideRight, width));
        // elidedText cuts at the end of the line; make the cut visible.
        if (!lines.last().endsWith(QChar(0x2026)))
            lines.last() += QChar(0x2026);
    }
    return lines;
}

// ---- ScopeTitle ----

ScopeTitle::ScopeTitle(const Style &style, const QString &title, const QString &scope,
                       const QString &aside, QWidget *parent)
    : StatsWidget(style, parent), title_(title), scope_(scope), aside_(aside)
{
    fixHeightMm(metrics::kTitlePadTopMm + metrics::kTitleRowMm
                + (aside.isEmpty() ? 0.0 : metrics::kAsideRowMm));
}

void ScopeTitle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int m = s_.margin();
    const int pad = s_.px(metrics::kTitlePadTopMm);
    const int rowH = s_.px(metrics::kTitleRowMm);
    int right = width() - m;
    if (!scope_.isEmpty()) {
        const QFont f = s_.small();
        const int padX = s_.px(metrics::kTagPadMm);
        const int w = textWidth(f, scope_) + 2 * padX;
        const int h = s_.px(metrics::kTagHeightMm);
        const QRect tag(right - w, pad + (rowH - h) / 2, w, h);
        QPen pen(s_.palette.secondary);
        pen.setWidth(std::max(1, s_.px(0.15)));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const int half = pen.width() / 2;
        p.drawRoundedRect(tag.adjusted(half, half, -half, -half), s_.px(0.8), s_.px(0.8));
        drawText(p, tag, Qt::AlignCenter, scope_, f, s_.palette.secondary);
        right -= w + s_.px(2.0);
    }
    drawText(p, QRect(m, pad, std::max(0, right - m), rowH), Qt::AlignLeft | Qt::AlignVCenter,
             title_, s_.titleFont(), s_.palette.ink);
    if (!aside_.isEmpty())
        drawText(p, QRect(m, pad + rowH - s_.px(1.0), width() - 2 * m, s_.px(metrics::kAsideRowMm)),
                 Qt::AlignLeft | Qt::AlignVCenter, aside_, s_.small(), s_.palette.secondary);
}

// ---- RuleLine ----

RuleLine::RuleLine(const Style &style, QWidget *parent) : StatsWidget(style, parent)
{
    fixHeightPx(metrics::kRulePx);
}

void RuleLine::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), s_.palette.rule);
}

// ---- TilesWidget ----

TilesWidget::TilesWidget(const Style &style, const QVector<Tile> &tiles, QWidget *parent)
    : StatsWidget(style, parent), tiles_(tiles)
{
    const int rows = (tiles_.size() + 1) / 2;
    fixHeightPx(rows * s_.px(metrics::kTileRowMm) + metrics::kRulePx);
}

void TilesWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int rowH = s_.px(metrics::kTileRowMm);
    const int rows = (tiles_.size() + 1) / 2;
    const int m = s_.margin();
    const int pad = s_.px(4.0);
    const int bigH = QFontMetrics(s_.valueFont()).height();
    const int capH = QFontMetrics(s_.small()).height();
    for (int r = 0; r < rows; ++r) {
        const int top = r * rowH;
        p.fillRect(QRect(0, top, width(), metrics::kRulePx), s_.palette.rule);
        p.fillRect(QRect(width() / 2, top, metrics::kRulePx, rowH), s_.palette.rule);
        for (int c = 0; c < 2; ++c) {
            const int i = r * 2 + c;
            if (i >= tiles_.size())
                break;
            const int x = c == 0 ? m : width() / 2 + pad;
            const int w = (c == 0 ? width() / 2 : width()) - x - (c == 0 ? pad : m);
            const int y = top + (rowH - bigH - capH) / 2;
            drawText(p, QRect(x, y, w, bigH), Qt::AlignLeft | Qt::AlignVCenter, tiles_.at(i).value,
                     s_.valueFont(), s_.palette.ink);
            drawText(p, QRect(x, y + bigH, w, capH), Qt::AlignLeft | Qt::AlignVCenter,
                     tiles_.at(i).caption, s_.small(), s_.palette.secondary);
        }
    }
    p.fillRect(QRect(0, rows * rowH, width(), metrics::kRulePx), s_.palette.rule);
}

// ---- GoalWidget ----

GoalWidget::GoalWidget(const Style &style, const QString &title, const QString &pace,
                       double fraction, QWidget *parent)
    : StatsWidget(style, parent), title_(title), pace_(pace), fraction_(fraction)
{
    fixHeightPx(s_.px(1.0) + QFontMetrics(s_.valueFont()).height() + s_.px(1.0)
                + s_.px(metrics::kBarMm) + s_.px(1.2));
}

void GoalWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int m = s_.margin();
    const int textH = QFontMetrics(s_.valueFont()).height();
    const int top = s_.px(1.0);
    const int paceW = std::min(textWidth(s_.small(), pace_) + 6, (width() - 2 * m) / 2);
    drawText(p, QRect(width() - m - paceW, top, paceW, textH), Qt::AlignRight | Qt::AlignVCenter,
             pace_, s_.small(), s_.palette.secondary);
    drawText(p, QRect(m, top, width() - 2 * m - paceW - s_.px(2.0), textH),
             Qt::AlignLeft | Qt::AlignVCenter, title_, s_.valueFont(), s_.palette.ink);
    const QRect bar(m, top + textH + s_.px(1.0), width() - 2 * m, s_.px(metrics::kBarMm));
    drawProgress(p, s_, bar, fraction_);
}

// ---- RowsWidget ----

RowsWidget::RowsWidget(const Style &style, Mode mode, const QVector<Row> &rows, QWidget *parent)
    : StatsWidget(style, parent), mode_(mode), rows_(rows)
{
    fixHeightPx(rows_.size() * s_.px(metrics::kRowMm));
}

void RowsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int rowH = s_.px(metrics::kRowMm);
    const int m = s_.margin();
    const int right = width() - m;
    for (int i = 0; i < rows_.size(); ++i) {
        const QRect r(0, i * rowH, width(), rowH);
        const Row &row = rows_.at(i);
        if (mode_ == Records) {
            const int valueRight = m + int((width() - 2 * m) * 0.68);
            const int valueW = textWidth(s_.boldBody(), row.second) + 6;
            drawText(p, QRect(m, r.top(), valueRight - m - valueW - s_.px(2.0), rowH),
                     Qt::AlignLeft | Qt::AlignVCenter, row.first, s_.body(), s_.palette.ink);
            drawText(p, QRect(valueRight - valueW, r.top(), valueW, rowH),
                     Qt::AlignRight | Qt::AlignVCenter, row.second, s_.boldBody(), s_.palette.ink);
            const int noteW = right - valueRight - s_.px(3.0);
            drawText(p, QRect(right - noteW, r.top(), noteW, rowH),
                     Qt::AlignRight | Qt::AlignVCenter, row.third, s_.small(),
                     s_.palette.secondary);
        } else {
            const int dayW = int((width() - 2 * m) * 0.28);
            const int durW = textWidth(s_.boldBody(), row.third) + 6;
            drawText(p, QRect(m, r.top(), dayW, rowH), Qt::AlignLeft | Qt::AlignVCenter, row.first,
                     s_.body(), s_.palette.ink);
            drawText(p, QRect(m + dayW, r.top(), right - m - dayW - durW - s_.px(2.0), rowH),
                     Qt::AlignLeft | Qt::AlignVCenter, row.second, s_.body(), s_.palette.secondary);
            drawText(p, QRect(right - durW, r.top(), durW, rowH), Qt::AlignRight | Qt::AlignVCenter,
                     row.third, s_.boldBody(), s_.palette.ink);
        }
        p.fillRect(QRect(0, r.bottom(), width(), metrics::kRulePx), s_.palette.rule);
    }
}

// ---- TextLines ----

TextLines::TextLines(const Style &style, const QList<Line> &lines, QWidget *parent)
    : StatsWidget(style, parent), lines_(lines)
{
    int h = 2 * s_.px(1.2);
    for (const Line &l : lines_) {
        const QFont f = l.small ? s_.small() : (l.bold ? s_.boldBody() : s_.body());
        h += QFontMetrics(f).height();
    }
    fixHeightPx(h);
}

void TextLines::setSideBySide(bool on)
{
    sideBySide_ = on;
    update();
}

void TextLines::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int x = sideBySide_ ? 0 : s_.margin();
    const int w = width() - x - (sideBySide_ ? s_.margin() : s_.margin());
    int y = s_.px(1.2);
    for (const Line &l : lines_) {
        const QFont f = l.small ? s_.small() : (l.bold ? s_.boldBody() : s_.body());
        const int h = QFontMetrics(f).height();
        drawText(p, QRect(x, y, w, h), Qt::AlignLeft | Qt::AlignVCenter, l.text, f,
                 l.secondary ? s_.palette.secondary : s_.palette.ink);
        y += h;
    }
}

// ---- BadgeRow ----

BadgeRow::BadgeRow(const Style &style, const QString &name, const QString &toGo, double fraction,
                   QWidget *parent)
    : StatsWidget(style, parent), name_(name), toGo_(toGo), fraction_(fraction)
{
    const int text = QFontMetrics(s_.titleFont()).height() + s_.px(0.8) + s_.px(metrics::kBarMm)
            + s_.px(0.8) + QFontMetrics(s_.small()).height();
    fixHeightPx(std::max(s_.px(metrics::kBadgeIconMm) + s_.px(2.0), text + s_.px(2.0)));
}

void BadgeRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int m = s_.margin();
    const int d = s_.px(metrics::kBadgeIconMm);
    const QRect icon(m, (height() - d) / 2, d, d);
    p.setPen(Qt::NoPen);
    p.setBrush(s_.palette.accentTint);
    p.drawEllipse(icon);
    // A five-point star.
    QPainterPath star;
    const QPointF c = icon.center() + QPointF(0.5, 0.5);
    const double outer = d * 0.30;
    const double inner = outer * 0.42;
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < 10; ++i) {
        const double r = (i % 2 == 0) ? outer : inner;
        const double a = -pi / 2 + i * pi / 5;
        const QPointF pt(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
        if (i == 0)
            star.moveTo(pt);
        else
            star.lineTo(pt);
    }
    star.closeSubpath();
    p.setBrush(s_.palette.accentDark);
    p.drawPath(star);

    const int x = icon.right() + s_.px(3.5);
    const int w = width() - x - m;
    const int nameH = QFontMetrics(s_.titleFont()).height();
    const int smallH = QFontMetrics(s_.small()).height();
    const int barH = s_.px(metrics::kBarMm);
    const int block = nameH + s_.px(0.8) + barH + s_.px(0.8) + smallH;
    int y = (height() - block) / 2;
    drawText(p, QRect(x, y, w, nameH), Qt::AlignLeft | Qt::AlignVCenter, name_, s_.titleFont(),
             s_.palette.ink);
    y += nameH + s_.px(0.8);
    if (fraction_ >= 0)
        drawProgress(p, s_, QRect(x, y, w, barH), fraction_);
    y += barH + s_.px(0.8);
    drawText(p, QRect(x, y, w, smallH), Qt::AlignLeft | Qt::AlignVCenter, toGo_, s_.small(),
             s_.palette.secondary);
}

// ---- QuoteWidget ----

QuoteWidget::QuoteWidget(const Style &style, const QString &text, const QString &source,
                         int maxLines, QWidget *parent)
    : StatsWidget(style, parent), source_(source)
{
    lines_ = wrapLines(s_.body(), text, s_.profile.widthPx - 2 * s_.margin(), maxLines);
    int h = 2 * s_.px(1.5) + lines_.size() * QFontMetrics(s_.body()).height();
    if (!source_.isEmpty())
        h += QFontMetrics(s_.small()).height();
    fixHeightPx(h);
}

void QuoteWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int m = s_.margin();
    const int lh = QFontMetrics(s_.body()).height();
    int y = s_.px(1.5);
    // The ellipsis was computed for the device width; re-wrap if the widget is narrower.
    for (const QString &line : lines_) {
        drawText(p, QRect(m, y, width() - 2 * m, lh), Qt::AlignLeft | Qt::AlignVCenter, line,
                 s_.body(), s_.palette.ink);
        y += lh;
    }
    if (!source_.isEmpty())
        drawText(p, QRect(m, y, width() - 2 * m, QFontMetrics(s_.small()).height()),
                 Qt::AlignLeft | Qt::AlignVCenter, source_, s_.small(), s_.palette.secondary);
}

// ---- NoteWidget ----

NoteWidget::NoteWidget(const Style &style, const QString &text, QWidget *parent)
    : StatsWidget(style, parent), text_(text)
{
    fixHeightMm(9.0);
}

void NoteWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    drawText(p, QRect(s_.margin(), 0, width() - 2 * s_.margin(), height()),
             Qt::AlignLeft | Qt::AlignVCenter, text_, s_.small(), s_.palette.secondary);
}

// ---- ForecastBox ----

ForecastBox::ForecastBox(const Style &style, const QString &headline, const QString &detail,
                         QWidget *parent)
    : StatsWidget(style, parent), headline_(headline), detail_(detail)
{
    const int pad = s_.px(metrics::kBoxPadMm);
    const int inner = s_.profile.widthPx - 2 * s_.margin() - 2 * pad;
    const int detailLines = detail_.isEmpty() ? 0 : wrapLines(s_.small(), detail_, inner, 2).size();
    fixHeightPx(2 * s_.px(1.8) + 2 * pad + QFontMetrics(s_.valueFont()).height()
                + detailLines * QFontMetrics(s_.small()).height());
}

void ForecastBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int pad = s_.px(metrics::kBoxPadMm);
    const QRect box(s_.margin(), s_.px(1.8), width() - 2 * s_.margin(), height() - 2 * s_.px(1.8));
    QPen pen(s_.palette.ink);
    pen.setWidth(metrics::kBoxBorderPx);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const int half = metrics::kBoxBorderPx / 2;
    p.drawRoundedRect(box.adjusted(half, half, -half, -half), s_.px(metrics::kBoxRadiusMm),
                      s_.px(metrics::kBoxRadiusMm));
    const int x = box.left() + pad;
    const int w = box.width() - 2 * pad;
    int y = box.top() + pad;
    const int headH = QFontMetrics(s_.valueFont()).height();
    drawText(p, QRect(x, y, w, headH), Qt::AlignLeft | Qt::AlignVCenter, headline_, s_.valueFont(),
             s_.palette.ink);
    y += headH;
    const int lh = QFontMetrics(s_.small()).height();
    const QStringList lines =
            detail_.isEmpty() ? QStringList() : wrapLines(s_.small(), detail_, w, 2);
    for (const QString &line : lines) {
        drawText(p, QRect(x, y, w, lh), Qt::AlignLeft | Qt::AlignVCenter, line, s_.small(),
                 s_.palette.secondary);
        y += lh;
    }
}

} // namespace stats
} // namespace ui
} // namespace smalt
