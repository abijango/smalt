// Task 23, acceptance tests: screenshot tests of the three Stats screens and
// the book section on all three device profiles (Clara Colour and Libra Colour
// with the colour palette, Clara BW with the monochrome palette) against golden
// PNGs in tests/stats/golden/, and a check that the screens fit under the chips.
//
// Regenerate after an intended design change:
//   cmake --build build --target stats_update_goldens
// then look at the changed PNGs (and docs/mockups/stats.png) before committing them.
#include "test_support.h"

#include "kit/components.h"
#include "smalt/ui_kit.h"
#include "stats_tab.h"
#include "widgets/charts.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QVBoxLayout>
#include <QtTest>

using namespace smalt;
using namespace smalt::ui;
using namespace smalt::ui::stats;
using namespace statstest;

namespace {

const QString kBook = QStringLiteral("file:///mnt/onboard/BookOrbit/A/Piranesi.epub");

// 23:30 on Friday 20 November 2026: today's three sessions are behind us.
QDateTime shotNow()
{
    return utc(2026, 11, 20, 23, 30, 0);
}

QByteArray coverPng(int seed)
{
    static const unsigned int colours[] = { 0x5B7BA0, 0xB5893A, 0x6E6E7A, 0x4A7F66,
                                            0x8E6A96, 0x5F82A8, 0xA86A55, 0x7B8C5A,
                                            0x4F6E7E, 0x9E5367, 0x6A5C8C, 0x6F8F7F };
    QImage img(40, 60, QImage::Format_RGB32);
    img.fill(QColor(QRgb(colours[seed % 12])));
    QPainter p(&img);
    p.fillRect(QRect(0, 0, 40, 10), QColor(255, 255, 255, 60));
    p.end();
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

} // namespace

class TstScreenshots : public QObject
{
    Q_OBJECT

    LedgerFixture *m_fx = nullptr;

    static QString deviceName(int index)
    {
        static const char *const names[] = { "clara-colour", "clara-bw", "libra-colour" };
        return QLatin1String(names[index]);
    }

    static DeviceModel model(int index)
    {
        static const DeviceModel models[] = { DeviceModel::ClaraColour, DeviceModel::ClaraBW,
                                              DeviceModel::LibraColour };
        return models[index];
    }

    StatsOptions options() const
    {
        StatsOptions o;
        o.now = [] { return shotNow(); };
        o.zone = smalt::stats::fixedZone(0);
        return o;
    }

    void setChip(int chip)
    {
        QVERIFY(m_fx->ledger()
                        ->cachePut(QStringLiteral("stats.chip"), QStringLiteral("stats-ui"),
                                   QByteArray::number(chip), shotNow())
                        .ok());
    }

    // The tab inside the shell's chrome as the mockup shows it: header and tabs (the sync
    // status is part of the header there).
    QImage renderTab(int deviceIndex, int chip, int *pageHeightPx = nullptr)
    {
        setChip(chip);
        std::unique_ptr<ComponentFactory> kit = createComponentFactory(model(deviceIndex), nullptr);
        const DeviceProfile &p = kit->profile();
        QWidget frame;
        frame.setAutoFillBackground(true);
        QPalette pal = frame.palette();
        pal.setColor(QPalette::Window, Qt::white);
        frame.setPalette(pal);
        frame.resize(p.widthPx, p.heightPx);
        QVBoxLayout *l = new QVBoxLayout(&frame);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(kit->headerBar(QStringLiteral("Stats"), &frame));
        StatsDeps deps;
        deps.kit = kit.get();
        deps.ledger = m_fx->ledger();
        StatsTab *tab = new StatsTab(deps, options(), &frame);
        l->addWidget(tab, 1);
        l->addWidget(kit->bottomTabs(QStringList()
                                             << QStringLiteral("Home") << QStringLiteral("Library")
                                             << QStringLiteral("Series") << QStringLiteral("Stats")
                                             << QStringLiteral("More"),
                                     3, &frame));
        frame.show();
        QImage image(frame.size(), QImage::Format_RGB32);
        image.fill(Qt::white);
        frame.render(&image);
        if (pageHeightPx)
            *pageHeightPx = tab->page()->layout()->sizeHint().height() + tab->chips()->height();
        return image;
    }

    // Header-less: the section as book detail would place it.
    QImage renderBook(int deviceIndex)
    {
        std::unique_ptr<ComponentFactory> kit = createComponentFactory(model(deviceIndex), nullptr);
        const DeviceProfile &p = kit->profile();
        QWidget host;
        host.setAutoFillBackground(true);
        QPalette pal = host.palette();
        pal.setColor(QPalette::Window, Qt::white);
        host.setPalette(pal);
        QVBoxLayout *l = new QVBoxLayout(&host);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        StatsDeps deps;
        deps.kit = kit.get();
        deps.ledger = m_fx->ledger();
        YourReadingSection *s = new YourReadingSection(deps, kBook, options(), &host);
        l->addWidget(s);
        host.resize(p.widthPx, l->sizeHint().height());
        host.show();
        QImage image(host.size(), QImage::Format_RGB32);
        image.fill(Qt::white);
        host.render(&image);
        return image;
    }

private slots:

