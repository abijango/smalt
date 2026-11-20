#include "charts.h"

#include <QPaintEvent>

#include <algorithm>

namespace smalt {
namespace ui {
namespace stats {

namespace {

const double kHeatmapTopPadMm = 0.5;

// Seven-day dots and strips are drawn from the same level bands as the heatmap.
QColor levelColour(const Style &s, int minutes)
{
    return s.palette.heatmap[smalt::stats::heatLevel(minutes)];
}

} // namespace

// ---- HeatmapWidget ----

HeatmapWidget::HeatmapWidget(const Style &style, const smalt::stats::HeatmapGrid &grid,
                             QWidget *parent)
    : StatsWidget(style, parent), grid_(grid)
{
    const int cell = cellSize(s_, grid_.weeks, s_.profile.widthPx);
    const int gap = gapPx();
    fixHeightPx(s_.px(kHeatmapTopPadMm) + 7 * cell + 6 * gap + s_.px(metrics::kLegendRowMm));
}

int HeatmapWidget::gapPx() const
{
    return std::max(1, s_.px(metrics::kHeatmapGapMm));
}

int HeatmapWidget::cellSize(const Style &style, int weeks, int widthPx)
{
    const int gap = std::max(1, style.px(metrics::kHeatmapGapMm));
    const int avail = widthPx - 2 * style.margin();
    return std::max(2, (avail - (std::max(1, weeks) - 1) * gap) / std::max(1, weeks));
}

int HeatmapWidget::cellSizeFor(int widthPx) const
{
    return cellSize(s_, grid_.weeks, widthPx);
}

QRect HeatmapWidget::cellRect(int week, int row) const
{
    const int cell = cellSizeFor(width());
    const int gap = gapPx();
    const int gridW = grid_.weeks * cell + (grid_.weeks - 1) * gap;
    const int x0 = s_.margin() + (width() - 2 * s_.margin() - gridW) / 2;
    return QRect(x0 + week * (cell + gap), s_.px(kHeatmapTopPadMm) + row * (cell + gap), cell,
                 cell);
}

void HeatmapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.setRenderHint(QPainter::Antialiasing, false); // Crisp squares.
    fillPaper(p);
    if (grid_.weeks <= 0)
        return;
    for (int w = 0; w < grid_.weeks; ++w) {
        for (int r = 0; r < 7; ++r) {
            const smalt::stats::HeatCell &c = grid_.cell(w, r);
            if (c.future)
                continue;
            p.fillRect(cellRect(w, r), s_.palette.heatmap[c.level]);
        }
    }

    // Legend: Less [5 steps] More, right-aligned under the grid.
    const int cell = cellSizeFor(width());
    const int gridBottom = s_.px(kHeatmapTopPadMm) + 7 * cell + 6 * gapPx();
    const QRect row(0, gridBottom, width(), s_.px(metrics::kLegendRowMm));
    const QFont f = s_.small();
    const int sw = s_.px(metrics::kLegendSwatchMm);
    const int swGap = s_.px(0.8);
    const int textGap = s_.px(1.6);
    int right = width() - s_.margin();
    const QString more = QStringLiteral("More");
    const QString less = QStringLiteral("Less");
    const int moreW = textWidth(f, more);
    drawText(p, QRect(right - moreW, row.top(), moreW, row.height()),
             Qt::AlignRight | Qt::AlignVCenter, more, f, s_.palette.secondary);
    right -= moreW + textGap;
    for (int i = 4; i >= 0; --i) {
        const QRect r(right - sw, row.top() + (row.height() - sw) / 2, sw, sw);
        p.fillRect(r, s_.palette.heatmap[i]);
        right -= sw + swGap;
    }
    const int lessRight = right + swGap - textGap; // Left of the first swatch, minus the gap.
    const int lessW = textWidth(f, less);
    drawText(p, QRect(lessRight - lessW, row.top(), lessW, row.height()),
             Qt::AlignRight | Qt::AlignVCenter, less, f, s_.palette.secondary);
}

// ---- StreakWidget ----

StreakWidget::StreakWidget(const Style &style, const QString &big, const QString &caption,
                           const QVector<bool> &dots, const QVector<QString> &initials,
                           QWidget *parent)
    : StatsWidget(style, parent), big_(big), caption_(caption), dots_(dots), initials_(initials)
{
    fixHeightMm(metrics::kStreakRowMm);
}

void StreakWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const int m = s_.margin();
    const int h = height();
    const int bigH = QFontMetrics(s_.valueFont()).height();
    const int capH = QFontMetrics(s_.small()).height();
    const int top = (h - bigH - capH) / 2;
    const int textW = width() / 2 - m;
    if (!big_.isEmpty()) {
        drawText(p, QRect(m, top, textW, bigH), Qt::AlignLeft | Qt::AlignVCenter, big_,
                 s_.valueFont(), s_.palette.ink);
        drawText(p, QRect(m, top + bigH, textW, capH), Qt::AlignLeft | Qt::AlignVCenter, caption_,
                 s_.small(), s_.palette.secondary);
    } else {
        drawText(p, QRect(m, 0, textW, h), Qt::AlignLeft | Qt::AlignVCenter, caption_, s_.body(),
                 s_.palette.ink);
    }

