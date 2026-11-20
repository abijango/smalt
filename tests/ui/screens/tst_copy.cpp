// Acceptance test: the screens' copy matches the spec's Copy table and "Where
// sync status shows" exactly, read from docs/SPEC.md so the two can't drift.
#include "screens/copy.h"
#include "screens/pages.h"
#include "support.h"

using namespace screenstest;
using namespace smalt::ui::kit;

namespace {

// A sync engine that only reports a status and records requests.
class FakeSync : public SyncEngine
{
public:
    void request(const SyncRequest &r) override { requests.append(r); }

    void abort() override { }

    SyncStatus status() const override { return m_status; }

    void setStatus(const SyncStatus &s)
    {
        m_status = s;
        emit statusChanged();
    }

    QVector<SyncRequest> requests;

private:
    SyncStatus m_status;
};

QString specText()
{
    QFile f(QStringLiteral(SMALT_REPO_DIR "/docs/SPEC.md"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

} // namespace

class TstCopy : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase() { QVERIFY(pinTestFont()); }

    // The strings the spec gives are used exactly.
    void specStringsAreExact()
    {
        const QString spec = specText();
        QVERIFY(!spec.isEmpty());
        // Copy table.
        QVERIFY(spec.contains(screens::copy::repeatedFailure()));
        QCOMPARE(screens::copy::repeatedFailure(),
                 QStringLiteral(
                         "Couldn't reach BookOrbit. Your reading is saved and will sync later."));
        QVERIFY(spec.contains(screens::copy::keyRejected()));
        QVERIFY(spec.contains(screens::copy::removalBanner(3)));
        QCOMPARE(screens::copy::removalBanner(3),
                 QStringLiteral("3 books were removed from BookOrbit."));
        QVERIFY(spec.contains(QStringLiteral("**%1**").arg(screens::copy::removalReview())));

        SyncResult r;
        r.sessionsPushed = 2;
        r.highlightsPushed = 1;
        r.positionsPushed = 1;
        QVERIFY(spec.contains(screens::copy::manualSyncDone(r)));
        QCOMPARE(screens::copy::manualSyncDone(r),
                 QStringLiteral("Synced: 2 sessions, 1 highlight, 1 position"));

        // The Settings labels the spec names.
        const QStringList labels = QStringList() << "Test connection" << "Push while reading"
                                                 << "New session after sleep" << "Export log";
        for (const QString &label : labels)
            QVERIFY2(spec.contains(label), qPrintable(label));
    }

    void singularAndEmptyForms()
    {
        SyncResult r;
        QCOMPARE(screens::copy::manualSyncDone(r), QStringLiteral("Synced: nothing new to send"));
        r.sessionsPushed = 1;
        QCOMPARE(screens::copy::manualSyncDone(r), QStringLiteral("Synced: 1 session"));
        QCOMPARE(screens::copy::removalBanner(1),
                 QStringLiteral("1 book was removed from BookOrbit."));
        QCOMPARE(screens::copy::downloadingOf(3, 5), QStringLiteral("Downloading 3 of 5"));
    }

    // "Synced 10:42 · 3 waiting", or "Offline · 3 waiting".
    void statusLineForms()
    {
        SyncStatus s;
        s.online = true;
        s.waiting = 3;
        s.lastSuccessUtc = QDateTime(QDate(2026, 11, 19), QTime(10, 42), Qt::UTC);
        const QString clock = s.lastSuccessUtc.toLocalTime().toString(QStringLiteral("HH:mm"));
        QCOMPARE(screens::copy::statusLine(s, true),
                 QStringLiteral("Synced %1 · 3 waiting").arg(clock));
        s.online = false;
        QCOMPARE(screens::copy::statusLine(s, true), QStringLiteral("Offline · 3 waiting"));
        s.waiting = 0;
        QCOMPARE(screens::copy::statusLine(s, true), QStringLiteral("Offline"));
        s.online = true;
        s.lastSuccessUtc = QDateTime();
        QCOMPARE(screens::copy::statusLine(s, true), QStringLiteral("Not synced yet"));
    }

    // The header's status line follows the sync engine.
    void theShellShowsTheSyncStatus()
    {
        FakeSync sync;
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.deps.sync = &sync;
        SyncStatus st;
        st.online = false;
        st.waiting = 3;
        sync.setStatus(st);
        Shell *shell = env.makeShell();
        QCOMPARE(shell->statusLineText(), QStringLiteral("Offline · 3 waiting"));
        st.online = true;
        st.waiting = 0;
        st.lastSuccessUtc = QDateTime(QDate(2026, 11, 19), QTime(10, 42), Qt::UTC);
        sync.setStatus(st);
        QCOMPARE(
                shell->statusLineText(),
                QStringLiteral("Synced %1").arg(st.lastSuccessUtc.toLocalTime().toString("HH:mm")));
        delete shell;
    }

    // Sync status: "Sync now" asks the engine for a manual sync and shows the result.
    void syncNowShowsTheResult()
    {
        FakeSync sync;
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.deps.sync = &sync;
        Shell *shell = env.makeShell();
        shell->openSyncStatus();
        Button *syncNow = buttonNamed(shell->currentScreen(), QStringLiteral("syncNow"));
        QVERIFY(syncNow);
        tap(syncNow);
        QCOMPARE(sync.requests.size(), 1);
        QCOMPARE(int(sync.requests.first().trigger), int(SyncTrigger::Manual));
        SyncResult result;
        result.trigger = SyncTrigger::Manual;
        result.outcome = SyncOutcome::Succeeded;
        result.sessionsPushed = 2;
        result.highlightsPushed = 1;
        result.positionsPushed = 1;
        emit sync.finished(result);
        bool found = false;
        const QList<QWidget *> blocks =
                qobject_cast<ColumnScreen *>(shell->currentScreen())->column()->blocks();
        for (QWidget *w : blocks) {
            TextBlock *t = qobject_cast<TextBlock *>(w);
            found = found
                    || (t
                        && t->text()
                                == QLatin1String("Synced: 2 sessions, 1 highlight, 1 position"));
        }
        QVERIFY(found);
        delete shell;
    }

    // A rejected key and a failing sync use the spec's sentences.
    void failuresUseTheSpecsSentences()
    {
        QCOMPARE(screens::copy::errorText(ErrorKind::AuthRejected), screens::copy::keyRejected());
        FakeSync sync;
        Env env(DeviceModel::ClaraColour);
        env.seedCache();
        env.deps.sync = &sync;
        Shell *shell = env.makeShell();
        shell->openSyncStatus();
        SyncResult result;
        result.trigger = SyncTrigger::Manual;
        result.outcome = SyncOutcome::Failed;
        result.error = ErrorKind::AuthRejected;
        emit sync.finished(result);
        bool rejected = false;
        QList<QWidget *> blocks =
                qobject_cast<ColumnScreen *>(shell->currentScreen())->column()->blocks();
        for (QWidget *w : blocks) {
            TextBlock *t = qobject_cast<TextBlock *>(w);
            rejected = rejected || (t && t->text() == screens::copy::keyRejected());
        }
        QVERIFY(rejected);
        result.error = ErrorKind::Offline;
        emit sync.finished(result);
        bool unreachable = false;
        blocks = qobject_cast<ColumnScreen *>(shell->currentScreen())->column()->blocks();
        for (QWidget *w : blocks) {
            TextBlock *t = qobject_cast<TextBlock *>(w);
            unreachable = unreachable || (t && t->text() == screens::copy::repeatedFailure());
        }
        QVERIFY(unreachable);
        delete shell;
    }

    // The six BookOrbit statuses, in sentence case.
    void statusLabels()
    {
        QCOMPARE(screens::copy::statusLabel(ReadStatus::Unread), QStringLiteral("Unread"));
        QCOMPARE(screens::copy::statusLabel(ReadStatus::WantToRead),
                 QStringLiteral("Want to read"));
        QCOMPARE(screens::copy::statusLabel(ReadStatus::Reading), QStringLiteral("Reading"));
        QCOMPARE(screens::copy::statusLabel(ReadStatus::OnHold), QStringLiteral("On hold"));
        QCOMPARE(screens::copy::statusLabel(ReadStatus::Read), QStringLiteral("Read"));
        QCOMPARE(screens::copy::statusLabel(ReadStatus::Abandoned), QStringLiteral("Abandoned"));
        // The wire values BookOrbit accepts round-trip.
        for (ReadStatus s : { ReadStatus::Unread, ReadStatus::WantToRead, ReadStatus::Reading,
                              ReadStatus::OnHold, ReadStatus::Read, ReadStatus::Abandoned })
            QCOMPARE(int(screens::parseReadStatus(screens::readStatusWire(s))), int(s));
    }
};

QTEST_MAIN(TstCopy)
#include "tst_copy.moc"
