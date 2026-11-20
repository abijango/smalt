// What every Stats widget paints with: the device profile, the palette and the
// kit's fonts, plus the sizes this task had to choose (the spec gives colours
// and rules, not millimetres; see the task 23 report).
//
// Widgets are plain QWidgets drawn with QPainter, no animations; layout is in
// millimetres through the kit's mmToPx so a 7" screen shows more, not bigger.
#pragma once

#include "kit/theme.h"
#include "smalt/ui_kit.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QWidget>

#include <algorithm>

namespace smalt {
namespace ui {
namespace stats {

// --- Chosen values (NOT in docs/SPEC.md) ---
namespace metrics {
constexpr double kTitleRowMm = 5.5; // Section title row
constexpr double kTitlePadTopMm = 1.0; // Air above a title that follows a chart
constexpr double kAsideRowMm = 5.0; // Secondary line under a title
constexpr double kTagHeightMm = 5.0; // "All devices" / "This Kobo" label
constexpr double kTagPadMm = 1.8;
constexpr double kHeatmapGapMm = 0.35; // Between heatmap cells
constexpr double kLegendRowMm = 4.5;
constexpr double kLegendSwatchMm = 2.8;
constexpr double kStreakRowMm = 10.5;
constexpr double kDotMm = 3.6;
constexpr double kDotPitchMm = 5.8;
constexpr double kBarMm = 1.5; // Progress bars (goal, badge)
constexpr double kTileRowMm = 10.5;
constexpr double kFontValueMm = 4.6; // Big numbers in tiles, streak and goal
constexpr double kStripMm = 8.5; // 24-hour strip including its labels
constexpr double kStripTrackMm = 2.0;
constexpr double kStripBlockMm = 3.6;
constexpr double kStripMinBlockMm = 0.9;
constexpr double kHistogramMm = 10.0; // Bars only
constexpr double kHistogramLabelsMm = 4.5;
constexpr double kHistogramGapMm = 0.5;
constexpr double kWeekdayBarsMm = 7.0;
constexpr double kWeekdayGapMm = 2.2;
constexpr double kRowMm = 6.8; // Records and sessions rows
constexpr double kDayCellMm = 2.4; // Book "days you read" strip
constexpr double kDayGapMm = 0.6;
constexpr double kCoverGridMm = 7.4; // Finished-this-year covers
constexpr double kCoverGridGapMm = 1.6;
constexpr double kYearAgoCoverMm = 7.0;
constexpr double kBadgeIconMm = 7.0;
constexpr double kBoxPadMm = 3.0; // Forecast box
constexpr double kBoxRadiusMm = 1.2;
constexpr int kBoxBorderPx = 2;
constexpr int kRulePx = 1;
} // namespace metrics

struct Style
{
    explicit Style(const ComponentFactory &kit)
        : profile(kit.profile()), palette(kit.palette()), theme(profile, nullptr)
    {
    }

    DeviceProfile profile;
    Palette palette;
    kit::Theme theme;

    int px(double mm) const { return mmToPx(mm, profile); }

    int margin() const { return px(kit::metrics::kPageMarginMm); }

    QFont body() const { return theme.font(kit::FontRole::Body); }

    QFont small() const { return theme.font(kit::FontRole::Small); }

    // Titles in medium weight, body size ("Type").
    QFont titleFont() const
    {
        QFont f = theme.font(kit::FontRole::Body);
        f.setWeight(kit::metrics::kWeightTitle);
        return f;
    }

    // Big numbers: titles' weight at the value size.
    QFont valueFont() const
    {
        QFont f = theme.font(kit::FontRole::Title);
        f.setPixelSize(px(metrics::kFontValueMm));
        return f;
    }

    // Bold figures in rows ("2 h 14 m").
    QFont boldBody() const
    {
        QFont f = theme.font(kit::FontRole::Body);
        f.setWeight(kit::metrics::kWeightTitle);
        return f;
    }
};

inline int textWidth(const QFont &font, const QString &text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return QFontMetrics(font).horizontalAdvance(text);
#else
    return QFontMetrics(font).width(text); // The Kobo's Qt 5.2 has no horizontalAdvance().
#endif
}

inline void setupPainter(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
}

inline void drawText(QPainter &p, const QRect &r, int flags, const QString &text, const QFont &font,
                     const QColor &colour)
{
    p.save();
    p.setFont(font);
    p.setPen(colour);
    const QString shown = QFontMetrics(font).elidedText(text, Qt::ElideRight, r.width());
    p.drawText(r, flags | Qt::TextSingleLine, shown);
    p.restore();
}

// A thin progress bar: Rule-grey track, accent fill (teal on colour, ink on BW).
inline void drawProgress(QPainter &p, const Style &s, const QRect &track, double fraction)
{
    p.fillRect(track, s.palette.heatmap[0]);
    const int w = int(track.width() * std::max(0.0, std::min(1.0, fraction)) + 0.5);
    if (w > 0)
        p.fillRect(QRect(track.left(), track.top(), w, track.height()), s.palette.accent);
}

// Base of the chart and row widgets: fixed height, paper background.
class StatsWidget : public QWidget
{
public:
    StatsWidget(const Style &style, QWidget *parent) : QWidget(parent), s_(style)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    const Style &style() const { return s_; }

protected:
    void fixHeightPx(int h) { setFixedHeight(h); }

    void fixHeightMm(double mm) { setFixedHeight(s_.px(mm)); }

    void fillPaper(QPainter &p) { p.fillRect(rect(), s_.palette.paper); }

    Style s_;
};

} // namespace stats
} // namespace ui
} // namespace smalt
