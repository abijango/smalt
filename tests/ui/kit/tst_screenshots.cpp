// Acceptance test: screenshot tests of each component in both palettes, at true
// pixel size, against golden PNGs in tests/ui/kit/golden/.
//
// Regenerate after an intended design change:
//   cmake --build build --target ui_kit_update_goldens
// then look at the changed PNGs (and docs/mockups/) before committing them.
#include "test_support.h"

using namespace kittest;

class TstScreenshots : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void component_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("modelIndex");
        const QStringList names = kit::gallery::sampleNames();
        for (int i = 0; i < names.size(); ++i) {
            QTest::newRow(qPrintable(names.at(i) + QStringLiteral(" colour"))) << names.at(i) << 0;
            QTest::newRow(qPrintable(names.at(i) + QStringLiteral(" mono"))) << names.at(i) << 1;
        }
    }

    // Colour palette: Clara Colour. Monochrome palette: Clara BW.
    void component()
    {
        QFETCH(QString, name);
        QFETCH(int, modelIndex);
        kit::KitFactory f(allModels().at(modelIndex), nullptr);
        QWidget *w = kit::gallery::buildSample(name, f, f.profile().widthPx);
        const QImage img = kit::gallery::renderToImage(w);
        delete w;
        QCOMPARE(img.width(), f.profile().widthPx);
        const QString dir = modelIndex == 0 ? QStringLiteral("colour") : QStringLiteral("mono");
        const QString error = compareGolden(img, dir + QStringLiteral("/") + name);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void libraTabsAndFrames_data()
    {
        QTest::addColumn<int>("modelIndex");
        QTest::addColumn<QString>("name");
        QTest::newRow("clara colour frame") << 0 << QStringLiteral("clara-colour-frame");
        QTest::newRow("clara bw frame") << 1 << QStringLiteral("clara-bw-frame");
        QTest::newRow("libra colour frame") << 2 << QStringLiteral("libra-colour-frame");
        QTest::newRow("libra colour tabs") << 2 << QStringLiteral("libra-colour-bottom_tabs");
        QTest::newRow("libra colour cover row") << 2 << QStringLiteral("libra-colour-cover_row");
    }

    // The three device frames (Home), plus the five-tab bar and four covers.
    void libraTabsAndFrames()
    {
        QFETCH(int, modelIndex);
        QFETCH(QString, name);
        kit::KitFactory f(allModels().at(modelIndex), nullptr);
        QWidget *w = nullptr;
        if (name.endsWith(QLatin1String("-frame")))
            w = kit::gallery::buildHomeFrame(f);
        else
            w = kit::gallery::buildSample(name.mid(name.indexOf(QLatin1String("colour-")) + 7), f,
                                          f.profile().widthPx);
        const QImage img = kit::gallery::renderToImage(w);
        delete w;
        const QString error = compareGolden(img, QStringLiteral("devices/") + name);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
};

QTEST_MAIN(TstScreenshots)
#include "tst_screenshots.moc"
