#include "smalt/log.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstLog : public QObject
{
    Q_OBJECT

private slots:

    void cleanup() { smalt::log::shutdown(); }

    void writesLinesToTheLogFolder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString dir = root.path() + QStringLiteral("/.adds/bookorbit/logs");
        smalt::log::init(dir); // Creates the folder lazily on the writer thread.
        smalt::log::info("test", QStringLiteral("hello"));
        smalt::log::warn("test", QStringLiteral("café"));
        smalt::log::flush();

        QCOMPARE(smalt::log::currentFile(), dir + QStringLiteral("/smalt.log"));
        QFile file(dir + QStringLiteral("/smalt.log"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = file.readAll().split('\n');
        QCOMPARE(lines.size(), 3); // Two lines plus the empty tail.
        QVERIFY(lines.at(0).endsWith("INFO  [test] hello"));
        QVERIFY(lines.at(1).endsWith(QStringLiteral("WARN  [test] café").toUtf8()));
        QVERIFY(lines.at(0).contains('T') && lines.at(0).contains("Z "));
    }

    void dropsLinesBelowTheMinimumLevel()
    {
        QTemporaryDir root;
        smalt::log::init(root.path());
        smalt::log::setMinimumLevel(smalt::log::Level::Warn);
        smalt::log::info("test", QStringLiteral("quiet"));
        smalt::log::error("test", QStringLiteral("loud"));
        smalt::log::flush();
        smalt::log::setMinimumLevel(smalt::log::Level::Info);

        QFile file(root.path() + QStringLiteral("/smalt.log"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(!text.contains("quiet"));
        QVERIFY(text.contains("loud"));
    }

    void rotatesAtAboutOneMegabyte()
    {
        QTemporaryDir root;
        smalt::log::init(root.path());
        const QString chunk(1000, QLatin1Char('x'));
        for (int i = 0; i < 1100; ++i)
            smalt::log::info("test", chunk);
        smalt::log::flush();
        smalt::log::info("test", QStringLiteral("after rotation"));
        smalt::log::flush();

        QVERIFY(QFile::exists(root.path() + QStringLiteral("/smalt.log.1")));
        QFile current(root.path() + QStringLiteral("/smalt.log"));
        QVERIFY(current.open(QIODevice::ReadOnly));
        QVERIFY(current.size() < 1024 * 1024);
        QVERIFY(current.readAll().contains("after rotation"));
    }

    void unwritableFolderNeverFailsTheCaller()
    {
        QTemporaryDir root;
        const QString blocker = root.path() + QStringLiteral("/not-a-dir");
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        smalt::log::init(blocker
                         + QStringLiteral("/logs")); // A file sits where the folder should be.
        smalt::log::error("test", QStringLiteral("goes nowhere"));
        smalt::log::flush(); // Must return, not hang or crash.
        QVERIFY(!QDir(blocker + QStringLiteral("/logs")).exists());
    }

    void beforeInitLinesGoToStderrOnly()
    {
        smalt::log::shutdown();
        QVERIFY(smalt::log::currentFile().isEmpty());
        smalt::log::info("test", QStringLiteral("stderr only"));
        smalt::log::flush(); // No-op when not running.
    }
};

QTEST_GUILESS_MAIN(TstLog)
#include "tst_log.moc"
