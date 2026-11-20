// Pixel and behaviour checks for the spec's visual rules and E Ink rules:
// dialog border, rules, teal placement, refresh requests, no animations, taps.
#include "test_support.h"

#include <QAbstractAnimation>
#include <QSignalSpy>
#include <QTimer>

using namespace kittest;

namespace {

int textWidth(const QFont &font, const QString &text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return QFontMetrics(font).horizontalAdvance(text);
#else
    return QFontMetrics(font).width(text); // The Kobo's Qt 5.2 has no horizontalAdvance().
#endif
}

QImage grab(QWidget *w)
{
    return kit::gallery::renderToImage(w);
}

bool sameRgb(QRgb a, const QColor &c)
{
    return qRed(a) == c.red() && qGreen(a) == c.green() && qBlue(a) == c.blue();
}

void click(QWidget *w, const QPoint &pos)
{
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, pos);
}

} // namespace

class TstRules : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void dialogHasATwoPixelInkBorderAndNoShadow_data()
    {
        QTest::addColumn<int>("modelIndex");
        QTest::newRow("clara colour") << 0;
        QTest::newRow("clara bw") << 1;
        QTest::newRow("libra colour") << 2;
    }

    void dialogHasATwoPixelInkBorderAndNoShadow()
    {
        QFETCH(int, modelIndex);
        kit::KitFactory f(allModels().at(modelIndex), nullptr);
        QWidget page;
        page.resize(f.profile().widthPx, f.profile().heightPx);
        kit::Dialog *d = static_cast<kit::Dialog *>(f.dialog(
                QStringLiteral("Your phone is at 54% (Chapter 12). You're at 41% here."),
                QStringList() << QStringLiteral("Jump to 54%") << QStringLiteral("Stay here"),
                &page));
        const QImage img = grab(&page);
        const QRect box = d->boxRect();
        QVERIFY(!box.isEmpty());
        QVERIFY(box.width() <= mmToPx(90.0, 300));
        QVERIFY(box.left() > 0 && box.right() < page.width() - 1);
        // Box is centred.
        QVERIFY(qAbs((box.left() + box.right()) / 2 - page.width() / 2) <= 1);
        QVERIFY(qAbs((box.top() + box.bottom()) / 2 - page.height() / 2) <= 1);

        const QColor ink(QRgb(token::Ink));
        const QColor paper(QRgb(token::Paper));
        const int midY = box.center().y();
        const int midX = box.center().x();
        const int bw = kit::metrics::kDialogBorderPx;
        QCOMPARE(bw, 2);
        // Left and right edges: outside (dimmed), 2 ink pixels, then paper.
        QVERIFY(!sameRgb(img.pixel(box.left() - 1, midY), ink));
        QVERIFY(!sameRgb(img.pixel(box.left() - 1, midY), paper)); // Page behind is dimmed.
        QVERIFY(sameRgb(img.pixel(box.left(), midY), ink));
        QVERIFY(sameRgb(img.pixel(box.left() + 1, midY), ink));
        QVERIFY(sameRgb(img.pixel(box.left() + 2, midY), paper));
        QVERIFY(sameRgb(img.pixel(box.right(), midY), ink));
        QVERIFY(sameRgb(img.pixel(box.right() - 1, midY), ink));
        QVERIFY(sameRgb(img.pixel(box.right() - 2, midY), paper));
        // Top and bottom edges.
        QVERIFY(sameRgb(img.pixel(midX, box.top()), ink));
        QVERIFY(sameRgb(img.pixel(midX, box.top() + 1), ink));
        QVERIFY(sameRgb(img.pixel(midX, box.top() + 2), paper));
        QVERIFY(sameRgb(img.pixel(midX, box.bottom()), ink));
        QVERIFY(sameRgb(img.pixel(midX, box.bottom() - 1), ink));
        QVERIFY(sameRgb(img.pixel(midX, box.bottom() - 2), paper));
        // No shadow: the page just outside the box is one flat dimmed colour.
        const QRgb dimmed = img.pixel(box.left() - 1, midY);
        for (int off = 1; off <= 30; ++off) {
            QCOMPARE(img.pixel(box.left() - off, midY), dimmed);
            QCOMPARE(img.pixel(box.right() + off, midY), dimmed);
            QCOMPARE(img.pixel(midX, box.top() - off), dimmed);
            QCOMPARE(img.pixel(midX, box.bottom() + off), dimmed);
        }
        QVERIFY(isGrey(dimmed));
    }

    void dialogActionsAndModality()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget page;
        page.resize(f.profile().widthPx, f.profile().heightPx);
        kit::Dialog *d = static_cast<kit::Dialog *>(
                f.dialog(QStringLiteral("Sure?"), QStringList() << "Yes" << "No", &page));
        page.show();
        QSignalSpy spy(d, SIGNAL(actionTapped(int)));
        const QList<kit::Button *> buttons = d->findChildren<kit::Button *>();
        QCOMPARE(buttons.size(), 2);
        click(buttons.at(1), QPoint(10, 10));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        // A tap on the dimmed page goes nowhere.
        click(d, QPoint(3, 3));
        QCOMPARE(spy.count(), 1);
    }

    void headerBarHasAnInkRuleBelow()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget parent;
        QWidget *h = f.headerBar(QStringLiteral("Home"), &parent);
        h->resize(f.profile().widthPx, h->height());
        const QImage img = grab(h);
        const QColor ink(QRgb(token::Ink));
        QVERIFY(sameRgb(img.pixel(20, img.height() - 1), ink));
        QVERIFY(sameRgb(img.pixel(20, img.height() - 2), ink));
        QVERIFY(!sameRgb(img.pixel(20, img.height() - 3), ink));
    }

    void listRowsAreSeparatedByAThinGreyRule()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget parent;
        ListRowSpec spec;
        spec.title = QStringLiteral("Title");
        QWidget *row = f.listRow(spec, &parent);
        row->resize(f.profile().widthPx, row->height());
        const QImage img = grab(row);
        QVERIFY(sameRgb(img.pixel(img.width() / 2, img.height() - 1), QColor(QRgb(token::Rule))));
        QVERIFY(sameRgb(img.pixel(img.width() / 2, img.height() - 2), QColor(QRgb(token::Paper))));
    }

    void activeTabUnderlineIsTealOnTheActiveTabOnly()
    {
        for (int m = 0; m < 2; ++m) {
            kit::KitFactory f(m == 0 ? DeviceModel::ClaraColour : DeviceModel::ClaraBW, nullptr);
            QWidget parent;
            kit::BottomTabs *tabs = static_cast<kit::BottomTabs *>(f.bottomTabs(
                    QStringList() << "Home" << "Library" << "Series" << "Stats" << "More", 1,
                    &parent));
            tabs->resize(f.profile().widthPx, tabs->height());
            const QImage img = grab(tabs);
            const int y = img.height() - 2;
            const QColor accent = f.palette().accent;
            QVERIFY(sameRgb(img.pixel(tabs->tabRect(1).center().x(), y), accent));
            QVERIFY(sameRgb(img.pixel(tabs->tabRect(0).center().x(), y),
                            QColor(QRgb(token::Paper))));
            QVERIFY(sameRgb(img.pixel(tabs->tabRect(2).center().x(), y),
                            QColor(QRgb(token::Paper))));
        }
    }

    void accentButtonIsTealOnColourAndBlackOnClaraBW()
    {
        kit::KitFactory colour(DeviceModel::ClaraColour, nullptr);
        kit::KitFactory mono(DeviceModel::ClaraBW, nullptr);
        QWidget pc, pm;
        QWidget *a = colour.accentButton(QStringLiteral("Download"), &pc);
        QWidget *b = mono.accentButton(QStringLiteral("Download"), &pm);
        a->resize(a->sizeHint());
        b->resize(b->sizeHint());
        QVERIFY(sameRgb(grab(a).pixel(a->width() / 2, 6), QColor(QRgb(token::Teal))));
        QVERIFY(sameRgb(grab(b).pixel(b->width() / 2, 6), QColor(QRgb(token::Ink))));
    }

    void primaryButtonIsSolidBlackAndOutlineIsOutlined()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget parent;
        QWidget *p = f.primaryButton(QStringLiteral("Continue"), &parent);
        QWidget *o = f.outlineButton(QStringLiteral("Open"), &parent);
        p->resize(p->sizeHint());
        o->resize(o->sizeHint());
        const QColor ink(QRgb(token::Ink));
        QVERIFY(sameRgb(grab(p).pixel(8, p->height() / 2), ink));
        const QImage oi = grab(o);
        QVERIFY(sameRgb(oi.pixel(o->width() / 2, 0), ink)); // Top edge of the outline
        QVERIFY(sameRgb(oi.pixel(o->width() / 2, 1), ink));
        QVERIFY(sameRgb(oi.pixel(8, o->height() / 2), QColor(QRgb(token::Paper)))); // Hollow
    }

    void coverFormatLabelAndProgressBar()
    {
        for (int m = 0; m < 2; ++m) {
            kit::KitFactory f(m == 0 ? DeviceModel::ClaraColour : DeviceModel::ClaraBW, nullptr);
            QWidget parent;
            CoverSpec spec;
            spec.pixmap = kit::gallery::fakeCover(1);
            spec.format = QStringLiteral("CBZ");
            spec.progress = 0.5;
            QWidget *c = f.cover(spec, 21.0, &parent);
            const QImage img = grab(c);
            QVERIFY(sameRgb(img.pixel(1, 1), QColor(QRgb(token::Ink)))); // Format label corner
            const int y = img.height() - 2;
            QVERIFY(sameRgb(img.pixel(4, y), f.palette().accent)); // Progress filled part
            QVERIFY(sameRgb(img.pixel(img.width() - 5, y), QColor(QRgb(token::Rule)))); // Track
        }
    }

    void segmentedProgressHasOneSegmentPerBookAndNoRounding()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget parent;
        QWidget *s = f.segmentedProgress(8, 3, &parent);
        s->resize(800, s->height());
        const QImage img = grab(s);
        const int y = img.height() / 2;
        int runs = 0;
        int filled = 0;
        bool inRun = false;
        for (int x = 0; x < img.width(); ++x) {
            const bool paper = sameRgb(img.pixel(x, y), QColor(QRgb(token::Paper)));
            if (!paper && !inRun)
                ++runs;
            inRun = !paper;
            if (sameRgb(img.pixel(x, y), QColor(QRgb(token::Teal))))
                ++filled;
        }
        QCOMPARE(runs, 8);
        QVERIFY(filled > 0);
        QVERIFY(sameRgb(img.pixel(img.width() - 1, y), QColor(QRgb(token::Rule))));
    }

    void pressesRequestPartialRefreshAndScreenChangesRequestFull()
    {
        RefreshLog log;
        kit::KitFactory f(DeviceModel::ClaraColour, &log);
        QWidget page;
        page.resize(f.profile().widthPx, 600);
        QWidget *b = f.primaryButton(QStringLiteral("Continue"), &page);
        b->setGeometry(0, 0, 400, b->height());
        kit::BottomTabs *tabs = static_cast<kit::BottomTabs *>(
                f.bottomTabs(QStringList() << "Home" << "Library", 0, &page));
        tabs->setGeometry(0, 200, 800, tabs->height());
        page.show();

        log.clear();
        click(b, QPoint(20, 20));
        QVERIFY(log.count(RefreshKind::Partial) >= 1);
        QCOMPARE(log.count(RefreshKind::Full), 0);
        QCOMPARE(log.widgets.first(), b);

        log.clear();
        click(tabs, tabs->tabRect(1).center());
        QCOMPARE(log.count(RefreshKind::Full), 1); // Tab change is a screen change.

        log.clear();
        QWidget *d = f.dialog(QStringLiteral("Sure?"), QStringList() << "Yes", &page);
        d->show();
        QCOMPARE(log.count(RefreshKind::Full), 1);
    }

    void pagerPageTurnsAreOnlyPartial()
    {
        RefreshLog log;
        kit::KitFactory f(DeviceModel::ClaraColour, &log);
        QWidget page;
        kit::Pager *pager = static_cast<kit::Pager *>(f.pager(2, 54, &page));
        pager->setGeometry(0, 0, f.profile().widthPx, pager->height());
        page.show();
        QSignalSpy next(pager, SIGNAL(nextTapped()));
        QSignalSpy prev(pager, SIGNAL(previousTapped()));
        QSignalSpy counter(pager, SIGNAL(counterTapped()));
        click(pager, QPoint(pager->width() - 20, 20));
        click(pager, QPoint(20, 20));
        click(pager, QPoint(pager->width() / 2, 20));
        QCOMPARE(next.count(), 1);
        QCOMPARE(prev.count(), 1);
        QCOMPARE(counter.count(), 1);
        QCOMPARE(log.count(RefreshKind::Full), 0);
        // First page has no previous; last has no next.
        pager->setPage(1, 54);
        click(pager, QPoint(20, 20));
        QCOMPARE(prev.count(), 1);
        pager->setPage(54, 54);
        click(pager, QPoint(pager->width() - 20, 20));
        QCOMPARE(next.count(), 1);
    }

    void chipsToggleAndClear()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget page;
        kit::ChipRow *chips = static_cast<kit::ChipRow *>(
                f.chipRow(QStringList() << "Books" << "Comics" << "PDFs", &page));
        chips->setGeometry(0, 0, f.profile().widthPx, chips->height());
        page.show();
        QSignalSpy spy(chips, SIGNAL(chipToggled(int, bool)));
        click(chips, chips->chipRects().at(1).center());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), true);
        QCOMPARE(chips->selected(), QList<int>() << 1);
        click(chips, chips->chipRects().at(1).center());
        QCOMPARE(spy.at(1).at(1).toBool(), false);
        chips->setSelected(QList<int>() << 0 << 2);
        chips->clearSelection();
        QVERIFY(chips->selected().isEmpty());
    }

    void tapSignals()
    {
        kit::KitFactory f(DeviceModel::LibraColour, nullptr);
        QWidget page;
        page.resize(f.profile().widthPx, 800);
        QWidget *btn = f.primaryButton(QStringLiteral("Open"), &page);
        btn->setGeometry(0, 0, 300, btn->height());
        kit::HeaderBar *hdr =
                static_cast<kit::HeaderBar *>(f.headerBar(QStringLiteral("Home"), &page));
        hdr->setGeometry(0, 100, f.profile().widthPx, hdr->height());
        QWidget *search = f.searchField(&page);
        search->setGeometry(0, 300, f.profile().widthPx, search->height());
        kit::Banner *banner = static_cast<kit::Banner *>(
                f.banner(QStringLiteral("3 books were removed from BookOrbit."),
                         QStringLiteral("Review"), &page));
        banner->setGeometry(0, 400, f.profile().widthPx, banner->height());
        kit::LetterGrid *grid = static_cast<kit::LetterGrid *>(f.letterGrid(&page));
        grid->move(0, 500);
        page.show();

        QSignalSpy tapped(btn, SIGNAL(tapped()));
        click(btn, QPoint(10, 10));
        QCOMPARE(tapped.count(), 1);

        QSignalSpy searchTapped(hdr, SIGNAL(searchTapped()));
        click(hdr, QPoint(10, 10)); // On the title: not search.
        QCOMPARE(searchTapped.count(), 0);
        click(hdr, hdr->searchRect().center());
        QCOMPARE(searchTapped.count(), 1);

        QSignalSpy fieldTapped(search, SIGNAL(tapped()));
        click(search, QPoint(100, 20));
        QCOMPARE(fieldTapped.count(), 1);

        QSignalSpy action(banner, SIGNAL(actionTapped()));
        click(banner, QPoint(10, 20)); // On the text: not the action.
        QCOMPARE(action.count(), 0);
        click(banner, banner->actionRect().center());
        QCOMPARE(action.count(), 1);

        QSignalSpy letter(grid, SIGNAL(letterTapped(QString)));
        click(grid, grid->cellRect(2).center());
        QCOMPARE(letter.count(), 1);
        QCOMPARE(letter.at(0).at(0).toString(), QStringLiteral("C"));
        QCOMPARE(kit::LetterGrid::letters().size(), 27);
    }

    void rowsAndBadgesReportTaps()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget page;
        ListRowSpec spec;
        spec.title = QStringLiteral("Piranesi");
        spec.badge = BadgeState::Download;
        kit::ListRow *row = static_cast<kit::ListRow *>(f.listRow(spec, &page));
        row->setGeometry(0, 0, f.profile().widthPx, row->height());
        page.show();
        QSignalSpy rowTap(row, SIGNAL(tapped()));
        QSignalSpy badgeTap(row, SIGNAL(badgeTapped()));
        click(row, QPoint(400, 40));
        QCOMPARE(rowTap.count(), 1);
        click(row, row->badgeRect().center());
        QCOMPARE(badgeTap.count(), 1);
        QCOMPARE(rowTap.count(), 1);
    }

    void fiveTabsFitEveryDeviceWithNinemmTargets()
    {
        const QList<DeviceModel> models = allModels();
        for (int m = 0; m < models.size(); ++m) {
            kit::KitFactory f(models.at(m), nullptr);
            QCOMPARE(f.profile().tabs, 5);
            QWidget parent;
            kit::BottomTabs *tabs = static_cast<kit::BottomTabs *>(f.bottomTabs(
                    QStringList() << "Home" << "Library" << "Series" << "Stats" << "More", 0,
                    &parent));
            tabs->resize(f.profile().widthPx, tabs->height());
            QVERIFY(tabs->heightMm() >= kMinTouchTargetMm);
            const QFont font = f.theme().font(kit::FontRole::Body);
            int covered = 0;
            for (int i = 0; i < 5; ++i) {
                const QRect r = tabs->tabRect(i);
                covered += r.width();
                QVERIFY(r.width() >= mmToPx(kMinTouchTargetMm, f.profile()));
                // Five equal tabs: about 18 mm on the Claras, 21 mm on the Libra.
                QVERIFY(qAbs(r.width() - f.profile().widthPx / 5) <= 1);
                const double mm = r.width() * kMmPerInch / f.profile().ppi;
                if (f.profile().widthPx == 1072)
                    QVERIFY2(qAbs(mm - 18.1) < 0.2, "Clara tabs are about 18 mm");
                else
                    QVERIFY2(qAbs(mm - 21.3) < 0.2, "Libra tabs are about 21 mm");
                QVERIFY(textWidth(font, i == 1 ? "Library" : "Series") < r.width());
            }
            QCOMPARE(covered, f.profile().widthPx);
        }
    }

    void tabsReportChangesOnly()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget page;
        kit::BottomTabs *tabs = static_cast<kit::BottomTabs *>(f.bottomTabs(
                QStringList() << "Home" << "Library" << "Series" << "Stats" << "More", 0, &page));
        tabs->setGeometry(0, 0, f.profile().widthPx, tabs->height());
        page.show();
        QSignalSpy spy(tabs, SIGNAL(tabTapped(int)));
        click(tabs, tabs->tabRect(0).center());
        QCOMPARE(spy.count(), 0);
        click(tabs, tabs->tabRect(3).center());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);
        QCOMPARE(tabs->active(), 3);
    }

    void noAnimationsAnywhere()
    {
        kit::KitFactory f(DeviceModel::LibraColour, nullptr);
        const QStringList names = kit::gallery::sampleNames();
        for (int i = 0; i < names.size(); ++i) {
            QWidget *w = kit::gallery::buildSample(names.at(i), f, f.profile().widthPx);
            QVERIFY2(w->findChildren<QAbstractAnimation *>().isEmpty(), qPrintable(names.at(i)));
            QVERIFY2(w->findChildren<QTimer *>().isEmpty(), qPrintable(names.at(i)));
            delete w;
        }
    }

    void factoryBuildsThroughTheInterface()
    {
        RefreshLog log;
        std::unique_ptr<ComponentFactory> f = createComponentFactory(DeviceModel::ClaraBW, &log);
        QVERIFY(f);
        QCOMPARE(f->profile().model, DeviceModel::ClaraBW);
        QVERIFY(!f->palette().colourCovers);
        QWidget parent;
        QVERIFY(f->headerBar("a", &parent));
        QVERIFY(f->statusLine("a", &parent));
        QVERIFY(f->sectionHeader("a", 2, &parent));
        QVERIFY(f->primaryButton("a", &parent));
        QVERIFY(f->accentButton("a", &parent));
        QVERIFY(f->outlineButton("a", &parent));
        QVERIFY(f->chipRow(QStringList() << "a", &parent));
        QVERIFY(f->bottomTabs(QStringList() << "a", 0, &parent));
        QVERIFY(f->pager(1, 2, &parent));
        QVERIFY(f->searchField(&parent));
        QVERIFY(f->banner("a", "b", &parent));
        QVERIFY(f->dialog("a", QStringList() << "b", &parent));
        QVERIFY(dynamic_cast<kit::KitFactory *>(f.get()));
        // A null refresh requester is allowed.
        std::unique_ptr<ComponentFactory> g = createComponentFactory(DeviceModel::Unknown, nullptr);
        QWidget *btn = g->primaryButton("a", &parent);
        btn->setGeometry(0, 0, 200, btn->height());
        parent.show();
        click(btn, QPoint(5, 5));
    }
};

QTEST_MAIN(TstRules)
#include "tst_rules.moc"
