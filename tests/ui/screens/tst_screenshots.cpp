// Acceptance test: screenshot tests of each screen on the three device
// profiles (Clara Colour: colour palette, Clara BW: monochrome palette, Libra
// Colour: the wider layout), against golden PNGs in tests/ui/screens/golden/.
//
// There is no mockup of these screens in docs/mockups/ yet (only stats.png), so
// the goldens are this implementation's own rendering of the spec text and the
// UI kit; the maintainer compares them with the design. Each golden is half
// size to stay small.
//
// Regenerate after an intended design change:
//   cmake --build build --target ui_screens_update_goldens
#include "support.h"

using namespace screenstest;

namespace {

struct Device
{
    const char *name;
    DeviceModel model;
};

const Device kDevices[] = { { "clara-colour", DeviceModel::ClaraColour },
                            { "clara-bw", DeviceModel::ClaraBW },
                            { "libra-colour", DeviceModel::LibraColour } };

} // namespace

class TstScreenshots : public QObject
{
    Q_OBJECT

private:
    // The same data every time: the cached library, five books on the Kobo,
    // and no network chatter (the fake client answers at once).
    static void prepare(Env &env)
    {
        env.seedCache();
        env.seedKobo();
    }

    static void settle(Shell *shell)
    {
        QCoreApplication::processEvents();
        QTest::qWait(30); // Covers arrive; the shell redraws once.
        QCoreApplication::processEvents();
        shell->update();
    }

    static QString shoot(Shell *shell, const QString &device, const QString &screen)
    {
        settle(shell);
        return compareGolden(renderShell(shell), device + QStringLiteral("/") + screen);
    }

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    void screens_data()
    {
        QTest::addColumn<int>("deviceIndex");
        for (int i = 0; i < 3; ++i)
            QTest::newRow(kDevices[i].name) << i;
    }

    void screens()
    {
        QFETCH(int, deviceIndex);
        const Device &d = kDevices[deviceIndex];
        Env env(d.model);
        prepare(env);
        Shell *shell = env.makeShell();
        QString error;

        // The five tabs.
        error = shoot(shell, d.name, QStringLiteral("home"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->switchTab(1);
        error = shoot(shell, d.name, QStringLiteral("library"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->switchTab(2);
        error = shoot(shell, d.name, QStringLiteral("series"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->switchTab(3);
        error = shoot(shell, d.name, QStringLiteral("stats"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->switchTab(4);
        error = shoot(shell, d.name, QStringLiteral("more"));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // Screens under More.
        shell->openSettings();
        error = shoot(shell, d.name, QStringLiteral("settings"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->pop();

        // Series detail (the first series: one read, one in progress).
        shell->switchTab(2);
        shell->openSeries(1);
        error = shoot(shell, d.name, QStringLiteral("series-detail"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->pop();

        // Book detail: on the Kobo (Open), and not on it (Download).
        shell->switchTab(1);
        shell->openBook(2);
        error = shoot(shell, d.name, QStringLiteral("book-detail-open"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->pop();
        shell->openBook(3);
        error = shoot(shell, d.name, QStringLiteral("book-detail-download"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->pop();

        // Search results, grouped.
        shell->openSearch(QStringLiteral("amber"), false);
        error = shoot(shell, d.name, QStringLiteral("search"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        delete shell;
    }

    // The offline Home and Library: cached pages, nothing downloaded shown as unavailable.
    void offline_data()
    {
        QTest::addColumn<int>("deviceIndex");
        QTest::newRow("clara-colour") << 0;
    }

    void offline()
    {
        QFETCH(int, deviceIndex);
        const Device &d = kDevices[deviceIndex];
        Env env(d.model);
        prepare(env);
        env.fake.offline = true;
        env.nickel->simulateNetwork(false);
        Shell *shell = env.makeShell();
        QString error = shoot(shell, d.name, QStringLiteral("home-offline"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        shell->switchTab(1);
        error = shoot(shell, d.name, QStringLiteral("library-offline"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        delete shell;
    }
};

QTEST_MAIN(TstScreenshots)
#include "tst_screenshots.moc"
