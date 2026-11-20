// KoboNickelBridge, recorder part. Owned by task 20; see bridge.h.
//
// The signals (nickelReady, bookOpened, bookClosed, aboutToSleep, wokeUp,
// usbConnected, usbDisconnected, pageTurned, aboutToPowerOff) come from
// recorder_hooks.cpp; the symbols and their sources are listed there.
//
// readContent() reads Nickel's content row read-only with SQL, the way
// Kobalt reads committed progress (platforms/kobo/nickel-qt5/
// native_progress.cpp, NativeProgress::persisted(), and common/
// home_sleep_screen.cpp, readNativeBook(); Kobalt commit
// be47258850ffdc3295257475afdf8b8f6cb53176): its own connection with
// QSQLITE_OPEN_READONLY and a 100 ms busy timeout, "WHERE ContentID = ? AND
// ContentType = 6". It never writes (CLAUDE.md hard rule 4) and never opens
// Kobalt's database (hard rule 8). A separate connection sees committed data
// only, which is what the recorder wants: Kobalt's pre-sleep and close
// writes once they have landed.
#include "bridge.h"
#include "hooks_recorder.h"
#include "recorder_watch.h"

#include "smalt/log.h"
#include "smalt/paths.h"

#include <QApplication>
#include <QAtomicInt>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QWidget>

namespace smalt {
namespace kobo {
namespace {

QAtomicInt g_connectionSerial;

QDateTime parseNickelDate(const QVariant &value)
{
    QDateTime date = value.toDateTime();
    if (!date.isValid())
        date = QDateTime::fromString(value.toString(), Qt::ISODate);
    return date.isValid() ? date.toUTC() : QDateTime();
}

} // namespace

bool KoboNickelBridge::isReady() const
{
    RecorderWatch *watch = RecorderWatch::instance();
    if (watch->ready())
        return true;
    // The finishInitialization hook was missed or is missing: Nickel's main
    // window being up is the direct check (Kobalt looks for a visible
    // "MainWindowView" the same way, common/home_sleep_screen.cpp).
    if (!qApp)
        return false;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible() && widget->inherits("MainWindowView")) {
            watch->install();
            watch->setReady();
            log::info("nickel", QStringLiteral("Nickel ready (direct check)"));
            return true;
        }
    }
    return false;
}

DeviceModel KoboNickelBridge::deviceModel() const
{
    const void *device = rec::currentDevice ? rec::currentDevice() : nullptr;
    if (!device)
        return DeviceModel::Unknown;
    // The specific variant before its hardware family, as Kobalt does.
    if (rec::deviceIsSpaKoboBW && rec::deviceIsSpaKoboBW(device))
        return DeviceModel::ClaraBW;
    if (rec::deviceIsSpaKobo && rec::deviceIsSpaKobo(device))
        return DeviceModel::ClaraColour;
    if (rec::deviceIsMonzaKobo && rec::deviceIsMonzaKobo(device))
        return DeviceModel::LibraColour;
    return DeviceModel::Unknown;
}

QString KoboNickelBridge::openContentId() const
{
    return RecorderWatch::instance()->openContentId();
}

Status KoboNickelBridge::readContent(const QString &contentId, ContentRow *out)
{
    if (!out)
        return Status::error(StatusCode::InvalidArgument, QStringLiteral("readContent: no output"));
    *out = ContentRow();
    if (contentId.isEmpty())
        return Status::error(StatusCode::InvalidArgument, QStringLiteral("readContent: no book"));
    if (RecorderWatch::instance()->usbConnected())
        return Status::error(StatusCode::Busy, QStringLiteral("USB connected"));

    const QString path = Paths::device().nickelDatabase();
    const QString name =
            QStringLiteral("smalt-content-%1").arg(g_connectionSerial.fetchAndAddRelaxed(1));
    Status result =
            Status::error(StatusCode::NotFound, QStringLiteral("no content row: ") + contentId);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100"));
        if (!db.open()) {
            result = Status::error(
                    StatusCode::IoError,
                    QStringLiteral("can't open %1: %2").arg(path, db.lastError().text()));
        } else {
            // Columns: Kobalt's queries above; TimeSpentReading from the
            // Clara Colour's database (VERIFY-FINDINGS item 1).
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                    "SELECT ContentID, Title, Attribution, MimeType, ChapterIDBookmarked,"
                    " ___PercentRead, TimeSpentReading, DateLastRead, ReadStatus FROM content"
                    " WHERE ContentID = ? AND ContentType = 6 LIMIT 1"));
            query.addBindValue(contentId);
            if (!query.exec()) {
                const QSqlError error = query.lastError();
                result = Status::error(error.text().contains(QLatin1String("locked"))
                                               ? StatusCode::Busy
                                               : StatusCode::IoError,
                                       QStringLiteral("content query: ") + error.text());
            } else if (query.next()) {
                out->contentId = query.value(0).toString();
                out->title = query.value(1).toString();
                out->author = query.value(2).toString();
                out->mimeType = query.value(3).toString();
                out->chapterIdBookmarked = query.value(4).toString();
                out->percentRead = query.value(5).toDouble();
                out->timeSpentReading = query.value(6).toLongLong();
                out->dateLastRead = parseNickelDate(query.value(7));
                out->readStatus = query.value(8).toInt();
                // Ratings live in another table; task 33 reads them.
                if (out->contentId.startsWith(QLatin1String("file://")))
                    out->filePath = out->contentId.mid(7);
                result = Status::success();
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

} // namespace kobo
} // namespace smalt
