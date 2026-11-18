// Acceptance test: "Palette swap replaces every teal use with the monochrome
// mapping" (spec: Colours, Rules, Monochrome palette (Clara BW)).
#include "test_support.h"

using namespace kittest;

namespace {

bool hasCover(const QString &name)
{
    return name == QLatin1String("cover") || name == QLatin1String("cover_row")
            || name == QLatin1String("cover_stack") || name.startsWith(QLatin1String("list_row_"));
}

QImage renderSample(DeviceModel model, const QString &name)
{
    kit::KitFactory factory(model, nullptr);
    QWidget *w = kit::gallery::buildSample(name, factory, factory.profile().widthPx);
    const QImage img = kit::gallery::renderToImage(w);
    delete w;
    return img;
}

} // namespace

class TstPalette : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void tokensMatchTheSpecTable()
    {
        QCOMPARE(QColor(QRgb(token::Ink)).name(), QStringLiteral("#111111"));
        QCOMPARE(QColor(QRgb(token::Secondary)).name(), QStringLiteral("#555555"));
        QCOMPARE(QColor(QRgb(token::Rule)).name(), QStringLiteral("#dddddd"));
        QCOMPARE(QColor(QRgb(token::Paper)).name(), QStringLiteral("#ffffff"));
        QCOMPARE(QColor(QRgb(token::Teal)).name(), QStringLiteral("#3e6f6a"));
        QCOMPARE(QColor(QRgb(token::TealTint)).name(), QStringLiteral("#dce8e5"));
        QCOMPARE(QColor(QRgb(token::TealDark)).name(), QStringLiteral("#2f5853"));
    }

    void monochromeMappingReplacesEveryTealToken()
    {
        const Palette colour = paletteFor(DeviceModel::ClaraColour);
        const Palette mono = paletteFor(DeviceModel::ClaraBW);
        QCOMPARE(colour.accent, QColor(QRgb(token::Teal)));
        QCOMPARE(colour.accentTint, QColor(QRgb(token::TealTint)));
        QCOMPARE(colour.accentDark, QColor(QRgb(token::TealDark)));
        QVERIFY(colour.colourCovers);
        // Teal -> Ink, Teal tint -> light grey, Teal dark -> Ink, covers greyscale.
        QCOMPARE(mono.accent, QColor(QRgb(token::Ink)));
        QCOMPARE(mono.accentTint, QColor(QStringLiteral("#E6E6E6")));
        QCOMPARE(mono.accentDark, QColor(QRgb(token::Ink)));
        QVERIFY(!mono.colourCovers);
        QCOMPARE(paletteFor(DeviceModel::LibraColour).accent, colour.accent);
        QCOMPARE(paletteFor(DeviceModel::Unknown).accent, colour.accent);
    }

    void textColoursAreNeverAccents()
    {
        for (int i = 0; i < allModels().size(); ++i) {
            kit::Theme theme(deviceProfile(allModels().at(i)), nullptr);
            QVERIFY2(isGrey(theme.textColour().rgb()), "text colours must be neutral");
            QVERIFY2(isGrey(theme.secondaryTextColour().rgb()), "text colours must be neutral");
            QVERIFY2(isGrey(theme.onFillTextColour().rgb()), "text colours must be neutral");
        }
    }

    void monochromeRenderHasNoColourAnywhere_data()
    {
        QTest::addColumn<QString>("name");
        const QStringList names = kit::gallery::sampleNames();
        for (int i = 0; i < names.size(); ++i)
            QTest::newRow(qPrintable(names.at(i))) << names.at(i);
    }

    void monochromeRenderHasNoColourAnywhere()
    {
        QFETCH(QString, name);
        // Includes the covers: they are coloured pictures and must be greyscale on Clara BW.
        const QImage img = renderSample(DeviceModel::ClaraBW, name);
        QCOMPARE(countPixels(img, isNotGrey), 0);
    }

    void monochromeHomeFrameHasNoColour()
    {
        kit::KitFactory factory(DeviceModel::ClaraBW, nullptr);
        QWidget *frame = kit::gallery::buildHomeFrame(factory);
        const QImage img = kit::gallery::renderToImage(frame);
        delete frame;
        QCOMPARE(countPixels(img, isNotGrey), 0);
    }

    void coloursOnlyWhereTealIsAllowed_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("usesTeal");
        const QStringList names = kit::gallery::sampleNames();
        const QStringList teal = QStringList()
                << QStringLiteral("accent_button") << QStringLiteral("bottom_tabs")
                << QStringLiteral("segmented_progress") << QStringLiteral("download_badges")
                << QStringLiteral("cover");
        for (int i = 0; i < names.size(); ++i) {
            if (names.at(i) == QLatin1String("cover_row")
                || names.at(i) == QLatin1String("cover_stack")
                || names.at(i).startsWith(QLatin1String("list_row_")))
                continue; // Coloured covers: checked by coloursAreOnlyCoversAndTealOnRows.
            QTest::newRow(qPrintable(names.at(i))) << names.at(i) << teal.contains(names.at(i));
        }
    }

    void coloursOnlyWhereTealIsAllowed()
    {
        QFETCH(QString, name);
        QFETCH(bool, usesTeal);
        const QImage img = renderSample(DeviceModel::ClaraColour, name);
        if (name == QLatin1String("cover")) {
            // The coloured covers are the other allowed colour; the progress bar is teal.
            QVERIFY(countPixels(img, isTealFamily) > 0);
            return;
        }
        // Everything coloured must be teal family: no coloured text, no other accent.
        QCOMPARE(countPixels(img, isNotGreyOrTeal), 0);
        if (usesTeal)
            QVERIFY2(countPixels(img, isTealFamily) > 0, "expected the teal accent");
        else
            QCOMPARE(countPixels(img, isNotNearGrey), 0); // Teal means getting and progress only.
    }

    void coloursAreOnlyCoversAndTealOnRows()
    {
        // The row's text column (right of the thumbnail) is never coloured except by the badge.
        kit::KitFactory factory(DeviceModel::ClaraColour, nullptr);
        const kit::Theme &t = factory.theme();
        QWidget *w = kit::gallery::buildSample(QStringLiteral("list_row_downloading"), factory,
                                               factory.profile().widthPx);
        const QImage img = kit::gallery::renderToImage(w);
        delete w;
        const int textLeft =
                t.px(kit::metrics::kPageMarginMm + kit::metrics::kListCoverWidthMm + 3.0);
        const int badgeLeft =
                img.width() - t.px(kit::metrics::kPageMarginMm + kit::metrics::kBadgeSizeMm + 6.0);
        QCOMPARE(countPixels(img, isNotNearGrey,
                             QRect(textLeft, 0, badgeLeft - textLeft, img.height())),
                 0);
        // The cover itself is in colour on a colour device.
        QVERIFY(countPixels(img, isNotNearGrey, QRect(0, 0, textLeft, img.height())) > 0);
    }

    void swapKeepsEveryNonTealPixelIdentical_data()
    {
        QTest::addColumn<QString>("name");
        const QStringList names = kit::gallery::sampleNames();
        for (int i = 0; i < names.size(); ++i)
            if (!hasCover(names.at(i)))
                QTest::newRow(qPrintable(names.at(i))) << names.at(i);
    }

    // The layout is shared; only teal pixels change between the palettes.
    void swapKeepsEveryNonTealPixelIdentical()
    {
        QFETCH(QString, name);
        const QImage colour = renderSample(DeviceModel::ClaraColour, name);
        const QImage mono = renderSample(DeviceModel::ClaraBW, name);
        QCOMPARE(colour.size(), mono.size());
        int changedGrey = 0;
        for (int y = 0; y < colour.height(); ++y) {
            for (int x = 0; x < colour.width(); ++x) {
                const QRgb c = colour.pixel(x, y);
                const QRgb m = mono.pixel(x, y);
                if (isNearGrey(c) && qAbs(qGray(c) - qGray(m)) > 6)
                    ++changedGrey;
            }
        }
        QCOMPARE(changedGrey, 0);
    }
};

QTEST_MAIN(TstPalette)
#include "tst_palette.moc"
