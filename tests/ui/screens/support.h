// Shared helpers for the screens' tests: a fake BookOrbit client with a
// deterministic library, an environment (temporary user partition, ledger,
// settings, FakeNickel), a pinned font, and golden screenshot comparison.
#pragma once

#include "fakes/fake_nickel.h"
#include "kit/components.h"
#include "screens/shell.h"
#include "smalt/bookorbit_client.h"
#include "smalt/ledger.h"
#include "smalt/screens.h"
#include "smalt/settings.h"

#include <QBuffer>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

namespace screenstest {

using namespace smalt;
using namespace smalt::screens;

inline bool pinTestFont()
{
    const QString dir = QStringLiteral(SMALT_KIT_TEST_DIR "/fonts/");
    const int a =
            QFontDatabase::addApplicationFont(dir + QStringLiteral("SmaltTestSans-Regular.ttf"));
    const int b = QFontDatabase::addApplicationFont(dir + QStringLiteral("SmaltTestSans-Bold.ttf"));
    if (a < 0 || b < 0)
        return false;
    ui::kit::setFontFamily(QStringLiteral("Smalt Test Sans"));
    return true;
}

// 60 books: six series of four, comics and PDFs among EPUBs, a few statuses.
inline QJsonObject fakeBook(int i)
{
    static const char *adj[] = { "Amber",  "Bitter", "Crimson", "Distant", "Eastern", "Fallen",
                                 "Golden", "Hollow", "Iron",    "Jade",    "Kindled", "Lunar" };
    static const char *noun[] = { "Harbour", "Orchard", "Cartographer", "Lantern", "Meridian",
                                  "Archive", "Tide",    "Garden",       "Signal",  "Station" };
    static const char *author[] = { "Ada Quill", "Bo Marsh",  "Cy Reed",
                                    "Di Vale",   "Eli Stone", "Fay North" };
    QJsonObject o;
    o.insert("id", i);
    o.insert("title", QStringLiteral("%1 %2").arg(adj[(i - 1) / 5 % 12], noun[(i * 7) % 10]));
    o.insert("authors", QJsonArray() << author[i % 6]);
    if (i <= 24) {
        o.insert("seriesId", (i - 1) / 4 + 1);
        o.insert("seriesName", QStringLiteral("The %1 Cycle").arg(adj[(i - 1) / 4 * 2]));
        o.insert("seriesIndex", (i - 1) % 4 + 1);
    } else {
        o.insert("seriesId", QJsonValue::Null);
        o.insert("seriesName", QJsonValue::Null);
        o.insert("seriesIndex", QJsonValue::Null);
    }
    QString status = "unread";
    QJsonValue progress = QJsonValue::Null;
    QJsonValue lastRead = QJsonValue::Null;
    if (i == 1 || i == 5 || i == 6)
        status = "read";
    if (i == 2) {
        status = "reading";
        progress = 45;
        lastRead = "2026-11-18T20:00:00.000Z";
    }
    if (i == 30 || i == 31) {
        status = "reading";
        progress = i == 30 ? 54 : 20;
        lastRead = i == 30 ? "2026-11-17T08:00:00.000Z" : "2026-11-10T08:00:00.000Z";
    }
    if (i == 40)
        status = "abandoned";
    if (i == 41)
        status = "want_to_read";
    o.insert("progressPercentage", progress);
    o.insert("lastReadAt", lastRead);
    o.insert("readStatus", status);
    const QString format = i % 7 == 0 ? "cbz" : (i % 11 == 0 ? "pdf" : "epub");
    o.insert("formats", QJsonArray() << format);
    o.insert("hasCover", true);
    o.insert("addedAt",
             QDateTime(QDate(2026, 1, 1).addDays(i), QTime(0, 0), Qt::UTC).toString(Qt::ISODate));
    return o;
}

inline QJsonArray fakeLibrary(int count = 60)
{
    QJsonArray a;
    for (int i = 1; i <= count; ++i)
        a.append(fakeBook(i));
    return a;
}

inline QByteArray fakeCoverPng(int id)
{
    QImage img(120, 180, QImage::Format_RGB32);
    img.fill(QColor::fromHsv((id * 47) % 360, 110, 205));
    QPainter p(&img);
    p.fillRect(QRect(0, 120, 120, 60), QColor::fromHsv((id * 47) % 360, 140, 150));
    p.fillRect(QRect(14, 18, 92, 6), QColor(255, 255, 255, 200));
    p.fillRect(QRect(14, 32, 60, 6), QColor(255, 255, 255, 200));
    p.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return bytes;
}

// A BookOrbit client that answers from memory, at once. `offline` makes every
// call fail as if there were no network.
class FakeClient : public BookOrbitClient
{
public:
    QJsonArray library = fakeLibrary();
    bool offline = false;
    int booksCalls = 0;
    int thumbnailCalls = 0;
    QStringList statusCalls;
    QVector<int> ratingCalls;
    QString serverVersionText = QStringLiteral("9.9.9-fake");
    ClientConfig lastConfig;
    int authCalls = 0;
    ErrorKind authError = ErrorKind::None;

