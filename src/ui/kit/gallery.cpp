#include "gallery.h"

#include <QPainter>

namespace smalt {
namespace ui {
namespace kit {
namespace gallery {

using namespace metrics;

namespace {

QWidget *newPage(int width, int height)
{
    QWidget *page = new QWidget;
    page->setAttribute(Qt::WA_DontShowOnScreen, false);
    page->setAutoFillBackground(true);
    QPalette pal = page->palette();
    pal.setColor(QPalette::Window, Qt::white);
    page->setPalette(pal);
    page->resize(width, height);
    return page;
}

// Puts `w` at (x, y), stretched to `width` if width > 0, and returns its bottom.
int place(QWidget *w, int x, int y, int width = 0)
{
    const int h = w->height() > 0 && w->minimumHeight() == w->maximumHeight()
            ? w->height()
            : w->sizeHint().height();
    const int ww = width > 0
            ? width
            : (w->minimumWidth() == w->maximumWidth() ? w->width() : w->sizeHint().width());
    w->setGeometry(x, y, ww, h);
    return y + h;
}

QStringList tabNames(const DeviceProfile &)
{
    QStringList tabs;
    tabs << QStringLiteral("Home") << QStringLiteral("Library") << QStringLiteral("Series")
         << QStringLiteral("Stats") << QStringLiteral("More");
    return tabs;
}

CoverSpec cover(int seed, const QString &title, const QString &format = QString(),
                double progress = -1)
{
    CoverSpec c;
    c.pixmap = fakeCover(seed);
    c.title = title;
    c.format = format;
    c.progress = progress;
    return c;
}

} // namespace

QStringList sampleNames()
{
    QStringList names;
    names << QStringLiteral("header_bar") << QStringLiteral("status_line")
          << QStringLiteral("section_header") << QStringLiteral("primary_button")
          << QStringLiteral("accent_button") << QStringLiteral("outline_button")
          << QStringLiteral("chip_row") << QStringLiteral("bottom_tabs") << QStringLiteral("pager")
          << QStringLiteral("letter_grid") << QStringLiteral("search_field")
          << QStringLiteral("banner") << QStringLiteral("cover") << QStringLiteral("cover_row")
          << QStringLiteral("list_row_download") << QStringLiteral("list_row_downloading")
          << QStringLiteral("list_row_on_device") << QStringLiteral("download_badges")
          << QStringLiteral("segmented_progress") << QStringLiteral("cover_stack")
          << QStringLiteral("dialog");
    return names;
}

QPixmap fakeCover(int seed)
{
    QImage img(200, 300, QImage::Format_RGB32);
    QPainter p(&img);
    const int hue = (seed * 67 + 20) % 360;
    QLinearGradient g(0, 0, 0, 300);
    g.setColorAt(0, QColor::fromHsv(hue, 170, 190));
    g.setColorAt(1, QColor::fromHsv((hue + 40) % 360, 200, 110));
    p.fillRect(img.rect(), g);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 190));
    p.drawRect(QRect(24, 40 + (seed % 3) * 14, 152, 26));
    p.drawRect(QRect(24, 78 + (seed % 3) * 14, 100, 14));
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(QPoint(100, 200), 40 + (seed % 4) * 6, 40 + (seed % 4) * 6);
    p.end();
    return QPixmap::fromImage(img);
}

QWidget *buildSample(const QString &name, KitFactory &f, int width)
{
    const Theme &t = f.theme();
    const int margin = t.px(kPageMarginMm);
    const int gap = t.px(4.0);
    QWidget *page = newPage(width, 10);
    int bottom = 0;

    if (name == QLatin1String("header_bar")) {
        bottom = place(f.headerBar(QStringLiteral("Library"), page), 0, 0, width);
    } else if (name == QLatin1String("status_line")) {
        bottom = place(f.statusLine(QStringLiteral("Synced 10:42 · 3 waiting"), page), 0, 0, width);
    } else if (name == QLatin1String("section_header")) {
        bottom = place(f.sectionHeader(QStringLiteral("Reading now"), 3, page), 0, 0, width);
    } else if (name == QLatin1String("primary_button")) {
        bottom = place(f.primaryButton(QStringLiteral("Continue"), page), margin, gap) + gap;
    } else if (name == QLatin1String("accent_button")) {
        bottom = place(f.accentButton(QStringLiteral("Download"), page), margin, gap) + gap;
    } else if (name == QLatin1String("outline_button")) {
        bottom = place(f.outlineButton(QStringLiteral("Stay here"), page), margin, gap) + gap;
    } else if (name == QLatin1String("chip_row")) {
        ChipRow *chips = static_cast<ChipRow *>(
                f.chipRow(QStringList() << QStringLiteral("Books") << QStringLiteral("Comics")
                                        << QStringLiteral("PDFs") << QStringLiteral("On Kobo")
                                        << QStringLiteral("Sort"),
                          page));
        chips->setSelected(QList<int>() << 1);
        bottom = place(chips, 0, 0, width);
    } else if (name == QLatin1String("bottom_tabs")) {
        bottom = place(f.bottomTabs(tabNames(f.profile()), 1, page), 0, 0, width);
    } else if (name == QLatin1String("pager")) {
        bottom = place(f.pager(1, 54, page), 0, 0, width);
    } else if (name == QLatin1String("letter_grid")) {
        bottom = place(f.letterGrid(page), margin, gap) + gap;
    } else if (name == QLatin1String("search_field")) {
        bottom = place(f.searchField(page), 0, 0, width);
    } else if (name == QLatin1String("banner")) {
        bottom = place(f.banner(QStringLiteral("3 books were removed from BookOrbit."),
                                QStringLiteral("Review"), page),
                       0, 0, width);
    } else if (name == QLatin1String("cover")) {
        int x = margin;
        int h = 0;
        QWidget *a = f.cover(cover(1, QStringLiteral("A"), QString(), 0.42), 21.0, page);
        QWidget *b = f.cover(cover(2, QStringLiteral("B"), QStringLiteral("CBZ")), 21.0, page);
        QWidget *c = f.cover(CoverSpec(), 21.0, page);
        const QList<QWidget *> covers = QList<QWidget *>() << a << b << c;
        for (int i = 0; i < covers.size(); ++i) {
            h = place(covers.at(i), x, gap);
            x += covers.at(i)->width() + t.px(kCoverGapMm);
        }
        bottom = h + gap;
    } else if (name == QLatin1String("cover_row")) {
        QList<CoverSpec> covers;
        covers << cover(3, QStringLiteral("The Left Hand of Darkness"), QString(), 0.7)
               << cover(4, QStringLiteral("Saga, Vol. 1"), QStringLiteral("CBZ"), 0.15)
               << cover(5, QStringLiteral("Field Guide to Birds"), QStringLiteral("PDF"), 0.5)
               << cover(6, QStringLiteral("Dune"), QString(), 0.9);
        bottom = place(f.coverRow(covers, page), 0, gap, width) + gap;
    } else if (name.startsWith(QLatin1String("list_row_"))) {
        ListRowSpec spec;
        spec.cover = cover(7, QString(), QString(), -1);
        spec.title = QStringLiteral("Piranesi");
        spec.subtitle = QStringLiteral("Susanna Clarke · Book 1");
        if (name == QLatin1String("list_row_download")) {
            spec.badge = BadgeState::Download;
        } else if (name == QLatin1String("list_row_downloading")) {
            spec.badge = BadgeState::Percent;
            spec.badgeText = QStringLiteral("42%");
        } else {
            spec.badge = BadgeState::OnDevice;
            spec.cover.progress = 0.35;
        }
        bottom = place(f.listRow(spec, page), 0, 0, width);
    } else if (name == QLatin1String("download_badges")) {
        int x = margin;
        int h = 0;
        const QList<QWidget *> badges = QList<QWidget *>()
                << f.downloadBadge(BadgeState::Download, QString(), page)
                << f.downloadBadge(BadgeState::Percent, QStringLiteral("42%"), page)
                << f.downloadBadge(BadgeState::OnDevice, QString(), page);
        for (int i = 0; i < badges.size(); ++i) {
            h = place(badges.at(i), x, gap);
            x += badges.at(i)->width() + gap;
        }
        bottom = h + gap;
    } else if (name == QLatin1String("segmented_progress")) {
        bottom = place(f.segmentedProgress(8, 3, page), margin, gap, width - 2 * margin) + gap;
    } else if (name == QLatin1String("cover_stack")) {
        bottom = place(f.coverStack(cover(8, QStringLiteral("Series")), page), margin, gap) + gap;
    } else if (name == QLatin1String("dialog")) {
        page->resize(f.profile().widthPx, f.profile().heightPx);
        // Something behind the dialog, so the dimming shows.
        place(f.headerBar(QStringLiteral("Book"), page), 0, 0, page->width());
        place(f.primaryButton(QStringLiteral("Open"), page), margin, t.px(18));
        QWidget *d = f.dialog(QStringLiteral("Your phone is at 54% (Chapter 12), 2 hours ago. "
                                             "You're at 41% here."),
                              QStringList() << QStringLiteral("Jump to 54%")
                                            << QStringLiteral("Stay here"),
                              page);
        d->setGeometry(page->rect());
        return page;
    }
    page->resize(width, bottom);
    return page;
}

QWidget *buildHomeFrame(KitFactory &f)
{
    const DeviceProfile &prof = f.profile();
    const Theme &t = f.theme();
    const int w = prof.widthPx;
    QWidget *page = newPage(w, prof.heightPx);
    int y = 0;
    y = place(f.headerBar(QStringLiteral("Home"), page), 0, y, w);
    y = place(f.statusLine(QStringLiteral("Synced 10:42 · 3 waiting"), page), 0, y, w);
    y = place(f.sectionHeader(QStringLiteral("Reading now"), 5, page), 0, y, w);
    QList<CoverSpec> covers;
    covers << cover(3, QStringLiteral("The Left Hand of Darkness"), QString(), 0.7)
           << cover(4, QStringLiteral("Saga, Vol. 1"), QStringLiteral("CBZ"), 0.15)
           << cover(5, QStringLiteral("Field Guide to Birds"), QStringLiteral("PDF"), 0.5)
           << cover(6, QStringLiteral("Dune"), QString(), 0.9);
    y = place(f.coverRow(covers, page), 0, y, w) + t.px(2.0);
    y = place(f.sectionHeader(QStringLiteral("Up next in your series"), 12, page), 0, y, w);
    for (int i = 0; i < prof.upNextRows; ++i) {
        ListRowSpec spec;
        spec.cover = cover(10 + i, QString());
        spec.title = i == 0 ? QStringLiteral("Piranesi") : QStringLiteral("Children of Time");
        spec.subtitle = QStringLiteral("Book %1 in series").arg(i + 2);
        spec.badge = i == 0 ? BadgeState::Download : BadgeState::OnDevice;
        y = place(f.listRow(spec, page), 0, y, w);
    }
    // The tab bar sits at the bottom of the screen, below Nickel's status bar area.
    place(f.bottomTabs(tabNames(prof), 0, page), 0, prof.heightPx - t.px(kTabsHeightMm), w);
    return page;
}

QImage renderToImage(QWidget *widget)
{
    // Showing (offscreen on the desktop) delivers the resize events that lay out
    // the dialog box.
    widget->show();
    QImage image(widget->size(), QImage::Format_RGB32);
    image.fill(Qt::white);
    widget->render(&image);
    return image;
}

} // namespace gallery
} // namespace kit
} // namespace ui
} // namespace smalt
