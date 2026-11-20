// Settings and Sync status.
#include "bookorbit/client_config.h"
#include "copy.h"
#include "login_config.h"
#include "pages_common.h"
#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/nickel.h"
#include "smalt/settings.h"
#include "smalt/version.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

namespace smalt {
namespace screens {

namespace {

const int kBatteryChoices[6] = { 0, 10, 20, 30, 40, 50 };

QString pushModeLabel(LivePushMode m)
{
    switch (m) {
    case LivePushMode::Off:
        return QStringLiteral("Off");
    case LivePushMode::ConnectBriefly:
        return QStringLiteral("Connect briefly");
    case LivePushMode::OnlyWhenConnected:
    default:
        return QStringLiteral("Only when connected");
    }
}

QString minutesLabel(int minutes)
{
    return QStringLiteral("%1 minutes").arg(minutes);
}

QString sleepLabel(int seconds)
{
    if (seconds == kNewSessionAfterSleepNever)
        return QStringLiteral("Never");
    if (seconds < 3600)
        return QStringLiteral("%1 minutes").arg(seconds / 60);
    const int hours = seconds / 3600;
    return hours == 1 ? QStringLiteral("1 hour") : QStringLiteral("%1 hours").arg(hours);
}

QString onOff(bool on)
{
    return on ? QStringLiteral("On") : QStringLiteral("Off");
}

} // namespace

// ---- Settings ----

SettingsScreen::SettingsScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent) { }

QWidget *SettingsScreen::rowWith(const QString &title, const QString &value, const char *slot)
{
    TextRow *row = new TextRow(shell()->look(), title, value, column());
    connect(row, SIGNAL(tapped()), this, slot);
    return row;
}

QWidget *SettingsScreen::toggleRow(const QString &title, bool value, const char *key)
{
    TextRow *row = new TextRow(shell()->look(), title, onOff(value), column());
    row->setProperty("settingKey", QLatin1String(key));
    connect(row, SIGNAL(tapped()), this, SLOT(onToggle()));
    return row;
}

QList<QWidget *> SettingsScreen::buildBlocks()
{
    Shell *sh = shell();
    ui::ComponentFactory *kit = sh->look().kit();
    QWidget *parent = column();
    QList<QWidget *> out;
    const Settings s = sh->deps().settings ? sh->deps().settings->current() : Settings();
    LoginFields login;
    readLoginFields(sh->deps().paths.configIni(), &login);

    // The login lives in config.ini, not in the settings file.
    out << kit->sectionHeader(QStringLiteral("BookOrbit"), 3, parent);
    const QUrl url(login.url);
    out << rowWith(QStringLiteral("Server URL"),
                   login.url.isEmpty() ? QStringLiteral("Not set") : login.url,
                   SLOT(onServerUrl()));
    out << rowWith(QStringLiteral("KOReader username"),
                   login.username.isEmpty() ? QStringLiteral("Not set") : login.username,
                   SLOT(onUsername()));
    out << rowWith(QStringLiteral("KOReader password"),
                   login.password.isEmpty() ? QStringLiteral("Not set") : QStringLiteral("Set"),
                   SLOT(onPassword()));
    ButtonStack *test = new ButtonStack(sh->look(), parent);
    QWidget *testButton = kit->outlineButton(QStringLiteral("Test connection"), test);
    testButton->setObjectName(QStringLiteral("testConnection"));
    connect(testButton, SIGNAL(tapped()), this, SLOT(onTestConnection()));
    test->add(testButton);
    out << test;
    if (!m_testMessage.isEmpty())
        out << textBlock(sh, m_testMessage, TextBlock::Secondary, parent);

    out << kit->sectionHeader(QStringLiteral("Sync"), 6, parent);
    out << toggleRow(QStringLiteral("Sync when a book closes"), s.syncOnBookClose,
                     "syncOnBookClose");
    out << toggleRow(QStringLiteral("Sync before sleep"), s.syncBeforeSleep, "syncBeforeSleep");
    out << toggleRow(QStringLiteral("Sync on wake"), s.syncOnWake, "syncOnWake");
    out << toggleRow(QStringLiteral("Sync when Wi-Fi is on"), s.syncWhenWifiOn, "syncWhenWifiOn");
    out << rowWith(QStringLiteral("Skip pre-sleep sync below"),
                   s.preSleepMinBatteryPercent == 0
                           ? QStringLiteral("Never skip")
                           : QStringLiteral("%1%").arg(s.preSleepMinBatteryPercent),
                   SLOT(onBattery()));
    out << rowWith(QStringLiteral("Push while reading"), pushModeLabel(s.livePushMode),
                   SLOT(onPushMode()));
    if (s.livePushMode != LivePushMode::Off)
        out << rowWith(QStringLiteral("Push every"), minutesLabel(s.livePushMinutes),
                       SLOT(onPushInterval()));

    out << kit->sectionHeader(QStringLiteral("Reading"), 1, parent);
    out << rowWith(QStringLiteral("New session after sleep"),
                   sleepLabel(s.newSessionAfterSleepSeconds), SLOT(onNewSession()));

    out << kit->sectionHeader(QStringLiteral("Collections"), 3, parent);
    out << toggleRow(QStringLiteral("Mirror collections"), s.mirrorCollections,
                     "mirrorCollections");
    out << toggleRow(QStringLiteral("Keep an Abandoned collection"), s.abandonedCollection,
                     "abandonedCollection");
    out << toggleRow(QStringLiteral("Keep a Want to read collection"), s.wantToReadCollection,
                     "wantToReadCollection");

    out << kit->sectionHeader(QStringLiteral("Downloads and support"), 2, parent);
    out << rowWith(QStringLiteral("Download folder"),
                   s.downloadFolder.isEmpty() ? QStringLiteral("BookOrbit (default)")
                                              : s.downloadFolder,
                   SLOT(onDownloadFolder()));
    out << rowWith(QStringLiteral("Export log"), QString(), SLOT(onExportLog()));
    return out;
}

void SettingsScreen::save(const Settings &s)
{
    Shell *sh = shell();
    if (!sh->deps().settings) {
        sh->showMessage(QStringLiteral("Settings can't be saved on this build."));
        return;
    }
    const Status st = sh->deps().settings->save(s);
    if (!st.ok()) {
        log::warn("screens", QStringLiteral("settings not saved: %1").arg(st.message));
        sh->showMessage(QStringLiteral("Couldn't save the settings."));
    }
    rebuild();
}

void SettingsScreen::onToggle()
{
    QObject *row = sender();
    Shell *sh = shell();
    if (!row || !sh->deps().settings)
        return;
    Settings s = sh->deps().settings->current();
    const QString key = row->property("settingKey").toString();
    if (key == QLatin1String("syncOnBookClose"))
        s.syncOnBookClose = !s.syncOnBookClose;
    else if (key == QLatin1String("syncBeforeSleep"))
        s.syncBeforeSleep = !s.syncBeforeSleep;
    else if (key == QLatin1String("syncOnWake"))
        s.syncOnWake = !s.syncOnWake;
    else if (key == QLatin1String("syncWhenWifiOn"))
        s.syncWhenWifiOn = !s.syncWhenWifiOn;
    else if (key == QLatin1String("mirrorCollections"))
        s.mirrorCollections = !s.mirrorCollections;
    else if (key == QLatin1String("abandonedCollection"))
        s.abandonedCollection = !s.abandonedCollection;
    else if (key == QLatin1String("wantToReadCollection"))
        s.wantToReadCollection = !s.wantToReadCollection;
    else
        return;
    save(s);
}

void SettingsScreen::onBattery()
{
    Shell *sh = shell();
    if (!sh->deps().settings)
        return;
    const Settings s = sh->deps().settings->current();
    QStringList options;
    int current = -1;
    for (int i = 0; i < 6; ++i) {
        options << (kBatteryChoices[i] == 0 ? QStringLiteral("Never skip")
                                            : QStringLiteral("%1%").arg(kBatteryChoices[i]));
        if (kBatteryChoices[i] == s.preSleepMinBatteryPercent)
            current = i;
    }
    QPointer<SettingsScreen> self(this);
    sh->openChoice(QStringLiteral("Skip pre-sleep sync below"), options, current, [self](int i) {
        if (!self || i < 0 || i >= 6)
            return;
        Settings next = self->shell()->deps().settings->current();
        next.preSleepMinBatteryPercent = kBatteryChoices[i];
        self->save(next);
    });
}

void SettingsScreen::onPushMode()
{
    Shell *sh = shell();
    if (!sh->deps().settings)
        return;
    const Settings s = sh->deps().settings->current();
    const LivePushMode modes[3] = { LivePushMode::Off, LivePushMode::OnlyWhenConnected,
                                    LivePushMode::ConnectBriefly };
    QStringList options;
    int current = 0;
    for (int i = 0; i < 3; ++i) {
        options << pushModeLabel(modes[i]);
        if (modes[i] == s.livePushMode)
            current = i;
    }
    QPointer<SettingsScreen> self(this);
    sh->openChoice(QStringLiteral("Push while reading"), options, current, [self, modes](int i) {
        if (!self || i < 0 || i >= 3)
            return;
        Settings next = self->shell()->deps().settings->current();
        next.livePushMode = modes[i];
        // A mode brings its own default interval; the interval row can change it.
        if (modes[i] == LivePushMode::OnlyWhenConnected)
            next.livePushMinutes = kDefaultLivePushMinutes;
        else if (modes[i] == LivePushMode::ConnectBriefly)
            next.livePushMinutes = kDefaultConnectBrieflyMinutes;
        self->save(next);
    });
}

void SettingsScreen::onPushInterval()
{
    Shell *sh = shell();
    if (!sh->deps().settings)
        return;
    const Settings s = sh->deps().settings->current();
    QStringList options;
    int current = -1;
    for (int i = 0; i < 4; ++i) {
        options << minutesLabel(kLivePushChoicesMinutes[i]);
        if (kLivePushChoicesMinutes[i] == s.livePushMinutes)
            current = i;
    }
    QPointer<SettingsScreen> self(this);
    sh->openChoice(QStringLiteral("Push every"), options, current, [self](int i) {
        if (!self || i < 0 || i >= 4)
            return;
        Settings next = self->shell()->deps().settings->current();
        next.livePushMinutes = kLivePushChoicesMinutes[i];
        self->save(next);
    });
}

void SettingsScreen::onNewSession()
{
    Shell *sh = shell();
    if (!sh->deps().settings)
        return;
    const Settings s = sh->deps().settings->current();
    QStringList options;
    int current = -1;
    for (int i = 0; i < 6; ++i) {
        options << sleepLabel(kNewSessionAfterSleepChoicesSeconds[i]);
        if (kNewSessionAfterSleepChoicesSeconds[i] == s.newSessionAfterSleepSeconds)
            current = i;
    }
    QPointer<SettingsScreen> self(this);
    sh->openChoice(QStringLiteral("New session after sleep"), options, current, [self](int i) {
        if (!self || i < 0 || i >= 6)
            return;
        Settings next = self->shell()->deps().settings->current();
        next.newSessionAfterSleepSeconds = kNewSessionAfterSleepChoicesSeconds[i];
        self->save(next);
    });
}

void SettingsScreen::onDownloadFolder()
{
    Shell *sh = shell();
    if (!sh->deps().settings)
        return;
    QPointer<SettingsScreen> self(this);
    sh->promptText(QStringLiteral("Download folder (empty for the default)"),
                   sh->deps().settings->current().downloadFolder, false,
                   [self](const QString &text, bool accepted) {
                       if (!self || !accepted)
                           return;
                       Settings next = self->shell()->deps().settings->current();
                       next.downloadFolder = text.trimmed();
                       self->save(next);
                   });
}

void SettingsScreen::onExportLog()
{
    Shell *sh = shell();
    const Paths &paths = sh->deps().paths;
    log::flush();
    QByteArray all;
    const QString dir = paths.logsDir();
    const QStringList names = QStringList()
            << QStringLiteral("smalt.log.1") << QStringLiteral("smalt.log");
    bool any = false;
    for (const QString &name : names) {
        QFile f(dir + QLatin1Char('/') + name);
        if (f.open(QIODevice::ReadOnly)) {
            all += f.readAll();
            any = true;
        }
    }
    const QString fileName = QStringLiteral("smalt-log.txt");
    if (!any) {
        sh->showMessage(copy::exportLogFailed());
        return;
    }
    QSaveFile out(paths.onboard() + QLatin1Char('/') + fileName);
    if (!out.open(QIODevice::WriteOnly)) {
        sh->showMessage(copy::exportLogFailed());
        return;
    }
    out.write(all);
    sh->showMessage(out.commit() ? copy::exportLogDone(fileName) : copy::exportLogFailed());
}

// ---- Login ----

ClientConfig SettingsScreen::clientConfigFor(const QString &url, const QString &user,
                                             const QString &password) const
{
    const ShellDeps &deps = shell()->deps();
    ClientConfig c;
    c.serverUrl = QUrl(url);
    c.username = user;
    c.keyMd5Hex = md5Hex(password);
    c.deviceId = deps.ledger ? deps.ledger->deviceId() : QString();
    c.deviceModel = shell()->look().profile().name;
    c.pluginVersion = QStringLiteral(SMALT_VERSION);
    c.caBundlePath = deps.paths.caBundle();
    return c;
}

void SettingsScreen::applyLogin()
{
    Shell *sh = shell();
    if (!sh->deps().client)
        return;
    LoginFields f;
    if (!readLoginFields(sh->deps().paths.configIni(), &f).ok())
        return;
    sh->deps().client->setConfig(clientConfigFor(f.url, f.username, f.password));
}

void SettingsScreen::onServerUrl()
{
    Shell *sh = shell();
    LoginFields f;
    readLoginFields(sh->deps().paths.configIni(), &f);
    QPointer<SettingsScreen> self(this);
    sh->promptText(QStringLiteral("Server URL"), f.url, false,
                   [self](const QString &text, bool accepted) {
                       if (!self || !accepted)
                           return;
                       Shell *s = self->shell();
                       if (!isValidServerUrl(text)) {
                           s->showMessage(copy::badServerUrl());
                           return;
                       }
                       LoginFields fields;
                       readLoginFields(s->deps().paths.configIni(), &fields);
                       fields.url = text.trimmed();
                       if (!writeLoginFields(s->deps().paths, fields).ok())
                           s->showMessage(QStringLiteral("Couldn't save the login."));
                       self->applyLogin();
                       self->m_testMessage.clear();
                       self->rebuild();
                   });
}

void SettingsScreen::onUsername()
{
    Shell *sh = shell();
    LoginFields f;
    readLoginFields(sh->deps().paths.configIni(), &f);
    QPointer<SettingsScreen> self(this);
    sh->promptText(QStringLiteral("KOReader username"), f.username, false,
                   [self](const QString &text, bool accepted) {
                       if (!self || !accepted)
                           return;
                       Shell *s = self->shell();
                       LoginFields fields;
                       readLoginFields(s->deps().paths.configIni(), &fields);
                       fields.username = text.trimmed();
                       if (!writeLoginFields(s->deps().paths, fields).ok())
                           s->showMessage(QStringLiteral("Couldn't save the login."));
                       self->applyLogin();
                       self->m_testMessage.clear();
                       self->rebuild();
                   });
}

void SettingsScreen::onPassword()
{
    Shell *sh = shell();
    QPointer<SettingsScreen> self(this);
    // The password is never shown; an empty entry keeps the one that is saved.
    sh->promptText(QStringLiteral("KOReader password"), QString(), true,
                   [self](const QString &text, bool accepted) {
                       if (!self || !accepted || text.isEmpty())
                           return;
                       Shell *s = self->shell();
                       LoginFields fields;
                       readLoginFields(s->deps().paths.configIni(), &fields);
                       fields.password = text;
                       if (!writeLoginFields(s->deps().paths, fields).ok())
                           s->showMessage(QStringLiteral("Couldn't save the login."));
                       self->applyLogin();
                       self->m_testMessage.clear();
                       self->rebuild();
                   });
}

void SettingsScreen::onTestConnection()
{
    Shell *sh = shell();
    BookOrbitClient *client = sh->deps().client;
    LoginFields f;
    readLoginFields(sh->deps().paths.configIni(), &f);
    if (f.url.isEmpty() || f.username.isEmpty() || f.password.isEmpty()) {
        m_testMessage = QStringLiteral("Fill in the server, username and password first.");
        rebuild();
        return;
    }
    if (!client) {
        m_testMessage = copy::syncNotAvailable();
        rebuild();
        return;
    }
    client->setConfig(clientConfigFor(f.url, f.username, f.password));
    m_testMessage = QStringLiteral("Testing the connection.");
    rebuild();
    QPointer<SettingsScreen> self(this);
    client->checkAuth([self, client](const Reply<bool> &auth) {
        if (!self)
            return;
        if (!auth.result.ok()) {
            self->m_testMessage = copy::errorText(auth.result.error);
            self->rebuild();
            self->shell()->catalog()->setOnline(auth.result.error != ErrorKind::Offline
                                                        && auth.result.error != ErrorKind::Timeout,
                                                auth.result.error);
            return;
        }
        // The login works; ask for the server's version for the message.
        client->capabilities([self, client](const Reply<QJsonObject> &) {
            if (!self)
                return;
            self->m_testMessage = copy::testConnectionOk(client->serverVersion());
            self->rebuild();
            self->shell()->catalog()->setOnline(true, ErrorKind::None);
        });
    });
}

// ---- Sync status ----

SyncStatusScreen::SyncStatusScreen(Shell *shell, QWidget *parent) : ColumnScreen(shell, parent)
{
    if (SyncEngine *sync = shell->deps().sync) {
        connect(sync, SIGNAL(progress(QString)), this, SLOT(onProgress(QString)));
        connect(sync, SIGNAL(finished(smalt::SyncResult)), this,
                SLOT(onFinished(smalt::SyncResult)));
        connect(sync, SIGNAL(statusChanged()), this, SLOT(onProgressRefresh()));
    }
}

QList<QWidget *> SyncStatusScreen::buildBlocks()
{
    Shell *sh = shell();
    QWidget *parent = column();
    QList<QWidget *> out;
    out << textBlock(sh, sh->statusLineText(), TextBlock::Body, parent);
    if (SyncEngine *sync = sh->deps().sync) {
        const SyncStatus st = sync->status();
        if (st.lastError != ErrorKind::None)
            out << textBlock(sh, copy::errorText(st.lastError), TextBlock::Secondary, parent);
        if (st.consecutiveFailures >= kToastAfterConsecutiveFailures)
            out << textBlock(sh, copy::repeatedFailure(), TextBlock::Secondary, parent);
        out << textBlock(sh,
                         st.waiting > 0 ? QStringLiteral("%1 waiting to sync.").arg(st.waiting)
                                        : QStringLiteral("Nothing is waiting to sync."),
                         TextBlock::Secondary, parent);
    }
    ButtonStack *stack = new ButtonStack(sh->look(), parent);
    QWidget *button = sh->look().kit()->primaryButton(QStringLiteral("Sync now"), stack);
    button->setObjectName(QStringLiteral("syncNow"));
    connect(button, SIGNAL(tapped()), this, SLOT(onSyncNow()));
    stack->add(button);
    out << stack;
    if (!m_message.isEmpty())
        out << textBlock(sh, m_message, TextBlock::Body, parent);
    return out;
}

void SyncStatusScreen::onSyncNow()
{
    m_message = shell()->deps().sync ? QStringLiteral("Syncing") : QString();
    shell()->syncNow();
    rebuild();
}

void SyncStatusScreen::onProgress(const QString &text)
{
    m_message = text;
    rebuild();
}

void SyncStatusScreen::onProgressRefresh()
{
    rebuild();
}

void SyncStatusScreen::onFinished(const smalt::SyncResult &result)
{
    if (result.trigger != SyncTrigger::Manual)
        return;
    switch (result.outcome) {
    case SyncOutcome::Succeeded:
        m_message = copy::manualSyncDone(result);
        break;
    case SyncOutcome::Partial:
        m_message = QStringLiteral("Sync was cut short. It will carry on later.");
        break;
    case SyncOutcome::Failed:
        m_message = result.error == ErrorKind::AuthRejected ? copy::keyRejected()
                                                            : copy::repeatedFailure();
        break;
    case SyncOutcome::Skipped:
        m_message.clear();
        break;
    }
    rebuild();
}

} // namespace screens
} // namespace smalt
