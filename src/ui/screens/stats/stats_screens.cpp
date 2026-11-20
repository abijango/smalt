#include "stats_screens.h"

#include "widgets/blocks.h"
#include "widgets/charts.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace smalt {
namespace ui {
namespace stats {

namespace {

namespace ss = smalt::stats;

// A column of widgets, no margins or gaps, paper background.
QWidget *newColumn(const Style &style, QWidget *parent, QVBoxLayout **layoutOut)
{
    QWidget *w = new QWidget(parent);
    w->setAutoFillBackground(true);
    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, style.palette.paper);
    w->setPalette(pal);
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    *layoutOut = l;
    return w;
}

// Every widget this file adds has a fixed height.
int fixedHeight(const QWidget *w)
{
    return w->minimumHeight() == w->maximumHeight() ? w->minimumHeight() : w->sizeHint().height();
}

QString plural(int n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

// Finished-this-year covers in rows of however many fit.
QWidget *coverGrid(ComponentFactory *kit, const Style &style, const QVector<DashboardBook> &books,
                   const QHash<qint64, QPixmap> &covers, QWidget *parent)
{
    QWidget *grid = new QWidget(parent);
    grid->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int w = style.px(metrics::kCoverGridMm);
    const int gap = style.px(metrics::kCoverGridGapMm);
    const int avail = style.profile.widthPx - 2 * style.margin();
    const int columns = std::max(1, (avail + gap) / (w + gap));
    const int rows = (books.size() + columns - 1) / columns;
    const int coverH = int(w * 1.5 + 0.5);
    QGridLayout *l = new QGridLayout(grid);
    l->setContentsMargins(style.margin(), style.px(1.0), style.margin(), style.px(1.0));
    l->setHorizontalSpacing(gap);
    l->setVerticalSpacing(gap);
    for (int i = 0; i < books.size(); ++i) {
        CoverSpec spec;
        spec.title = books.at(i).title;
        spec.pixmap = covers.value(books.at(i).id);
        QWidget *c = kit->cover(spec, metrics::kCoverGridMm, grid);
        l->addWidget(c, i / columns, i % columns, Qt::AlignLeft | Qt::AlignTop);
    }
    // Keep the columns left-aligned when a row is short.
    l->setColumnStretch(columns, 1);
    grid->setFixedHeight(rows * coverH + std::max(0, rows - 1) * gap + 2 * style.px(1.0));
    return grid;
}

QWidget *yearAgoRow(ComponentFactory *kit, const Style &style, const DashboardYearAgo &y,
                    const QHash<qint64, QPixmap> &covers, QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout *l = new QHBoxLayout(row);
    l->setContentsMargins(style.margin(), style.px(1.0), 0, style.px(1.0));
    l->setSpacing(style.px(3.0));
    CoverSpec spec;
    spec.title = y.book.title;
    spec.pixmap = covers.value(y.book.id);
    l->addWidget(kit->cover(spec, metrics::kYearAgoCoverMm, row), 0, Qt::AlignTop);

    QStringList parts;
    if (y.hasDays)
        parts << plural(y.days, QStringLiteral("day"), QStringLiteral("days"));
    if (y.hasMinutes)
        parts << ss::formatDuration(qint64(y.minutes) * 60);
    if (y.hasEndProgress)
        parts << QStringLiteral("stopped at %1%").arg(int(y.endProgress + 0.5));
    QList<TextLines::Line> lines;
    TextLines::Line a;
    a.text = QStringLiteral("You were reading %1").arg(y.book.title);
    a.bold = true;
    lines.append(a);
    if (!parts.isEmpty()) {
        TextLines::Line b;
        b.text = parts.join(QStringLiteral(" · "));
        b.secondary = true;
        b.small = true;
        lines.append(b);
    }
    TextLines *text = new TextLines(style, lines, row);
    text->setSideBySide(true);
    text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    l->addWidget(text, 1, Qt::AlignVCenter);
    row->setFixedHeight(
            std::max(int(style.px(metrics::kYearAgoCoverMm) * 1.5 + 0.5), text->minimumHeight())
            + 2 * style.px(1.0));
    return row;
}

} // namespace

QStringList chipNames()
{
    return QStringList() << QStringLiteral("Overview") << QStringLiteral("Patterns")
                         << QStringLiteral("Year");
}

QWidget *buildOverview(ComponentFactory *kit, const ss::StatsSnapshot &s, QWidget *parent)
{
    const Style style(*kit);
    QVBoxLayout *l = nullptr;
    QWidget *col = newColumn(style, parent, &l);

    if (s.showHeatmap) {
        l->addWidget(new ScopeTitle(style, QStringLiteral("Last %1 weeks").arg(s.heatmapWeeks),
                                    scopeAllDevices(), QString(), col));
        l->addWidget(new HeatmapWidget(style, s.heatmap, col));
    }

    if (s.showStreak) {
        QString big;
        if (s.hasStreakCurrent)
            big = plural(s.streakCurrent, QStringLiteral("day"), QStringLiteral("days"));
        QString caption = QStringLiteral("Streak");
        if (s.hasStreakLongest)
            caption += QStringLiteral(" · longest %1").arg(s.streakLongest);
        l->addWidget(new RuleLine(style, col));
        l->addWidget(new StreakWidget(style, big, caption, s.streakDots, s.streakInitials, col));
    }

    if (s.showGoal) {
        l->addWidget(new RuleLine(style, col));
        l->addWidget(new GoalWidget(style, s.goalTitle, s.goalPace, s.goalFraction, col));
    }

    if (s.showTiles) {
        const QString suffix = s.tilesFromThisKoboOnly ? QStringLiteral(" (this Kobo)") : QString();
        QVector<Tile> tiles;
        Tile today;
        today.value = ss::formatDuration(qint64(s.todayMinutes) * 60);
        today.caption = QStringLiteral("Today") + suffix;
        Tile week;
        week.value = ss::formatDuration(qint64(s.weekMinutes) * 60);
        week.caption = QStringLiteral("This week") + suffix;
        tiles << today << week;
        l->addWidget(new TilesWidget(style, tiles, col));
    }

    l->addWidget(new ScopeTitle(style, QStringLiteral("Today"), scopeThisKobo(), QString(), col));
    l->addWidget(new SessionStrip(style, s.todayBlocks, col));
    l->addStretch(1);
    return col;
}

QWidget *buildPatterns(ComponentFactory *kit, const ss::StatsSnapshot &s, QWidget *parent)
{
    const Style style(*kit);
    QVBoxLayout *l = nullptr;
    QWidget *col = newColumn(style, parent, &l);

    bool anyHour = false;
    for (qint64 v : s.secondsByHour)
        anyHour = anyHour || v > 0;

    l->addWidget(new ScopeTitle(style, QStringLiteral("Time of day"), scopeThisKobo(),
                                s.hourSummary, col));
    if (anyHour) {
        l->addWidget(new HourHistogram(style, s.secondsByHour, col));
        l->addWidget(new ScopeTitle(style, QStringLiteral("Day of week (avg min)"), QString(),
                                    QString(), col));
        l->addWidget(new WeekdayBars(style, s.weekdayAverages, col));
    } else {
        l->addWidget(
                new NoteWidget(style, QStringLiteral("No reading recorded on this Kobo yet"), col));
    }

    QVector<Row> rows;
    const ss::Records &r = s.records;
    if (r.hasLongestSession)
        rows.append({ QStringLiteral("Longest session"),
                      ss::formatDuration(r.longestSessionSeconds),
                      ss::formatShortDate(r.longestSessionDate) });
    if (r.hasBestDay)
        rows.append({ QStringLiteral("Best day"), ss::formatDuration(r.bestDaySeconds),
                      ss::formatShortDate(r.bestDayDate) });
    if (s.pace30.valid)
        rows.append({ QStringLiteral("Reading speed"),
                      QStringLiteral("%1 pages/h").arg(int(s.pace30.pagesPerHour + 0.5)),
                      QStringLiteral("last 30 days") });
    if (r.sessionCount > 0)
        rows.append(
                { QStringLiteral("Average session"), ss::formatDuration(r.averageSessionSeconds),
                  plural(r.sessionCount, QStringLiteral("session"), QStringLiteral("sessions")) });
    if (!rows.isEmpty()) {
        l->addWidget(new ScopeTitle(style, QStringLiteral("Records"), QString(), QString(), col));
        l->addWidget(new RowsWidget(style, RowsWidget::Records, rows, col));
    }
    l->addStretch(1);
    return col;
}

QWidget *buildYear(ComponentFactory *kit, const ss::StatsSnapshot &s,
                   const QHash<qint64, QPixmap> &covers, int remainingPx, QWidget *parent)
{
    const Style style(*kit);
    QVBoxLayout *l = nullptr;
    QWidget *col = newColumn(style, parent, &l);
    bool any = false;
    // The screen is all devices: its label sits on the first title only.
    bool labelled = false;
    auto scope = [&]() {
        if (labelled)
            return QString();
        labelled = true;
        return scopeAllDevices();
    };

    if (s.showFinished) {
        any = true;
        QStringList summary;
        summary << plural(s.finishedCount, QStringLiteral("book"), QStringLiteral("books"));
        if (s.hasYearSeconds)
            summary << ss::formatHours(s.yearSeconds);
        l->addWidget(new ScopeTitle(style, QStringLiteral("Finished in %1").arg(s.finishedYear),
                                    scope(), summary.join(QStringLiteral(" · ")), col));
        if (s.finishedBooks.isEmpty())
            l->addWidget(
                    new NoteWidget(style, QStringLiteral("No books finished yet this year"), col));
        else
            l->addWidget(coverGrid(kit, style, s.finishedBooks, covers, col));
    }

    if (s.showYearAgo) {
        any = true;
        l->addWidget(new ScopeTitle(style, QStringLiteral("A year ago"), scope(), QString(), col));
        l->addWidget(yearAgoRow(kit, style, s.yearAgo, covers, col));
    }

    if (s.showBadge) {
        any = true;
        l->addWidget(new ScopeTitle(style, QStringLiteral("Next badge"), scope(), QString(), col));
        l->addWidget(new BadgeRow(style, s.badge.name, ss::badgeToGo(s.badge),
                                  ss::badgeFraction(s.badge), col));
    }

    if (s.showHighlight) {
        const bool wasLabelled = labelled;
        ScopeTitle *title = new ScopeTitle(style, QStringLiteral("Highlight of the day"), scope(),
                                           QString(), col);
        const QString source = s.highlight.bookTitle.isEmpty()
                ? QString()
                : QStringLiteral("— %1").arg(s.highlight.bookTitle);
        QuoteWidget *quote = new QuoteWidget(style, s.highlight.text, source, 3, col);
        int used = 0;
        for (int i = 0; i < l->count(); ++i)
            if (l->itemAt(i)->widget())
                used += fixedHeight(l->itemAt(i)->widget());
        if (remainingPx <= 0 || used + fixedHeight(title) + fixedHeight(quote) <= remainingPx) {
            any = true;
            l->addWidget(title);
            l->addWidget(quote);
        } else {
            delete title;
            delete quote;
            labelled = wasLabelled;
        }
    }

    if (!any)
        l->addWidget(new NoteWidget(
                style, QStringLiteral("Connect to BookOrbit once to see your year"), col));
    l->addStretch(1);
    return col;
}

QWidget *buildYourReading(ComponentFactory *kit, const ss::BookSnapshot &b, QWidget *parent)
{
    const Style style(*kit);
    QVBoxLayout *l = nullptr;
    QWidget *col = newColumn(style, parent, &l);

    if (b.forecast.state != ss::Forecast::Finished) {
        l->addWidget(new ForecastBox(style, ss::forecastHeadline(b.forecast),
                                     ss::forecastDetail(b.forecast, b.today), col));
    }

    // The days you read the book, as many as fit.
    const int cells = DayStrip::maxCells(style);
    QDate first = b.stats.firstDay;
    QString since;
    if (first.isValid()) {
        const QDate limit = b.today.addDays(-(cells - 1));
        if (first < limit) {
            first = limit;
            since = QStringLiteral("last %1 days").arg(cells);
        } else {
            since = QStringLiteral("since %1").arg(ss::formatShortDate(first));
        }
    }
    l->addWidget(
            new ScopeTitle(style, QStringLiteral("Your reading"), scopeThisKobo(), since, col));
    if (first.isValid()) {
        QVector<int> days;
        for (QDate d = first; d <= b.today; d = d.addDays(1))
            days.append(b.minutesPerDay.value(d, 0));
        l->addWidget(new DayStrip(style, days, col));
    }

    QVector<Tile> tiles;
    tiles.append({ ss::formatDuration(b.stats.totalSeconds), QStringLiteral("Time read") });
    tiles.append({ QString::number(b.stats.sessionCount), QStringLiteral("Sessions") });
    tiles.append({ b.stats.pace.valid ? QString::number(int(b.stats.pace.pagesPerHour + 0.5))
                                      : QStringLiteral("–"),
                   QStringLiteral("Pages per hour") });
    tiles.append({ b.stats.sessionCount > 0 ? ss::formatDuration(b.stats.averageSessionSeconds)
                                            : QStringLiteral("–"),
                   QStringLiteral("Average session") });
    l->addWidget(new TilesWidget(style, tiles, col));

    if (!b.latest.isEmpty()) {
        l->addWidget(new ScopeTitle(style, QStringLiteral("Sessions"), QString(), QString(), col));
        QVector<Row> rows;
        for (const ss::SessionRow &r : b.latest)
            rows.append({ r.day, r.range, r.duration });
        l->addWidget(new RowsWidget(style, RowsWidget::Sessions, rows, col));
    }
    return col;
}

} // namespace stats
} // namespace ui
} // namespace smalt
