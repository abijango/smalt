// Acceptance test: every item of the spec's Settings screen round-trips through
// the SettingsStore (or config.ini for the login), and Test connection reports
// what the server said.
#include "bookorbit/mock_server.h"
#include "bookorbit/client_config.h"
#include "screens/login_config.h"
#include "screens/pages.h"
#include "smalt/log.h"
#include "support.h"

#include <QLineEdit>

using namespace screenstest;
using namespace smalt::ui::kit;

namespace {

ColumnScreen *column(Shell *shell)
{
    return qobject_cast<ColumnScreen *>(shell->currentScreen());
}

// Finds a row by title on the current screen, flipping pages from the first.
TextRow *goToRow(Shell *shell, const QString &title)
{
    PagedColumn *list = column(shell)->column();
    list->setPage(0);
    for (int i = 0; i < list->pageCount(); ++i) {
        if (TextRow *row = rowTitled(shell->currentScreen(), title))
            return row;
        list->setPage(i + 1);
    }
    return rowTitled(shell->currentScreen(), title);
}

// Taps a row, then the nth option of the page of choices that opens.
void choose(Shell *shell, const QString &rowTitle, int option)
{
    TextRow *row = goToRow(shell, rowTitle);
    QVERIFY2(row, qPrintable(rowTitle));
    tap(row);
    auto *choices = qobject_cast<ChoiceScreen *>(shell->currentScreen());
    QVERIFY2(choices, qPrintable(rowTitle));
    QVERIFY(option < choices->column()->blocks().size());
    tap(qobject_cast<TextRow *>(choices->column()->blocks().at(option)));
    QVERIFY(!qobject_cast<ChoiceScreen *>(shell->currentScreen()));
}

void toggle(Shell *shell, const QString &rowTitle)
{
    TextRow *row = goToRow(shell, rowTitle);
    QVERIFY2(row, qPrintable(rowTitle));
    tap(row);
}

// Types into the open text prompt and taps Done.
void answerPrompt(Shell *shell, const QString &text)
{
    QVERIFY(shell->promptOpen());
    QLineEdit *edit = shell->prompt()->findChild<QLineEdit *>();
    QVERIFY(edit);
    edit->setText(text);
    const QList<Button *> buttons = shell->prompt()->findChildren<Button *>();
    QCOMPARE(buttons.size(), 2);
    tap(buttons.last());
}

Settings reload(const Paths &paths)
{
    std::unique_ptr<SettingsStore> fresh = createSettingsStore(paths);
    fresh->load();
    return fresh->current();
}

} // namespace

