// Scaffold placeholder for task 13: checks that the module links and
// that the spec values in its interface are what docs/SPEC.md says.
#include "smalt/ui_kit.h"

#include <QtTest>

class TstUiKitPlaceholder : public QObject
{
    Q_OBJECT

private slots:

    void touchTargetIsAtLeastNineMillimetres()
    {
        QCOMPARE(smalt::ui::mmToPx(smalt::ui::kMinTouchTargetMm, 300), 106);
    }
};

QTEST_MAIN(TstUiKitPlaceholder)
#include "tst_placeholder.moc"
