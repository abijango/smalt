// Acceptance test: "Millimetre conversion gives identical physical sizes on 6"
// and 7" profiles", plus the touch-target rule ("at least 9 mm tall").
#include "test_support.h"

using namespace kittest;

namespace {

double toMm(int px, int ppi)
{
    return px * kMmPerInch / ppi;
}

// Every tappable component the factory builds, for one profile.
QList<QWidget *> tappables(kit::KitFactory &f, QWidget *parent)
{
    QList<QWidget *> w;
    w << f.headerBar(QStringLiteral("Home"), parent)
      << f.statusLine(QStringLiteral("Synced"), parent)
      << f.primaryButton(QStringLiteral("Continue"), parent)
      << f.accentButton(QStringLiteral("Download"), parent)
      << f.outlineButton(QStringLiteral("Stay here"), parent)
      << f.chipRow(QStringList() << QStringLiteral("Books"), parent)
      << f.bottomTabs(QStringList() << QStringLiteral("Home") << QStringLiteral("More"), 0, parent)
      << f.pager(1, 3, parent) << f.searchField(parent)
      << f.banner(QStringLiteral("Removed"), QStringLiteral("Review"), parent)
      << f.downloadBadge(kit::BadgeState::Download, QString(), parent);
    kit::ListRowSpec row;
    row.title = QStringLiteral("T");
    w << f.listRow(row, parent);
    w << f.letterGrid(parent);
    return w;
}

} // namespace

