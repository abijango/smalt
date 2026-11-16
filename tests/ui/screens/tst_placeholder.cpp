// Scaffold placeholder for task 22: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "smalt/device.h"

#include <QtTest>

class TstScreensPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void deviceLayouts()
    {
        const smalt::DeviceProfile clara = smalt::deviceProfile(smalt::DeviceModel::ClaraColour);
        const smalt::DeviceProfile libra = smalt::deviceProfile(smalt::DeviceModel::LibraColour);
        QCOMPARE(clara.readingNowCovers, 3);
        QCOMPARE(libra.readingNowCovers, 4);
        QCOMPARE(clara.upNextRows, 1);
        QCOMPARE(libra.upNextRows, 2);
        QCOMPARE(clara.tabs, 4);
        QCOMPARE(libra.tabs, 5);
    }
};

QTEST_GUILESS_MAIN(TstScreensPlaceholder)
#include "tst_placeholder.moc"
