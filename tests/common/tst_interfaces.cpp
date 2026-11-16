// Compiles every shared interface together and checks the values the spec
// fixes in them (device profiles, colour tokens, palettes, mm layout).
#include "smalt/bookorbit_client.h"
#include "smalt/device.h"
#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/nickel.h"
#include "smalt/outbox.h"
#include "smalt/paths.h"
#include "smalt/safety.h"
#include "smalt/status.h"
#include "smalt/sync.h"
#include "smalt/ui_kit.h"
#include "smalt/version.h"

#include <QtTest>

using smalt::DeviceModel;

class TstInterfaces : public QObject
{
    Q_OBJECT

private slots:

    void versionIsSet() { QVERIFY(QByteArray(SMALT_VERSION).size() > 0); }

    void statusDefaultsToOk()
    {
        QVERIFY(smalt::Status().ok());
        const smalt::Status s =
                smalt::Status::error(smalt::StatusCode::Busy, QStringLiteral("open"));
        QVERIFY(!s.ok());
        QCOMPARE(s.message, QStringLiteral("open"));
    }

    void deviceProfilesMatchTheSpec()
    {
        const smalt::DeviceProfile clara = smalt::deviceProfile(DeviceModel::ClaraColour);
        QCOMPARE(clara.widthPx, 1072);
        QCOMPARE(clara.heightPx, 1448);
        QCOMPARE(clara.ppi, 300);
        QVERIFY(clara.colour);
        QVERIFY(!clara.pageTurnButtons);

        const smalt::DeviceProfile bw = smalt::deviceProfile(DeviceModel::ClaraBW);
        QCOMPARE(bw.widthPx, 1072);
        QCOMPARE(bw.heightPx, 1448);
        QVERIFY(!bw.colour);

        const smalt::DeviceProfile libra = smalt::deviceProfile(DeviceModel::LibraColour);
        QCOMPARE(libra.widthPx, 1264);
        QCOMPARE(libra.heightPx, 1680);
        QCOMPARE(libra.ppi, 300);
        QVERIFY(libra.pageTurnButtons);
    }

    void colourTokensMatchTheSpec()
    {
        const smalt::ui::Palette p = smalt::ui::paletteFor(DeviceModel::ClaraColour);
        QCOMPARE(p.ink.name(), QStringLiteral("#111111"));
        QCOMPARE(p.secondary.name(), QStringLiteral("#555555"));
        QCOMPARE(p.rule.name(), QStringLiteral("#dddddd"));
        QCOMPARE(p.paper.name(), QStringLiteral("#ffffff"));
        QCOMPARE(p.accent.name(), QStringLiteral("#3e6f6a"));
        QCOMPARE(p.accentTint.name(), QStringLiteral("#dce8e5"));
        QCOMPARE(p.accentDark.name(), QStringLiteral("#2f5853"));
        QVERIFY(p.colourCovers);
        QCOMPARE(smalt::ui::paletteFor(DeviceModel::LibraColour).accent, p.accent);
    }

    void monochromePaletteReplacesTeal()
    {
        const smalt::ui::Palette p = smalt::ui::paletteFor(DeviceModel::ClaraBW);
        QCOMPARE(p.accent.name(), QStringLiteral("#111111"));
        QCOMPARE(p.accentTint.name(), QStringLiteral("#e6e6e6"));
        QCOMPARE(p.accentDark.name(), QStringLiteral("#111111"));
        QVERIFY(!p.colourCovers);
    }

    void millimetresGiveTheSameSizeOnEveryDevice()
    {
        const int clara = smalt::ui::mmToPx(9.0, smalt::deviceProfile(DeviceModel::ClaraColour));
        const int libra = smalt::ui::mmToPx(9.0, smalt::deviceProfile(DeviceModel::LibraColour));
        QCOMPARE(clara, libra);
        QCOMPARE(smalt::ui::mmToPx(25.4, 300), 300);
    }
};

QTEST_GUILESS_MAIN(TstInterfaces)
#include "tst_interfaces.moc"
