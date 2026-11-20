#include "sync_text.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace smalt {
namespace sync {

QString bookOrbitStatus(ReadStatus status)
{
    switch (status) {
    case ReadStatus::Unread:
        return QStringLiteral("unread");
    case ReadStatus::WantToRead:
        return QStringLiteral("want_to_read");
    case ReadStatus::Reading:
        return QStringLiteral("reading");
    case ReadStatus::OnHold:
        return QStringLiteral("on_hold");
    case ReadStatus::Read:
        return QStringLiteral("read");
    case ReadStatus::Abandoned:
        return QStringLiteral("abandoned");
    case ReadStatus::Unknown:
        break;
    }
    return QString();
}

ReadStatus statusFromBookOrbit(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("unread"))
        return ReadStatus::Unread;
    if (v == QLatin1String("want_to_read"))
        return ReadStatus::WantToRead;
    if (v == QLatin1String("reading"))
        return ReadStatus::Reading;
    if (v == QLatin1String("on_hold"))
        return ReadStatus::OnHold;
    if (v == QLatin1String("read"))
        return ReadStatus::Read;
    if (v == QLatin1String("abandoned"))
        return ReadStatus::Abandoned;
    return ReadStatus::Unknown;
}

int nickelStatus(ReadStatus status)
{
    switch (status) {
    case ReadStatus::Reading:
    case ReadStatus::OnHold:
        return kNickelReading;
    case ReadStatus::Read:
        return kNickelFinished;
    case ReadStatus::Unread:
    case ReadStatus::WantToRead:
        return kNickelUnread;
    case ReadStatus::Abandoned:
    case ReadStatus::Unknown:
        break;
    }
    return -1;
}

QByteArray positionPayload(const QString &partialMd5, double percent0to100, qint64 timestamp)
{
    QJsonObject o;
    o.insert(QStringLiteral("document"), partialMd5.toLower());
    o.insert(QStringLiteral("percentage"), qBound(0.0, percent0to100 / 100.0, 1.0));
    o.insert(QStringLiteral("progress"), QString());
    o.insert(QStringLiteral("timestamp"), static_cast<double>(timestamp));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool parsePositionPayload(const QByteArray &payload, RemoteProgress *out)
{
    const QJsonObject o = QJsonDocument::fromJson(payload).object();
    const QString doc = o.value(QStringLiteral("document")).toString();
    if (doc.isEmpty() || !o.value(QStringLiteral("percentage")).isDouble())
        return false;
    if (out) {
        out->document = doc;
        out->percentage = o.value(QStringLiteral("percentage")).toDouble();
        out->progress = o.value(QStringLiteral("progress")).toString();
        out->timestamp = static_cast<qint64>(o.value(QStringLiteral("timestamp")).toDouble());
    }
    return true;
}

QByteArray statusPayload(const QString &contentId, qint64 bookId)
{
    QJsonObject o;
    o.insert(QStringLiteral("contentId"), contentId);
    o.insert(QStringLiteral("bookId"), static_cast<double>(bookId));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool parseAnnotationPayload(const QByteArray &payload, AnnotationExchange *out)
{
    const QJsonObject o = QJsonDocument::fromJson(payload).object();
    const QString doc = o.value(QStringLiteral("document")).toString();
    if (doc.isEmpty())
        return false;
    if (out) {
        out->document = doc;
        out->changes = o.value(QStringLiteral("changes")).toArray();
        out->keys = o.value(QStringLiteral("keys")).toArray();
        out->keysComplete = o.value(QStringLiteral("keysComplete")).toBool(true);
        out->sidecarState = o.value(QStringLiteral("sidecarState")).toString();
    }
    return true;
}

QString headerText(const SyncStatus &status, const QDateTime &nowLocal)
{
    QString head;
    if (status.running) {
        head = QStringLiteral("Syncing…");
    } else if (!status.online) {
        head = QStringLiteral("Offline");
    } else if (status.lastSuccessUtc.isValid()) {
        const QDateTime when = status.lastSuccessUtc.toLocalTime();
        head = QStringLiteral("Synced ")
                + (when.date() == nowLocal.date() ? when.toString(QStringLiteral("HH:mm"))
                                                  : when.toString(QStringLiteral("d MMM")));
    } else {
        head = QStringLiteral("Not synced yet");
    }
    if (status.waiting > 0)
        head += QStringLiteral(" · %1 waiting").arg(status.waiting);
    return head;
}

namespace {

QString count(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}

} // namespace

QString summaryText(const SyncResult &result)
{
    QStringList parts;
    if (result.sessionsPushed > 0)
        parts << count(result.sessionsPushed, "session", "sessions");
    if (result.highlightsPushed > 0)
        parts << count(result.highlightsPushed, "highlight", "highlights");
    if (result.positionsPushed > 0)
        parts << count(result.positionsPushed, "position", "positions");
    if (result.statusesPushed > 0)
        parts << count(result.statusesPushed, "status", "statuses");
    if (parts.isEmpty())
        return QStringLiteral("Synced: nothing new");
    return QStringLiteral("Synced: ") + parts.join(QStringLiteral(", "));
}

QString failureText(ErrorKind error)
{
    if (error == ErrorKind::AuthRejected)
        return QStringLiteral("BookOrbit didn't accept your KOReader login. Check it in "
                              "BookOrbit's Settings > KOReader.");
    return QStringLiteral("Couldn't reach BookOrbit. Your reading is saved and will sync later.");
}

} // namespace sync
} // namespace smalt