    void initTestCase()
    {
        QVERIFY(pinTestFont());
        m_fx = new LedgerFixture;
        QVERIFY(m_fx->ok());
        Ledger *ledger = m_fx->ledger();

        // The server's side: the dashboard fixture, fetched an hour ago, and covers.
        m_fx->cacheDashboard(loadFixtureObject(QStringLiteral("dashboard_full.json")),
                             shotNow().addSecs(-3600));
        for (int id = 1; id <= 12; ++id)
            QVERIFY(ledger->cachePut(QStringLiteral("stats.thumb.%1").arg(id),
                                     QStringLiteral("stats-thumb"), coverPng(id), shotNow())
                            .ok());

        // This Kobo's side: Piranesi, read most evenings for two months, and three
        // sessions today. Nothing was pushed yet.
        for (int d = 1; d <= 60; ++d) {
            if ((d * 5) % 7 == 0)
                continue;
            const QDateTime start = utc(2026, 11, 20, 21, 0).addDays(-d).addSecs((d % 3) * 1200);
            m_fx->read(kBook, start, 12, 100 + (d % 5) * 30, 5 + d, 400);
        }
        m_fx->read(kBook, utc(2026, 11, 20, 7, 10), 8, 150, 300, 400); // 20 min
        m_fx->read(kBook, utc(2026, 11, 20, 12, 40), 8, 150, 310, 400); // 20 min
        m_fx->read(kBook, utc(2026, 11, 20, 21, 5), 26, 150, 320, 400); // 65 min
        Position p;
        p.percent = 41;
        QVERIFY(ledger->setLocalProgress(kBook, p, utc(2026, 11, 20, 22, 10)).ok());
    }

    void cleanupTestCase()
    {
        delete m_fx;
        m_fx = nullptr;
    }

    void screen_data()
    {
        QTest::addColumn<int>("device");
        QTest::addColumn<int>("chip");
        static const char *const screens[] = { "overview", "patterns", "year" };
        for (int d = 0; d < 3; ++d)
            for (int c = 0; c < 3; ++c)
                QTest::newRow(
                        qPrintable(deviceName(d) + QLatin1Char(' ') + QLatin1String(screens[c])))
                        << d << c;
    }

    // The tab with each chip on, in the shell's chrome.
    void screen()
    {
        QFETCH(int, device);
        QFETCH(int, chip);
        static const char *const screens[] = { "overview", "patterns", "year" };
        const QImage img = renderTab(device, chip);
        const QString error = compareGolden(
                img, deviceName(device) + QLatin1Char('/') + QLatin1String(screens[chip]));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void book_data()
    {
        QTest::addColumn<int>("device");
        for (int d = 0; d < 3; ++d)
            QTest::newRow(qPrintable(deviceName(d))) << d;
    }

    // "Your reading" for book detail.
    void book()
    {
        QFETCH(int, device);
        const QImage img = renderBook(device);
        const QString error = compareGolden(img, deviceName(device) + QStringLiteral("/book"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    // Each screen fits a screen: no taller than what is left under the chips, with the
    // shell's header (13 mm) and tabs (14 mm) around it.
    void screensFitBelowTheChips_data() { screen_data(); }

    void screensFitBelowTheChips()
    {
        QFETCH(int, device);
        QFETCH(int, chip);
        int pageAndChips = 0;
        renderTab(device, chip, &pageAndChips);
        std::unique_ptr<ComponentFactory> kit = createComponentFactory(model(device), nullptr);
        const DeviceProfile &p = kit->profile();
        const int chrome = mmToPx(13.0 + 14.0, p);
        const int room = p.heightPx - chrome;
        qInfo("%s chip %d: %d px (%.1f mm) of %d px (%.1f mm) free", qPrintable(deviceName(device)),
              chip, pageAndChips, pageAndChips * 25.4 / p.ppi, room, room * 25.4 / p.ppi);
        QVERIFY2(pageAndChips + mmToPx(3.0, p) <= room,
                 qPrintable(QStringLiteral("%1 chip %2 needs %3 px, %4 px free")
                                    .arg(deviceName(device))
                                    .arg(chip)
                                    .arg(pageAndChips)
                                    .arg(room)));
    }

    // Colour only where the design allows it: on Clara BW every pixel is grey.
    void claraBWIsMonochrome()
    {
        for (int chip = 0; chip < 3; ++chip) {
            const QImage img = renderTab(1, chip);
            bool allGrey = true;
            for (int y = 0; y < img.height() && allGrey; ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb c = img.pixel(x, y);
                    if (qRed(c) != qGreen(c) || qGreen(c) != qBlue(c)) {
                        allGrey = false;
                        break;
                    }
                }
            QVERIFY2(allGrey,
                     qPrintable(QStringLiteral("chip %1 has colour on Clara BW").arg(chip)));
        }
    }

    // The heatmap is 26 weeks on the Claras and 52 on the Libra.
    void heatmapWeeksFollowTheDevice()
    {
        setChip(0);
        for (int d = 0; d < 3; ++d) {
            std::unique_ptr<ComponentFactory> kit = createComponentFactory(model(d), nullptr);
            StatsDeps deps;
            deps.kit = kit.get();
            deps.ledger = m_fx->ledger();
            std::unique_ptr<StatsTab> tab(new StatsTab(deps, options(), nullptr));
            HeatmapWidget *heat = tab->page()->findChild<HeatmapWidget *>();
            QVERIFY(heat);
            QCOMPARE(heat->grid().weeks, d == 2 ? 52 : 26);
        }
    }
};

QTEST_MAIN(TstScreenshots)
#include "tst_screenshots.moc"