class TstSettings : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    // Every item of the spec's list is on the screen.
    void everyItemOfTheSpecIsThere()
    {
        Env env(DeviceModel::ClaraColour);
        Shell *shell = env.makeShell();
        shell->openSettings();
        const QStringList titles = QStringList()
                << "Server URL" << "KOReader username" << "KOReader password"
                << "Sync when a book closes" << "Sync before sleep" << "Sync on wake"
                << "Sync when Wi-Fi is on" << "Skip pre-sleep sync below" << "Push while reading"
                << "New session after sleep" << "Mirror collections"
                << "Keep an Abandoned collection" << "Keep a Want to read collection"
                << "Download folder" << "Export log";
        for (const QString &t : titles)
            QVERIFY2(goToRow(shell, t), qPrintable(t));
        // Test connection is a button on the first page.
        column(shell)->column()->setPage(0);
        QVERIFY(buttonNamed(shell->currentScreen(), QStringLiteral("testConnection")));
        // The interval only shows while pushing is on (the default).
        QVERIFY(goToRow(shell, QStringLiteral("Push every")));
        delete shell;
    }

    void switchesAndChoicesRoundTrip()
    {
        Env env(DeviceModel::ClaraColour);
        Shell *shell = env.makeShell();
        shell->openSettings();

        // The defaults.
        Settings s = env.settings->current();
        QVERIFY(s.syncOnBookClose && s.syncBeforeSleep && s.syncOnWake && s.syncWhenWifiOn);
        QCOMPARE(int(s.livePushMode), int(LivePushMode::OnlyWhenConnected));

        // A switch for each trigger.
        toggle(shell, QStringLiteral("Sync when a book closes"));
        toggle(shell, QStringLiteral("Sync before sleep"));
        toggle(shell, QStringLiteral("Sync on wake"));
        toggle(shell, QStringLiteral("Sync when Wi-Fi is on"));
        s = reload(env.paths);
        QVERIFY(!s.syncOnBookClose && !s.syncBeforeSleep && !s.syncOnWake && !s.syncWhenWifiOn);
        toggle(shell, QStringLiteral("Sync on wake"));
        QVERIFY(reload(env.paths).syncOnWake);

        // Battery threshold: Never skip, 10, 20, 30, 40, 50.
        choose(shell, QStringLiteral("Skip pre-sleep sync below"), 3);
        QCOMPARE(reload(env.paths).preSleepMinBatteryPercent, 30);

        // Push while reading: Off, Only when connected, Connect briefly (with its default interval).
        choose(shell, QStringLiteral("Push while reading"), 2);
        s = reload(env.paths);
        QCOMPARE(int(s.livePushMode), int(LivePushMode::ConnectBriefly));
        QCOMPARE(s.livePushMinutes, 15);
        choose(shell, QStringLiteral("Push every"), 3); // 5, 10, 15, 30
        QCOMPARE(reload(env.paths).livePushMinutes, 30);
        choose(shell, QStringLiteral("Push while reading"), 1);
        s = reload(env.paths);
        QCOMPARE(int(s.livePushMode), int(LivePushMode::OnlyWhenConnected));
        QCOMPARE(s.livePushMinutes, 5); // The mode's default.
        choose(shell, QStringLiteral("Push while reading"), 0);
        QCOMPARE(int(reload(env.paths).livePushMode), int(LivePushMode::Off));
        QVERIFY(!goToRow(shell, QStringLiteral("Push every"))); // Nothing to set while Off.

        // New session after sleep: 15 minutes, 1 hour, 4 hours, 6 hours, 12 hours, Never.
        QCOMPARE(reload(env.paths).newSessionAfterSleepSeconds, 15 * 60);
        choose(shell, QStringLiteral("New session after sleep"), 2);
        QCOMPARE(reload(env.paths).newSessionAfterSleepSeconds, 4 * 3600);
        choose(shell, QStringLiteral("New session after sleep"), 5);
        QCOMPARE(reload(env.paths).newSessionAfterSleepSeconds, kNewSessionAfterSleepNever);

        // Collections: mirroring (on by default), Abandoned and Want to read (off).
        QVERIFY(reload(env.paths).mirrorCollections);
        toggle(shell, QStringLiteral("Mirror collections"));
        toggle(shell, QStringLiteral("Keep an Abandoned collection"));
        toggle(shell, QStringLiteral("Keep a Want to read collection"));
        s = reload(env.paths);
        QVERIFY(!s.mirrorCollections && s.abandonedCollection && s.wantToReadCollection);

        // Download folder.
        TextRow *folder = goToRow(shell, QStringLiteral("Download folder"));
        QVERIFY(folder);
        tap(folder);
        answerPrompt(shell, QStringLiteral("Books/Kobo"));
        QCOMPARE(reload(env.paths).downloadFolder, QStringLiteral("Books/Kobo"));
        // The screen shows the saved values.
        QCOMPARE(goToRow(shell, QStringLiteral("Download folder"))->value(),
                 QStringLiteral("Books/Kobo"));
        QCOMPARE(goToRow(shell, QStringLiteral("New session after sleep"))->value(),
                 QStringLiteral("Never"));
        delete shell;
    }

    void theLoginIsWrittenToConfigIniAndKeepsTheRest()
    {
        QTemporaryDir dir;
        const Paths paths = Paths::forRoot(dir.path());
        QDir().mkpath(paths.addsDir());
        // Start from the shipped example: comments and layout stay.
        QFile example(QStringLiteral(SMALT_REPO_DIR "/packaging/resources/config.ini.example"));
        QVERIFY(example.open(QIODevice::ReadOnly));
        const QByteArray text = example.readAll();
        QFile target(paths.configIni());
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write(text);
        target.close();

        screens::LoginFields f;
        f.url = QStringLiteral("https://books.example.com");
        f.username = QStringLiteral("reader");
        f.password = QStringLiteral("pa;ss,wo=rd # not a comment");
        QVERIFY(screens::writeLoginFields(paths, f).ok());

        screens::LoginFields back;
        QVERIFY(screens::readLoginFields(paths.configIni(), &back).ok());
        QCOMPARE(back.url, f.url);
        QCOMPARE(back.username, f.username);
        QCOMPARE(back.password, f.password);
        // The existing reader (task 11) agrees: same URL, username and the MD5 of the password.
        ClientConfig config;
        QVERIFY(readClientConfig(paths.configIni(), &config).ok());
        QCOMPARE(config.serverUrl.toString(), QStringLiteral("https://books.example.com"));
        QCOMPARE(config.username, QStringLiteral("reader"));
        QCOMPARE(config.keyMd5Hex, md5Hex(f.password));
        // Comments from the example survive.
        QFile after(paths.configIni());
        QVERIFY(after.open(QIODevice::ReadOnly));
        const QByteArray written = after.readAll();
        QVERIFY(written.contains("; Smalt configuration."));
        QVERIFY(written.contains("[koreader]"));
        // No temporary file is left behind.
        QCOMPARE(
                QDir(paths.addsDir()).entryList(QStringList() << "config.ini*", QDir::Files).size(),
                1);

        // Changing one field keeps the others.
        f.username = QStringLiteral("someone-else");
        QVERIFY(screens::writeLoginFields(paths, f).ok());
        QVERIFY(screens::readLoginFields(paths.configIni(), &back).ok());
        QCOMPARE(back.username, QStringLiteral("someone-else"));
        QCOMPARE(back.password, f.password);

        // A missing file is created from nothing.
        QTemporaryDir other;
        const Paths fresh = Paths::forRoot(other.path());
        QVERIFY(screens::writeLoginFields(fresh, f).ok());
        QVERIFY(screens::readLoginFields(fresh.configIni(), &back).ok());
        QCOMPARE(back.url, f.url);

        QVERIFY(screens::isValidServerUrl(QStringLiteral("https://x.example")));
        QVERIFY(screens::isValidServerUrl(QStringLiteral("http://192.168.1.5:8080")));
        QVERIFY(!screens::isValidServerUrl(QStringLiteral("books.example.com")));
        QVERIFY(!screens::isValidServerUrl(QStringLiteral("ftp://x")));
    }

    // The login fields on the screen write config.ini and reconfigure the client.
    void theLoginScreenWritesConfigAndReconfiguresTheClient()
    {
        Env env(DeviceModel::ClaraColour);
        Shell *shell = env.makeShell();
        shell->openSettings();
        tap(goToRow(shell, QStringLiteral("Server URL")));
        answerPrompt(shell, QStringLiteral("not a url"));
        QVERIFY(shell->dialogOpen()); // "Enter the full address ..."
        tap(findFirst<Button>(shell->dialogWidget()));
        QVERIFY(!QFile::exists(env.paths.configIni()));

        tap(goToRow(shell, QStringLiteral("Server URL")));
        answerPrompt(shell, QStringLiteral("https://books.example.com"));
        tap(goToRow(shell, QStringLiteral("KOReader username")));
        answerPrompt(shell, QStringLiteral("reader"));
        tap(goToRow(shell, QStringLiteral("KOReader password")));
        answerPrompt(shell, QStringLiteral("secret"));
        screens::LoginFields f;
        QVERIFY(screens::readLoginFields(env.paths.configIni(), &f).ok());
        QCOMPARE(f.url, QStringLiteral("https://books.example.com"));
        QCOMPARE(f.username, QStringLiteral("reader"));
        QCOMPARE(f.password, QStringLiteral("secret"));
        // The client got the new login; it holds only the MD5 of the password.
        QCOMPARE(env.fake.lastConfig.serverUrl.toString(),
                 QStringLiteral("https://books.example.com"));
        QCOMPARE(env.fake.lastConfig.username, QStringLiteral("reader"));
        QCOMPARE(env.fake.lastConfig.keyMd5Hex, md5Hex(QStringLiteral("secret")));
        // The password is never shown; the row says it is set.
        QCOMPARE(goToRow(shell, QStringLiteral("KOReader password"))->value(),
                 QStringLiteral("Set"));
        // An empty password entry keeps the saved one.
        tap(goToRow(shell, QStringLiteral("KOReader password")));
        answerPrompt(shell, QString());
        QVERIFY(screens::readLoginFields(env.paths.configIni(), &f).ok());
        QCOMPARE(f.password, QStringLiteral("secret"));
        delete shell;
    }

    void testConnectionReportsWhatTheServerSaid()
    {
        Env env(DeviceModel::ClaraColour);
        screens::LoginFields f;
        f.url = QStringLiteral("https://books.example.com");
        f.username = QStringLiteral("reader");
        f.password = QStringLiteral("secret");
        QVERIFY(screens::writeLoginFields(env.paths, f).ok());
        Shell *shell = env.makeShell();
        shell->openSettings();
        auto *settings = qobject_cast<SettingsScreen *>(shell->currentScreen());
        QVERIFY(settings);
        column(shell)->column()->setPage(0);

        tap(buttonNamed(settings, QStringLiteral("testConnection")));
        QCOMPARE(settings->testMessage(), QStringLiteral("Connected to BookOrbit 9.9.9-fake."));

        env.fake.authError = ErrorKind::AuthRejected;
        tap(buttonNamed(settings, QStringLiteral("testConnection")));
        QCOMPARE(settings->testMessage(),
                 QStringLiteral("BookOrbit didn't accept your KOReader login. Check it in "
                                "BookOrbit's Settings > KOReader."));

        env.fake.authError = ErrorKind::None;
        env.fake.offline = true;
        tap(buttonNamed(settings, QStringLiteral("testConnection")));
        QCOMPARE(settings->testMessage(), QStringLiteral("Couldn't reach BookOrbit."));
        delete shell;
    }

    // Against the mock: right credentials work, wrong ones are refused.
    void testConnectionAgainstTheMockServer()
    {
        ::testing::MockServer server;
        const QString error = server.start();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        std::unique_ptr<BookOrbitClient> client = createBookOrbitClient(server.clientConfig());
        Env env(DeviceModel::ClaraColour, nullptr, client.get());
        env.nickel->simulateNetwork(true);
        screens::LoginFields f;
        f.url = server.url().toString();
        f.username = ::testing::mockUser();
        f.password = QStringLiteral("wrong-password");
        QVERIFY(screens::writeLoginFields(env.paths, f).ok());
        Shell *shell = env.makeShell();
        shell->openSettings();
        auto *settings = qobject_cast<SettingsScreen *>(shell->currentScreen());
        column(shell)->column()->setPage(0);
        tap(buttonNamed(settings, QStringLiteral("testConnection")));
        QTRY_VERIFY_WITH_TIMEOUT(
                settings->testMessage().startsWith(QStringLiteral("BookOrbit didn't")), 10000);

        f.password = ::testing::mockPassword();
        QVERIFY(screens::writeLoginFields(env.paths, f).ok());
        tap(buttonNamed(settings, QStringLiteral("testConnection")));
        QTRY_COMPARE_WITH_TIMEOUT(settings->testMessage(),
                                  QStringLiteral("Connected to BookOrbit 9.9.9-mock."), 10000);
        delete shell;
    }

    void exportLogCopiesTheLogToTheUserPartition()
    {
        Env env(DeviceModel::ClaraColour);
        log::init(env.paths.logsDir());
        log::info("test", QStringLiteral("a line for the export"));
        Shell *shell = env.makeShell();
        shell->openSettings();
        tap(goToRow(shell, QStringLiteral("Export log")));
        QVERIFY(shell->dialogOpen()); // "Log saved to smalt-log.txt"
        QFile out(env.paths.onboard() + QStringLiteral("/smalt-log.txt"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QVERIFY(out.readAll().contains("a line for the export"));
        delete shell;
        log::shutdown();
    }
};

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
