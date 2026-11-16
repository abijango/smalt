#include "fakes/fake_nickel.h"

namespace smalt {
namespace testing {

FakeNickel::FakeNickel(QObject *parent) : NickelBridge(parent) { }

void FakeNickel::addReadingTime(const QString &contentId, qint64 seconds)
{
    m_content[contentId].contentId = contentId;
    m_content[contentId].timeSpentReading += seconds;
}

void FakeNickel::setPosition(const QString &contentId, const QString &chapterId, double percent)
{
    ContentRow &row = m_content[contentId];
    row.contentId = contentId;
    row.chapterIdBookmarked = chapterId;
    row.percentRead = percent;
    row.dateLastRead = QDateTime::currentDateTimeUtc();
}

void FakeNickel::simulateNickelReady()
{
    m_ready = true;
    emit nickelReady();
}

void FakeNickel::simulateBookOpened(const QString &contentId)
{
    m_open = contentId;
    emit bookOpened(contentId);
}

void FakeNickel::simulateBookClosed()
{
    const QString closed = m_open;
    m_open.clear();
    emit bookClosed(closed);
}

void FakeNickel::simulateSleep()
{
    emit aboutToSleep();
}

void FakeNickel::simulateWake()
{
    emit wokeUp();
}

void FakeNickel::simulateUsbConnected()
{
    usbConnectedNow = true;
    emit usbConnected();
}

void FakeNickel::simulateUsbDisconnected()
{
    usbConnectedNow = false;
    emit usbDisconnected();
}

void FakeNickel::simulateNetwork(bool connected)
{
    m_connected = connected;
    if (connected)
        m_wifi = true;
    emit networkChanged(connected);
}

void FakeNickel::simulatePageTurn(int page, int totalPages)
{
    emit pageTurned(m_open, page, totalPages);
}

Status FakeNickel::openBook(const QString &contentId)
{
    if (!m_content.contains(contentId))
        return Status::error(StatusCode::NotFound, QStringLiteral("no such book: ") + contentId);
    openedBooks.append(contentId);
    simulateBookOpened(contentId);
    return Status::success();
}

Status FakeNickel::importFiles(const QStringList &paths)
{
    importedFiles.append(paths);
    return Status::success();
}

Status FakeNickel::showNickelHome()
{
    ++nickelHomeShown;
    return Status::success();
}

Status FakeNickel::readContent(const QString &contentId, ContentRow *out)
{
    if (!m_content.contains(contentId))
        return Status::error(StatusCode::NotFound, QStringLiteral("no such book: ") + contentId);
    if (out)
        *out = m_content.value(contentId);
    return Status::success();
}

Status FakeNickel::listCollections(QVector<NickelCollection> *out)
{
    if (out)
        *out = m_collections;
    return Status::success();
}

Status FakeNickel::checkWritable(const QString &contentId)
{
    if (usbConnectedNow)
        return Status::error(StatusCode::Busy, QStringLiteral("USB connected"));
    if (!contentId.isEmpty() && contentId == m_open)
        return Status::error(StatusCode::Busy, QStringLiteral("book is open: ") + contentId);
    if (m_failNextWrite) {
        m_failNextWrite = false;
        return Status::error(StatusCode::IoError, QStringLiteral("simulated write failure"));
    }
    return Status::success();
}

Status FakeNickel::writePosition(const QString &contentId, const QString &chapterIdBookmarked,
                                 double percentRead, const QDateTime &dateLastReadUtc)
{
    const Status s = checkWritable(contentId);
    if (!s.ok())
        return s;
    if (!m_content.contains(contentId))
        return Status::error(StatusCode::NotFound, QStringLiteral("no such book: ") + contentId);
    ContentRow &row = m_content[contentId];
    row.chapterIdBookmarked = chapterIdBookmarked;
    row.percentRead = percentRead;
    row.dateLastRead = dateLastReadUtc;
    return Status::success();
}

Status FakeNickel::writeReadStatus(const QString &contentId, int readStatus)
{
    const Status s = checkWritable(contentId);
    if (!s.ok())
        return s;
    if (!m_content.contains(contentId))
        return Status::error(StatusCode::NotFound, QStringLiteral("no such book: ") + contentId);
    m_content[contentId].readStatus = readStatus;
    return Status::success();
}

Status FakeNickel::writeRating(const QString &contentId, int rating)
{
    const Status s = checkWritable(contentId);
    if (!s.ok())
        return s;
    if (!m_content.contains(contentId))
        return Status::error(StatusCode::NotFound, QStringLiteral("no such book: ") + contentId);
    m_content[contentId].rating = rating;
    return Status::success();
}

Status FakeNickel::writeCollection(const NickelCollection &collection)
{
    const Status s = checkWritable(QString());
    if (!s.ok())
        return s;
    for (NickelCollection &existing : m_collections) {
        if (existing.name == collection.name) {
            existing = collection;
            return Status::success();
        }
    }
    m_collections.append(collection);
    return Status::success();
}

Status FakeNickel::removeCollection(const QString &name)
{
    const Status s = checkWritable(QString());
    if (!s.ok())
        return s;
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).name == name) {
            m_collections.remove(i);
            return Status::success();
        }
    }
    return Status::error(StatusCode::NotFound, QStringLiteral("no such collection: ") + name);
}

Status FakeNickel::deleteBook(const QString &contentId)
{
    const Status s = checkWritable(contentId);
    if (!s.ok())
        return s;
    if (!m_content.remove(contentId))
        return Status::error(StatusCode::NotFound, QStringLiteral("no such book: ") + contentId);
    return Status::success();
}

Status FakeNickel::setWifiEnabled(bool enabled)
{
    if (enabled)
        ++wifiEnableCalls;
    else
        ++wifiDisableCalls;
    m_wifi = enabled;
    if (!enabled && m_connected) {
        m_connected = false;
        emit networkChanged(false);
    }
    return Status::success();
}

Status FakeNickel::connectWifi()
{
    if (!m_wifi)
        return Status::error(StatusCode::Unavailable, QStringLiteral("Wi-Fi is off"));
    m_connected = m_connectSucceeds;
    emit networkChanged(m_connected);
    return Status::success();
}

Status FakeNickel::holdSleep()
{
    ++sleepHolds;
    return Status::success();
}

void FakeNickel::releaseSleep()
{
    if (sleepHolds > 0)
        --sleepHolds;
}

} // namespace testing
} // namespace smalt
