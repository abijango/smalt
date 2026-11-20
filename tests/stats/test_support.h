// Helpers for the task 23 tests: a real ledger in a temp dir with reading
// written the way the recorder writes it, the dashboard fixture, a fake sync
// engine and golden image comparison.
#pragma once

#include "bookorbit/dashboard.h"
#include "kit/theme.h"
#include "smalt/ledger.h"
#include "smalt/paths.h"
#include "smalt/sync.h"
#include "stats/aggregate.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace statstest {

using namespace smalt;

inline QDateTime utc(int y, int mo, int d, int h = 12, int mi = 0, int s = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s), Qt::UTC);
}

inline qint64 unixSecs(const QDateTime &t)
{
    return t.toMSecsSinceEpoch() / 1000;
}

// The date every fixture is built around: Friday 20 November 2026, noon UTC.
inline QDateTime fixedNow()
{
    return utc(2026, 11, 20, 12, 0, 0);
}

inline PageEventRecord pageEvent(const QString &session, const QString &book, int page,
                                 const QDateTime &start, int seconds, int totalPages = 300)
{
    PageEventRecord e;
    e.sessionUuid = session;
    e.contentId = book;
    e.page = page;
    e.totalPages = totalPages;
    e.startUtc = start;
    e.durationSeconds = seconds;
    return e;
}

inline QJsonObject loadFixtureObject(const QString &name)
{
    QFile f(QStringLiteral(SMALT_STATS_FIXTURE_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

// Pins the bundled "Smalt Test Sans" (the UI kit's test font) so text renders the
// same on every machine.
inline bool pinTestFont()
{
    const QString dir = QStringLiteral(SMALT_KIT_FONT_DIR "/");
    const int a =
            QFontDatabase::addApplicationFont(dir + QStringLiteral("SmaltTestSans-Regular.ttf"));
    const int b = QFontDatabase::addApplicationFont(dir + QStringLiteral("SmaltTestSans-Bold.ttf"));
    if (a < 0 || b < 0)
        return false;
    ui::kit::setFontFamily(QStringLiteral("Smalt Test Sans"));
    return true;
}

// A ledger in its own temp dir, open.
class LedgerFixture
{
public:
    LedgerFixture()
    {
        if (!m_dir.isValid())
            return;
        m_ledger = createLedger(Paths::forRoot(m_dir.path()));
        m_ok = m_ledger->open().ok();
    }

    bool ok() const { return m_ok; }

    Ledger *ledger() const { return m_ledger.get(); }

    // Links a book so its page events can be confirmed through the watermark.
    void link(const QString &contentId, qint64 bookId)
    {
        BookMapEntry e;
        e.contentId = contentId;
        e.bookId = bookId;
        e.bookFileId = bookId * 10;
        e.createdUtc = utc(2026, 1, 1);
        m_ledger->putBookMap(e);
    }

    // A finished session of `pages` pages of `secondsPerPage` each, back to
    // back from `start`. Returns the session.
    SessionRecord read(const QString &book, const QDateTime &start, int pages, int secondsPerPage,
                       int firstPage = 10, int totalPages = 300)
    {
        SessionRecord s;
        s.contentId = book;
        s.startUtc = start;
        s.startPage = firstPage;
        s.start.percent = 100.0 * firstPage / totalPages;
        if (!m_ledger->beginSession(&s).ok())
            return SessionRecord();
        for (int i = 0; i < pages; ++i)
            m_ledger->appendPageEvent(pageEvent(s.uuid, book, firstPage + i,
                                                start.addSecs(qint64(i) * secondsPerPage),
                                                secondsPerPage, totalPages));
        s.endUtc = start.addSecs(qint64(pages) * secondsPerPage);
        s.activeSeconds = qint64(pages) * secondsPerPage;
        s.endPage = firstPage + pages - 1;
        s.end.percent = 100.0 * s.endPage / totalPages;
        s.pagesTurned = pages;
        s.totalPages = totalPages;
        m_ledger->finishSession(s);
        SessionRecord out;
        m_ledger->session(s.uuid, &out);
        return out;
    }

    // An open session with the given page events written so far.
    SessionRecord beginOpen(const QString &book, const QDateTime &start)
    {
        SessionRecord s;
        s.contentId = book;
        s.startUtc = start;
        s.totalPages = 300;
        m_ledger->beginSession(&s);
        return s;
    }

    void cacheDashboard(const QJsonObject &body, const QDateTime &fetched)
    {
        QVERIFY(smalt::cacheDashboard(m_ledger.get(), body, fetched).ok());
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Ledger> m_ledger;
    bool m_ok = false;
};

// A sync engine that only emits finished().
class FakeSync : public SyncEngine
{
public:
    void request(const SyncRequest &) override { }

    void abort() override { }

    SyncStatus status() const override { return SyncStatus(); }

    void finish(SyncOutcome outcome = SyncOutcome::Succeeded)
    {
        SyncResult r;
        r.outcome = outcome;
        emit finished(r);
    }
};

// Compares with tests/stats/golden/<name>.png; SMALT_UPDATE_GOLDENS=1 writes it.
inline QString compareGolden(const QImage &actual, const QString &name)
{
    const QString path = QStringLiteral(SMALT_STATS_GOLDEN_DIR "/") + name + QStringLiteral(".png");
    const QImage rgb = actual.convertToFormat(QImage::Format_RGB32);
    if (qgetenv("SMALT_UPDATE_GOLDENS") == "1") {
        QDir().mkpath(QFileInfo(path).absolutePath());
        // A greyscale rendering (Clara BW) is stored as 8-bit grey, which is lossless and
        // much smaller; colour ones stay truecolour so they can be reviewed as they are.
        bool grey = true;
        for (int y = 0; y < rgb.height() && grey; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            for (int x = 0; x < rgb.width(); ++x)
                if (qRed(line[x]) != qGreen(line[x]) || qGreen(line[x]) != qBlue(line[x])) {
                    grey = false;
                    break;
                }
        }
        const QImage out = grey ? rgb.convertToFormat(QImage::Format_Grayscale8) : rgb;
        if (!out.save(path, "PNG", 0))
            return QStringLiteral("cannot write ") + path;
        return QString();
    }
    QImage golden(path);
    if (golden.isNull())
        return QStringLiteral("missing golden %1 (run the stats_update_goldens target)").arg(path);
    golden = golden.convertToFormat(QImage::Format_RGB32);
    if (golden.size() != rgb.size())
        return QStringLiteral("%1: size %2x%3, golden %4x%5")
                .arg(name)
                .arg(rgb.width())
                .arg(rgb.height())
                .arg(golden.width())
                .arg(golden.height());
    // Tolerate antialiasing noise from other Qt/FreeType builds, not layout changes.
    qint64 different = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *a = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        const QRgb *g = reinterpret_cast<const QRgb *>(golden.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            if (qAbs(qRed(a[x]) - qRed(g[x])) > 40 || qAbs(qGreen(a[x]) - qGreen(g[x])) > 40
                || qAbs(qBlue(a[x]) - qBlue(g[x])) > 40)
                ++different;
        }
    }
    const double fraction = double(different) / (double(rgb.width()) * rgb.height());
    if (fraction > 0.002) {
        const QString out =
                QDir::temp().filePath(name.split('/').last() + QStringLiteral("-actual.png"));
        rgb.save(out);
        return QStringLiteral("%1: %2% of pixels differ from the golden (actual saved to %3)")
                .arg(name)
                .arg(fraction * 100)
                .arg(out);
    }
    return QString();
}

} // namespace statstest