class TstScaling : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void mmToPxMatchesThePpi()
    {
        QCOMPARE(mmToPx(25.4, 300), 300);
        QCOMPARE(mmToPx(9.0, 300), 106);
        QCOMPARE(mmToPx(0.0, 300), 0);
        QCOMPARE(mmToPx(9.0, deviceProfile(DeviceModel::LibraColour)), 106);
    }

    void allProfilesAre300Ppi()
    {
        for (int i = 0; i < allModels().size(); ++i)
            QCOMPARE(deviceProfile(allModels().at(i)).ppi, 300);
    }

    void screenSizesMatchTheSpec()
    {
        QCOMPARE(deviceProfile(DeviceModel::ClaraColour).widthPx, 1072);
        QCOMPARE(deviceProfile(DeviceModel::ClaraColour).heightPx, 1448);
        QCOMPARE(deviceProfile(DeviceModel::ClaraBW).heightPx, 1448);
        QCOMPARE(deviceProfile(DeviceModel::LibraColour).widthPx, 1264);
        QCOMPARE(deviceProfile(DeviceModel::LibraColour).heightPx, 1680);
    }

    void componentsHaveIdenticalPhysicalHeightOnSixAndSevenInch()
    {
        kit::KitFactory clara(DeviceModel::ClaraColour, nullptr);
        kit::KitFactory libra(DeviceModel::LibraColour, nullptr);
        QWidget pc, pl;
        const QList<QWidget *> a = tappables(clara, &pc);
        const QList<QWidget *> b = tappables(libra, &pl);
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(a.at(i)->height(), b.at(i)->height());
            QCOMPARE(toMm(a.at(i)->height(), 300), toMm(b.at(i)->height(), 300));
        }
    }

    void fixedSizeComponentsAreIdenticalOnSixAndSevenInch()
    {
        kit::KitFactory clara(DeviceModel::ClaraBW, nullptr);
        kit::KitFactory libra(DeviceModel::LibraColour, nullptr);
        QWidget pc, pl;
        // Covers, badges, the letter grid and cover stack do not grow on the big screen.
        QCOMPARE(clara.cover(kit::CoverSpec(), 21.0, &pc)->size(),
                 libra.cover(kit::CoverSpec(), 21.0, &pl)->size());
        QCOMPARE(clara.letterGrid(&pc)->size(), libra.letterGrid(&pl)->size());
        QCOMPARE(clara.coverStack(kit::CoverSpec(), &pc)->size(),
                 libra.coverStack(kit::CoverSpec(), &pl)->size());
        QCOMPARE(clara.downloadBadge(kit::BadgeState::Download, QString(), &pc)->size(),
                 libra.downloadBadge(kit::BadgeState::Download, QString(), &pl)->size());
        QCOMPARE(clara.theme().font(kit::FontRole::Body).pixelSize(),
                 libra.theme().font(kit::FontRole::Body).pixelSize());
        QCOMPARE(clara.theme().px(kit::metrics::kPageMarginMm),
                 libra.theme().px(kit::metrics::kPageMarginMm));
    }

    void libraShowsMoreItemsNotBiggerOnes()
    {
        kit::KitFactory clara(DeviceModel::ClaraColour, nullptr);
        kit::KitFactory libra(DeviceModel::LibraColour, nullptr);
        QCOMPARE(clara.profile().readingNowCovers, 3);
        QCOMPARE(libra.profile().readingNowCovers, 4);
        QCOMPARE(clara.profile().upNextRows, 1);
        QCOMPARE(libra.profile().upNextRows, 2);
        for (int i = 0; i < 2; ++i) {
            kit::KitFactory &f = i == 0 ? clara : libra;
            QWidget parent;
            QList<kit::CoverSpec> covers;
            for (int c = 0; c < 6; ++c)
                covers << kit::CoverSpec();
            kit::CoverRow *row = static_cast<kit::CoverRow *>(f.coverRow(covers, &parent));
            row->resize(f.profile().widthPx, row->height());
            const int visibleCovers = f.profile().readingNowCovers;
            // The last visible cover fits on the screen; one more would not fit.
            QVERIFY(row->coverRect(visibleCovers - 1).right()
                    < f.profile().widthPx - f.theme().px(kit::metrics::kPageMarginMm) + 1);
            QVERIFY(row->coverRect(visibleCovers).right()
                    >= f.profile().widthPx - f.theme().px(kit::metrics::kPageMarginMm));
            // The cover width is 21 mm on both.
            QCOMPARE(row->coverRect(0).width(), mmToPx(21.0, 300));
        }
    }

    void tabsFollowTheProfile()
    {
        QCOMPARE(deviceProfile(DeviceModel::ClaraColour).tabs, 4);
        QCOMPARE(deviceProfile(DeviceModel::ClaraBW).tabs, 4);
        QCOMPARE(deviceProfile(DeviceModel::LibraColour).tabs, 5);
        // Five tabs still give each tab a 9 mm wide target on the narrowest screen they use.
        kit::KitFactory libra(DeviceModel::LibraColour, nullptr);
        QWidget parent;
        kit::BottomTabs *tabs = static_cast<kit::BottomTabs *>(libra.bottomTabs(
                QStringList() << "Home" << "Library" << "Series" << "Shelves" << "More", 0,
                &parent));
        tabs->resize(libra.profile().widthPx, tabs->height());
        QVERIFY(tabs->tabRect(0).width() >= mmToPx(kMinTouchTargetMm, 300));
    }

    void touchTargetsAreAtLeastNineMillimetresTall_data()
    {
        QTest::addColumn<int>("modelIndex");
        QTest::newRow("clara colour") << 0;
        QTest::newRow("clara bw") << 1;
        QTest::newRow("libra colour") << 2;
    }

    void touchTargetsAreAtLeastNineMillimetresTall()
    {
        QFETCH(int, modelIndex);
        kit::KitFactory f(allModels().at(modelIndex), nullptr);
        QWidget parent;
        const QList<QWidget *> widgets = tappables(f, &parent);
        const int minPx = mmToPx(kMinTouchTargetMm, f.profile().ppi);
        for (int i = 0; i < widgets.size(); ++i) {
            QVERIFY2(widgets.at(i)->height() >= minPx,
                     qPrintable(QStringLiteral("%1 is %2 px tall, needs %3")
                                        .arg(QString::fromLatin1(
                                                widgets.at(i)->metaObject()->className()))
                                        .arg(widgets.at(i)->height())
                                        .arg(minPx)));
        }
        // Individual hit areas inside multi-target components.
        kit::ChipRow *chips =
                static_cast<kit::ChipRow *>(f.chipRow(QStringList() << "A" << "B", &parent));
        chips->resize(f.profile().widthPx, chips->height());
        for (const QRect &r : chips->chipRects())
            QVERIFY(r.height() >= minPx);
        kit::LetterGrid *grid = static_cast<kit::LetterGrid *>(f.letterGrid(&parent));
        QVERIFY(grid->cellRect(0).height() >= minPx);
        QVERIFY(grid->cellRect(0).width() >= minPx);
        kit::HeaderBar *header = static_cast<kit::HeaderBar *>(f.headerBar("x", &parent));
        header->resize(f.profile().widthPx, header->height());
        QVERIFY(header->searchRect().height() >= minPx);
        QVERIFY(header->searchRect().width() >= minPx);
    }

    void dialogButtonsAreTouchTargets()
    {
        kit::KitFactory f(DeviceModel::ClaraColour, nullptr);
        QWidget page;
        page.resize(f.profile().widthPx, f.profile().heightPx);
        QWidget *d = f.dialog(QStringLiteral("Sure?"), QStringList() << "Yes" << "No", &page);
        page.show();
        const QList<kit::Button *> buttons = d->findChildren<kit::Button *>();
        QCOMPARE(buttons.size(), 2);
        for (int i = 0; i < buttons.size(); ++i)
            QVERIFY(buttons.at(i)->height() >= mmToPx(kMinTouchTargetMm, 300));
    }
};

QTEST_MAIN(TstScaling)
#include "tst_scaling.moc"
