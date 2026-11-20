// Stats chart widgets: heatmap, legend, streak dots, 24-hour session strip,
// hour histogram, weekday bars and the book's day strip. Each paints itself
// from the kit's tokens (palette steps, accent, ink) with QPainter and never
// updates on its own (Stats "Rules": nothing updates live).
//
// Spec: Stats ("Screens", "Rules"), Visual design ("Monochrome palette"). The
// bars, blocks and progress use the accent (teal; ink on the Clara BW);
// numbers and labels stay ink.
#pragma once

#include "stats/aggregate.h"
#include "style.h"

#include <QStringList>
#include <QVector>

namespace smalt {
namespace ui {
namespace stats {

// Heatmap grid (26 or 52 weeks, Monday at the top, today's week last) with the
// five-step legend under it. Cell colours come from Palette::heatmap.
class HeatmapWidget : public StatsWidget
{
    Q_OBJECT

public:
    HeatmapWidget(const Style &style, const smalt::stats::HeatmapGrid &grid, QWidget *parent);

    // Pixel size of a cell for a given content width, shared with the tests.
    static int cellSize(const Style &style, int weeks, int widthPx);

    int cellSizePx() const { return cellSizeFor(width()); }

    QRect cellRect(int week, int row) const;

    const smalt::stats::HeatmapGrid &grid() const { return grid_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int cellSizeFor(int widthPx) const;
    int gapPx() const;

    smalt::stats::HeatmapGrid grid_;
};

// Seven dots with weekday initials: filled = read that day (accent), ring =
// not read. Today is the last dot.
class StreakWidget : public StatsWidget
{
    Q_OBJECT

public:
    StreakWidget(const Style &style, const QString &big, const QString &caption,
                 const QVector<bool> &dots, const QVector<QString> &initials, QWidget *parent);

    QString big() const { return big_; }

    QString caption() const { return caption_; }

    const QVector<bool> &dots() const { return dots_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString big_;
    QString caption_;
    QVector<bool> dots_;
    QVector<QString> initials_;
};

// Today's reading as blocks on a 24-hour strip, labelled 00 06 12 18 24.
class SessionStrip : public StatsWidget
{
    Q_OBJECT

public:
    SessionStrip(const Style &style, const QVector<smalt::stats::TimeBlock> &blocks,
                 QWidget *parent);

    // Block rectangle in widget coordinates (for tests).
    QRect blockRect(int index) const;

    int blockCount() const { return blocks_.size(); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QRect trackRect() const;

    QVector<smalt::stats::TimeBlock> blocks_;
};

// Minutes by hour of day: 24 bars, labels 00 06 12 18 24.
class HourHistogram : public StatsWidget
{
    Q_OBJECT

public:
    HourHistogram(const Style &style, const QVector<qint64> &secondsByHour, QWidget *parent);

    QRect barRect(int hour) const; // Zero hours are a hairline.

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<qint64> seconds_;
};

// Average minutes by day of the week: 7 bars with initials.
class WeekdayBars : public StatsWidget
{
    Q_OBJECT

public:
    WeekdayBars(const Style &style, const QVector<double> &minutes, QWidget *parent);

    QRect barRect(int index) const;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<double> minutes_;
};

// The days you read one book: one square per day, heatmap steps, oldest first.
class DayStrip : public StatsWidget
{
    Q_OBJECT

public:
    DayStrip(const Style &style, const QVector<int> &minutesPerDay, QWidget *parent);

    int cellCount() const { return minutes_.size(); }

    // How many squares fit the device's content width.
    static int maxCells(const Style &style);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<int> minutes_;
};

} // namespace stats
} // namespace ui
} // namespace smalt