    if (dots_.isEmpty())
        return;
    const int d = s_.px(metrics::kDotMm);
    const int pitch = s_.px(metrics::kDotPitchMm);
    const int n = dots_.size();
    const int total = (n - 1) * pitch + d;
    const int x0 = width() - m - total;
    const int labelH = capH;
    const int dotTop = (h - d - labelH) / 2;
    const int ring = std::max(2, s_.px(0.45));
    for (int i = 0; i < n; ++i) {
        const int x = x0 + i * pitch;
        const QRect r(x, dotTop, d, d);
        if (dots_.at(i)) {
            p.setPen(Qt::NoPen);
            p.setBrush(s_.palette.accent);
            p.drawEllipse(r);
        } else {
            QPen pen(s_.palette.accent);
            pen.setWidth(ring);
            p.setPen(pen);
            p.setBrush(s_.palette.paper);
            p.drawEllipse(r.adjusted(ring / 2, ring / 2, -ring / 2, -ring / 2));
        }
        if (i < initials_.size())
            drawText(p, QRect(x - (pitch - d) / 2, dotTop + d, pitch, labelH), Qt::AlignCenter,
                     initials_.at(i), s_.small(), s_.palette.ink);
    }
}

// ---- SessionStrip ----

SessionStrip::SessionStrip(const Style &style, const QVector<smalt::stats::TimeBlock> &blocks,
                           QWidget *parent)
    : StatsWidget(style, parent), blocks_(blocks)
{
    fixHeightMm(metrics::kStripMm);
}

QRect SessionStrip::trackRect() const
{
    const int m = s_.margin();
    const int h = s_.px(metrics::kStripTrackMm);
    const int centre = s_.px(metrics::kStripBlockMm) / 2 + s_.px(0.5);
    return QRect(m, centre - h / 2, width() - 2 * m, h);
}

QRect SessionStrip::blockRect(int index) const
{
    const QRect track = trackRect();
    const smalt::stats::TimeBlock &b = blocks_.at(index);
    const int x0 = track.left() + int(double(track.width()) * b.startSec / 86400.0);
    const int x1 = track.left() + int(double(track.width()) * b.endSec / 86400.0 + 0.5);
    const int w = std::max(s_.px(metrics::kStripMinBlockMm), x1 - x0);
    const int bh = s_.px(metrics::kStripBlockMm);
    const int x = std::min(x0, track.right() + 1 - w);
    return QRect(x, track.center().y() - bh / 2, w, bh);
}

void SessionStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    fillPaper(p);
    const QRect track = trackRect();
    p.setPen(Qt::NoPen);
    p.setBrush(s_.palette.heatmap[0]);
    p.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
    p.setBrush(s_.palette.accent);
    for (int i = 0; i < blocks_.size(); ++i)
        p.drawRoundedRect(blockRect(i), s_.px(0.5), s_.px(0.5));

    const QFont f = s_.small();
    const int labelTop = s_.px(metrics::kStripBlockMm) + s_.px(0.8);
    const int labelH = QFontMetrics(f).height();
    const char *const labels[5] = { "00", "06", "12", "18", "24" };
    for (int i = 0; i < 5; ++i) {
        const int cx = track.left() + track.width() * i / 4;
        const QString t = QLatin1String(labels[i]);
        const int w = textWidth(f, t) + 2;
        int x = cx - w / 2;
        if (i == 0)
            x = track.left();
        if (i == 4)
            x = track.right() + 1 - w;
        drawText(p, QRect(x, labelTop, w, labelH), Qt::AlignCenter, t, f, s_.palette.ink);
    }
}

// ---- HourHistogram ----

HourHistogram::HourHistogram(const Style &style, const QVector<qint64> &secondsByHour,
                             QWidget *parent)
    : StatsWidget(style, parent), seconds_(secondsByHour)
{
    fixHeightMm(metrics::kHistogramMm + metrics::kHistogramLabelsMm);
}

QRect HourHistogram::barRect(int hour) const
{
    const int m = s_.margin();
    const int gap = std::max(1, s_.px(metrics::kHistogramGapMm));
    const int n = 24;
    const int bw = std::max(1, (width() - 2 * m - (n - 1) * gap) / n);
    const int chartH = s_.px(metrics::kHistogramMm);
    qint64 maxV = 0;
    for (qint64 v : seconds_)
        maxV = std::max(maxV, v);
    const qint64 v = hour < seconds_.size() ? seconds_.at(hour) : 0;
    int bh = 0;
    if (v > 0 && maxV > 0)
        bh = std::max(s_.px(0.5), int(double(chartH) * double(v) / double(maxV) + 0.5));
    else
        bh = std::max(1, s_.px(0.2)); // Hairline for an hour with no reading.
    const int total = n * bw + (n - 1) * gap;
    const int x0 = m + (width() - 2 * m - total) / 2;
    return QRect(x0 + hour * (bw + gap), chartH - bh, bw, bh);
}

