// Stats text blocks: section titles with their scope label, tiles, rows,
// goal, badge, forecast box and the small helpers between them. Painted from
// kit tokens like the charts (charts.h); no widget here is tappable.
#pragma once

#include "style.h"

#include <QList>
#include <QPair>
#include <QStringList>
#include <QVector>

namespace smalt {
namespace ui {
namespace stats {

// "All devices" / "This Kobo" (Stats: every section carries a small label).
QString scopeAllDevices();
QString scopeThisKobo();

// A section title with the scope label at the right and an optional secondary
// line below it ("Mostly 21:00-23:00").
class ScopeTitle : public StatsWidget
{
    Q_OBJECT

public:
    ScopeTitle(const Style &style, const QString &title, const QString &scope, const QString &aside,
               QWidget *parent);

    QString title() const { return title_; }

    QString scope() const { return scope_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString title_;
    QString scope_;
    QString aside_;
};

// A Rule-coloured hairline across the content.
class RuleLine : public StatsWidget
{
    Q_OBJECT

public:
    RuleLine(const Style &style, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;
};

// A value over a caption, in two columns separated by rules ("1 h 46 m" /
// "Today").
struct Tile
{
    QString value;
    QString caption;
};

class TilesWidget : public StatsWidget
{
    Q_OBJECT

public:
    TilesWidget(const Style &style, const QVector<Tile> &tiles, QWidget *parent);

    const QVector<Tile> &tiles() const { return tiles_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<Tile> tiles_;
};

// "18 of 30 books" with "2026 goal · 2 behind" at the right, and the progress
// bar under them.
class GoalWidget : public StatsWidget
{
    Q_OBJECT

public:
    GoalWidget(const Style &style, const QString &title, const QString &pace, double fraction,
               QWidget *parent);

    QString title() const { return title_; }

    QString pace() const { return pace_; }

    double fraction() const { return fraction_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString title_;
    QString pace_;
    double fraction_;
};

// Rows of three cells separated by rules. Records: label, bold value, secondary
// note. Sessions: day, time range, bold duration.
struct Row
{
    QString first;
    QString second;
    QString third;
};

class RowsWidget : public StatsWidget
{
    Q_OBJECT

public:
    enum Mode { Records, Sessions };

    RowsWidget(const Style &style, Mode mode, const QVector<Row> &rows, QWidget *parent);

    const QVector<Row> &rows() const { return rows_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Mode mode_;
    QVector<Row> rows_;
};

// A line or two of text with a fixed font and colour (Secondary or Ink).
class TextLines : public StatsWidget
{
    Q_OBJECT

public:
    struct Line
    {
        QString text;
        bool bold = false;
        bool secondary = false;
        bool small = false;
    };

    TextLines(const Style &style, const QList<Line> &lines, QWidget *parent);

    // A text-only widget with no margin, so it can sit beside a cover.
    void setSideBySide(bool on);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<Line> lines_;
    bool sideBySide_ = false;
};

// The next badge: a star in a circle, its name, a progress bar and "38 h to go".
class BadgeRow : public StatsWidget
{
    Q_OBJECT

public:
    BadgeRow(const Style &style, const QString &name, const QString &toGo, double fraction,
             QWidget *parent);

    QString name() const { return name_; }

    QString toGo() const { return toGo_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString name_;
    QString toGo_;
    double fraction_;
};

// A quote wrapped to at most `maxLines` lines, the last one cut with an
// ellipsis, with its book title under it.
class QuoteWidget : public StatsWidget
{
    Q_OBJECT

public:
    QuoteWidget(const Style &style, const QString &text, const QString &source, int maxLines,
                QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QStringList lines_;
    QString source_;
};

// A line of secondary text: empty states ("No reading recorded on this Kobo yet").
class NoteWidget : public StatsWidget
{
    Q_OBJECT

public:
    NoteWidget(const Style &style, const QString &text, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
};

// The finish forecast: a 2 px ink box, headline over a secondary line.
class ForecastBox : public StatsWidget
{
    Q_OBJECT

public:
    ForecastBox(const Style &style, const QString &headline, const QString &detail,
                QWidget *parent);

    QString headline() const { return headline_; }

    QString detail() const { return detail_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString headline_;
    QString detail_;
};

// Word-wraps `text` into at most `maxLines` lines of `width` pixels; the last
// line is elided when the text does not fit.
QStringList wrapLines(const QFont &font, const QString &text, int width, int maxLines);

} // namespace stats
} // namespace ui
} // namespace smalt