    template <typename T>
    static Reply<T> fail(ErrorKind e)
    {
        Reply<T> r;
        r.result.error = e;
        r.result.message = QStringLiteral("fake failure");
        return r;
    }

    template <typename T>
    void answer(Callback<T> done, const T &value)
    {
        if (offline) {
            done(fail<T>(ErrorKind::Offline));
            return;
        }
        Reply<T> r;
        r.value = value;
        done(r);
    }

    void setConfig(const ClientConfig &config) override { lastConfig = config; }

    void cancel(RequestId) override { }

    void cancelAll() override { }

    RequestId checkAuth(Callback<bool> done) override
    {
        ++authCalls;
        if (!offline && authError != ErrorKind::None) {
            done(fail<bool>(authError));
            return 1;
        }
        answer<bool>(done, true);
        return 1;
    }

    RequestId capabilities(Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    bool hasCapability(const QString &) const override { return true; }

    bool capabilitiesKnown() const override { return true; }

    QString serverVersion() const override { return serverVersionText; }

    QString libraryVersion() const override { return QString(); }

    RequestId matchCheck(const QVector<MatchQuery> &, Callback<QVector<MatchResult>> done) override
    {
        answer<QVector<MatchResult>>(done, QVector<MatchResult>());
        return 1;
    }

    RequestId getProgress(const QString &, Callback<RemoteProgress> done) override
    {
        answer<RemoteProgress>(done, RemoteProgress());
        return 1;
    }

    RequestId putProgress(const RemoteProgress &, Callback<bool> done) override
    {
        answer<bool>(done, true);
        return 1;
    }

    RequestId bulkProgress(const QJsonArray &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId uploadPageStats(const QVector<PageStatsBook> &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId sendSweep(const QJsonObject &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId exchangeAnnotations(const QString &, const QJsonArray &, const QJsonArray &, bool,
                                  const QString &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId ackAnnotations(const QString &, const QJsonObject &, Callback<bool> done) override
    {
        answer<bool>(done, true);
        return 1;
    }

    RequestId bookStates(const QJsonArray &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId setReadStatus(qint64, const QString &status, Callback<bool> done) override
    {
        statusCalls << status;
        answer<bool>(done, true);
        return 1;
    }

    RequestId setRating(qint64, int rating, Callback<bool> done) override
    {
        ratingCalls << rating;
        answer<bool>(done, true);
        return 1;
    }

    RequestId catalogRoot(Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId dashboard(Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId dashboardSection(const QString &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId section(const QString &kind, int, Callback<QJsonObject> done) override
    {
        QJsonObject o;
        QJsonArray items;
        if (kind == QLatin1String("collections")) {
            QJsonObject c;
            c.insert("id", "collection-7");
            c.insert("title", "Summer reading");
            items.append(c);
        }
        o.insert("items", items);
        answer<QJsonObject>(done, o);
        return 1;
    }

    RequestId books(const BookQuery &query, Callback<QJsonObject> done) override
    {
        ++booksCalls;
        QJsonArray matches;
        for (const QJsonValue &v : library) {
            const QJsonObject o = v.toObject();
            if (!query.q.isEmpty()) {
                QString hay = o.value("title").toString();
                const QJsonArray authors = o.value("authors").toArray();
                for (const QJsonValue &a : authors)
                    hay += QLatin1Char(' ') + a.toString();
                if (!hay.contains(query.q, Qt::CaseInsensitive))
                    continue;
            }
            if (query.seriesId > 0 && o.value("seriesId").toInt() != query.seriesId)
                continue;
            matches.append(o);
        }
        const int size = query.size > 0 ? query.size : 20;
        const int page = qMax(1, query.page);
        QJsonArray window;
        for (int i = (page - 1) * size; i < matches.size() && i < page * size; ++i)
            window.append(matches.at(i));
        QJsonObject o;
        o.insert("items", window);
        o.insert("total", matches.size());
        o.insert("page", page);
        o.insert("size", size);
        o.insert("hasNext", page * size < matches.size());
        answer<QJsonObject>(done, o);
        return 1;
    }

    RequestId book(qint64 bookId, Callback<QJsonObject> done) override
    {
        for (const QJsonValue &v : library) {
            QJsonObject o = v.toObject();
            if (o.value("id").toInt() != bookId)
                continue;
            o.insert("description",
                     QStringLiteral("About %1. A long description that wraps across several lines "
                                    "so the book page has a paragraph of text to lay out.")
                             .arg(o.value("title").toString()));
            o.insert("publishedYear", 2020);
            o.insert("pageCount", 300);
            o.insert("rating", 0);
            QJsonObject file;
            file.insert("id", 1000 + bookId);
            file.insert("format", o.value("formats").toArray().first());
            file.insert("role", "content");
            file.insert("sizeBytes", 350000);
            o.insert("files", QJsonArray() << file);
            answer<QJsonObject>(done, o);
            return 1;
        }
        done(fail<QJsonObject>(ErrorKind::NotFound));
        return 1;
    }

    RequestId manifest(const QString &, Callback<QJsonObject> done) override
    {
        answer<QJsonObject>(done, QJsonObject());
        return 1;
    }

    RequestId download(qint64, const QString &, std::function<void(qint64, qint64)>,
                       Callback<QString> done) override
    {
        done(fail<QString>(ErrorKind::ClientError));
        return 1;
    }

    RequestId thumbnail(qint64 bookId, Callback<QByteArray> done) override
    {
        ++thumbnailCalls;
        answer<QByteArray>(done, fakeCoverPng(int(bookId)));
        return 1;
    }
};

// A fake Nickel whose import and open calls say "not implemented yet", as the
// real bridge does on the Kobo until task 30.
class UnimplementedNickel : public smalt::testing::FakeNickel
{
public:
    Status importFiles(const QStringList &) override
    {
        return Status::error(StatusCode::NotImplemented, QStringLiteral("stub"));
    }

    Status openBook(const QString &) override
    {
        return Status::error(StatusCode::NotImplemented, QStringLiteral("stub"));
    }

    Status showNickelHome() override
    {
        return Status::error(StatusCode::NotImplemented, QStringLiteral("stub"));
    }
};

// A temporary user partition with a ledger, settings and a fake Nickel.
struct Env
{
    QTemporaryDir dir;
    Paths paths = Paths::forRoot(QStringLiteral("/nonexistent"));
    std::unique_ptr<Ledger> ledger;
    std::unique_ptr<SettingsStore> settings;
    smalt::testing::FakeNickel defaultNickel;
    smalt::testing::FakeNickel *nickel = nullptr;
    FakeClient fake;
    BookOrbitClient *client = nullptr;
    std::unique_ptr<ui::ComponentFactory> kit;
    ShellDeps deps;

    explicit Env(DeviceModel model = DeviceModel::ClaraColour,
                 smalt::testing::FakeNickel *customNickel = nullptr,
                 BookOrbitClient *customClient = nullptr)
    {
        paths = Paths::forRoot(dir.path());
        QDir().mkpath(paths.addsDir());
        ledger = createLedger(paths);
        const Status s = ledger->open();
        if (!s.ok())
            qFatal("ledger did not open: %s", qPrintable(s.message));
        settings = createSettingsStore(paths);
        settings->load();
        nickel = customNickel ? customNickel : &defaultNickel;
        nickel->setDeviceModel(model);
        nickel->simulateNetwork(true);
        client = customClient ? customClient : &fake;
        kit = ui::createComponentFactory(model, nullptr);
        deps.kit = kit.get();
        deps.client = client;
        deps.ledger = ledger.get();
        deps.nickel = nickel;
        deps.settings = settings.get();
        deps.sync = nullptr;
        deps.paths = paths;
    }

    // Marks a fake-library book as on this Kobo, with local reading progress.
    void putOnKobo(int bookId, double percent = -1, ReadStatus status = ReadStatus::Unknown)
    {
        BookMapEntry e;
        e.contentId = QStringLiteral("file://%1/BookOrbit/Fake/Book %2.epub")
                              .arg(paths.onboard())
                              .arg(bookId);
        e.bookId = bookId;
        e.bookFileId = 1000 + bookId;
        e.partialMd5 = QStringLiteral("%1").arg(bookId, 32, 10, QLatin1Char('a'));
        e.origin = BookOrigin::Matched;
        e.createdUtc = QDateTime(QDate(2026, 11, 1), QTime(12, 0), Qt::UTC);
        QVERIFY(ledger->putBookMap(e).ok());
        ContentRow row; // Nickel knows the book too, so opening it works.
        row.contentId = e.contentId;
        row.title = QStringLiteral("Book %1").arg(bookId);
        nickel->putContent(row);
        if (percent >= 0) {
            Position p;
            p.percent = percent;
            QVERIFY(ledger->setLocalProgress(e.contentId, p,
                                             QDateTime(QDate(2026, 11, 19), QTime(9, 0), Qt::UTC))
                            .ok());
        }
        if (status != ReadStatus::Unknown)
            QVERIFY(ledger->setLocalStatus(e.contentId, status,
                                           QDateTime(QDate(2026, 11, 19), QTime(9, 0), Qt::UTC))
                            .ok());
    }

    // The books the screenshots and most tests start from.
    void seedKobo()
    {
        putOnKobo(2, 45, ReadStatus::Reading);
        putOnKobo(7, 12, ReadStatus::Reading);
        putOnKobo(13);
        putOnKobo(21, 80, ReadStatus::Reading);
        putOnKobo(31, 20, ReadStatus::Reading);
    }

    // Writes the fake library into the ledger's cache the way a refresh does.
    void seedCache(int count = 60)
    {
        QJsonArray all = fakeLibrary(count);
        int page = 0;
        for (int i = 0; i < all.size(); i += 100) {
            QJsonArray window;
            for (int j = i; j < all.size() && j < i + 100; ++j)
                window.append(all.at(j));
            ++page;
            ledger->cachePut(QStringLiteral("catalog:books:%1").arg(page), "catalog",
                             QJsonDocument(window).toJson(QJsonDocument::Compact),
                             QDateTime::currentDateTimeUtc());
        }
        QJsonObject meta;
        meta.insert("pages", page);
        ledger->cachePut(QStringLiteral("catalog:books:meta"), "catalog",
                         QJsonDocument(meta).toJson(QJsonDocument::Compact),
                         QDateTime::currentDateTimeUtc());
        for (int id = 1; id <= count; ++id)
            ledger->cachePut(QStringLiteral("cover:%1").arg(id), "cover", fakeCoverPng(id),
                             QDateTime::currentDateTimeUtc());
    }

    Shell *makeShell(QWidget *parent = nullptr)
    {
        const DeviceProfile profile = deviceProfile(nickel->deviceModel());
        QWidget *w = createCompanionShell(deps, parent);
        Shell *shell = qobject_cast<Shell *>(w);
        shell->resize(profile.widthPx, profile.heightPx);
        shell->show();
        QCoreApplication::processEvents();
        return shell;
    }
};

inline QImage renderShell(QWidget *w)
{
    w->show();
    QCoreApplication::processEvents();
    QImage image(w->size(), QImage::Format_RGB32);
    image.fill(Qt::white);
    w->render(&image);
    return image;
}

// Compares with tests/ui/screens/golden/<name>.png at half size (small files).
// SMALT_UPDATE_GOLDENS=1 rewrites the golden. Empty string when it matches.
inline QString compareGolden(const QImage &actual, const QString &name)
{
    const QString path =
            QStringLiteral(SMALT_SCREENS_GOLDEN_DIR "/") + name + QStringLiteral(".png");
    const QImage small = actual.scaled(actual.width() / 2, actual.height() / 2,
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 .convertToFormat(QImage::Format_RGB32);
    if (qgetenv("SMALT_UPDATE_GOLDENS") == "1") {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!small.save(path, "PNG", 0))
            return QStringLiteral("cannot write ") + path;
        return QString();
    }
    QImage golden(path);
    if (golden.isNull())
        return QStringLiteral("missing golden %1 (run the ui_screens_update_goldens target)")
                .arg(path);
    golden = golden.convertToFormat(QImage::Format_RGB32);
    if (golden.size() != small.size())
        return QStringLiteral("%1: size %2x%3, golden %4x%5")
                .arg(name)
                .arg(small.width())
                .arg(small.height())
                .arg(golden.width())
                .arg(golden.height());
    qint64 different = 0;
    for (int y = 0; y < small.height(); ++y) {
        const QRgb *a = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        const QRgb *g = reinterpret_cast<const QRgb *>(golden.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            if (qAbs(qRed(a[x]) - qRed(g[x])) > 48 || qAbs(qGreen(a[x]) - qGreen(g[x])) > 48
                || qAbs(qBlue(a[x]) - qBlue(g[x])) > 48)
                ++different;
        }
    }
    const double fraction = double(different) / (double(small.width()) * small.height());
    if (fraction > 0.003) {
        const QString out =
                QDir::temp().filePath(name.split('/').last() + QStringLiteral("-actual.png"));
        small.save(out);
        return QStringLiteral("%1: %2% of pixels differ from the golden (actual saved to %3)")
                .arg(name)
                .arg(fraction * 100)
                .arg(out);
    }
    return QString();
}

// Taps a widget at its centre (press and release).
inline void tap(QWidget *w)
{
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, w->rect().center());
    QCoreApplication::processEvents();
}

// The first descendant of type T (or null).
template <typename T>
T *findFirst(QWidget *root)
{
    const QList<T *> all = root->findChildren<T *>();
    for (T *w : all)
        if (w->isVisibleTo(root))
            return w;
    return nullptr;
}

template <typename T>
QList<T *> findVisible(QWidget *root)
{
    QList<T *> out;
    const QList<T *> all = root->findChildren<T *>();
    for (T *w : all)
        if (w->isVisibleTo(root))
            out.append(w);
    return out;
}

// A visible kit button with this object name ("main", "continue", "downloadNext",
// "syncNow", "testConnection"), if one is on screen.
inline ui::kit::Button *buttonNamed(QWidget *root, const QString &name)
{
    const QList<ui::kit::Button *> buttons = findVisible<ui::kit::Button>(root);
    for (ui::kit::Button *b : buttons)
        if (b->objectName() == name)
            return b;
    return nullptr;
}

// A visible text row with this title (Settings, More), if one is on screen.
inline TextRow *rowTitled(QWidget *root, const QString &title)
{
    const QList<TextRow *> rows = findVisible<TextRow>(root);
    for (TextRow *r : rows)
        if (r->title() == title)
            return r;
    return nullptr;
}

} // namespace screenstest