void HourHistogram::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.setRenderHint(QPainter::Antialiasing, false);
    fillPaper(p);
    for (int h = 0; h < 24; ++h) {
        const bool any = h < seconds_.size() && seconds_.at(h) > 0;
        p.fillRect(barRect(h), any ? s_.palette.accent : s_.palette.rule);
    }
    const QFont f = s_.small();
    const int labelTop = s_.px(metrics::kHistogramMm) + s_.px(0.8);
    const int labelH = QFontMetrics(f).height();
    const char *const labels[5] = { "00", "06", "12", "18", "24" };
    const int hours[5] = { 0, 6, 12, 18, 24 };
    for (int i = 0; i < 5; ++i) {
        const QString t = QLatin1String(labels[i]);
        const int w = textWidth(f, t) + 2;
        int x = 0;
        if (i == 4) {
            x = barRect(23).right() + 1 - w;
        } else {
            x = barRect(hours[i]).left();
        }
        drawText(p, QRect(x, labelTop, w, labelH), Qt::AlignLeft | Qt::AlignVCenter, t, f,
                 s_.palette.ink);
    }
}

// ---- WeekdayBars ----

WeekdayBars::WeekdayBars(const Style &style, const QVector<double> &minutes, QWidget *parent)
    : StatsWidget(style, parent), minutes_(minutes)
{
    fixHeightMm(metrics::kWeekdayBarsMm + metrics::kHistogramLabelsMm);
}

QRect WeekdayBars::barRect(int index) const
{
    const int m = s_.margin();
    const int gap = s_.px(metrics::kWeekdayGapMm);
    const int n = 7;
    const int bw = std::max(1, (width() - 2 * m - (n - 1) * gap) / n);
    const int chartH = s_.px(metrics::kWeekdayBarsMm);
    double maxV = 0;
    for (double v : minutes_)
        maxV = std::max(maxV, v);
    const double v = index < minutes_.size() ? minutes_.at(index) : 0;
    int bh = std::max(1, s_.px(0.2));
    if (v > 0 && maxV > 0)
        bh = std::max(s_.px(0.5), int(double(chartH) * v / maxV + 0.5));
    const int total = n * bw + (n - 1) * gap;
    const int x0 = m + (width() - 2 * m - total) / 2;
    return QRect(x0 + index * (bw + gap), chartH - bh, bw, bh);
}

void WeekdayBars::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.setRenderHint(QPainter::Antialiasing, false);
    fillPaper(p);
    const QFont f = s_.small();
    const int labelTop = s_.px(metrics::kWeekdayBarsMm) + s_.px(0.8);
    const int labelH = QFontMetrics(f).height();
    const char *const initials[7] = { "M", "T", "W", "T", "F", "S", "S" };
    for (int i = 0; i < 7; ++i) {
        const QRect r = barRect(i);
        const bool any = i < minutes_.size() && minutes_.at(i) > 0;
        p.fillRect(r, any ? s_.palette.accent : s_.palette.rule);
        drawText(p, QRect(r.left(), labelTop, r.width(), labelH), Qt::AlignCenter,
                 QLatin1String(initials[i]), f, s_.palette.ink);
    }
}

// ---- DayStrip ----

DayStrip::DayStrip(const Style &style, const QVector<int> &minutesPerDay, QWidget *parent)
    : StatsWidget(style, parent), minutes_(minutesPerDay)
{
    fixHeightPx(s_.px(metrics::kDayCellMm) + s_.px(2.0));
}

int DayStrip::maxCells(const Style &style)
{
    const int cell = style.px(metrics::kDayCellMm);
    const int gap = std::max(1, style.px(metrics::kDayGapMm));
    const int avail = style.profile.widthPx - 2 * style.margin();
    return std::max(1, (avail + gap) / (cell + gap));
}

void DayStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    setupPainter(p);
    p.setRenderHint(QPainter::Antialiasing, false);
    fillPaper(p);
    const int cell = s_.px(metrics::kDayCellMm);
    const int gap = std::max(1, s_.px(metrics::kDayGapMm));
    int x = s_.margin();
    const int y = s_.px(1.0);
    for (int i = 0; i < minutes_.size(); ++i) {
        p.fillRect(QRect(x, y, cell, cell), levelColour(s_, minutes_.at(i)));
        x += cell + gap;
    }
}

} // namespace stats
} // namespace ui
} // namespace smalt
