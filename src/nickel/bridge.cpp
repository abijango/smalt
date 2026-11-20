// KoboNickelBridge: instance() and placeholders for methods later tasks own.
// See bridge.h. Task 00.
#include "bridge.h"

#include "smalt/log.h"

namespace smalt {
namespace kobo {
namespace {

Status notYet(const char *what, const char *task)
{
    log::warn("nickel",
              QStringLiteral("%1 isn't implemented yet (task %2)")
                      .arg(QLatin1String(what), QLatin1String(task)));
    return Status::error(StatusCode::NotImplemented, QLatin1String(what));
}

} // namespace

KoboNickelBridge *KoboNickelBridge::instance()
{
    // Never destroyed: Nickel exits without unloading the mod.
    static KoboNickelBridge *bridge = new KoboNickelBridge;
    return bridge;
}

// --- Task 30: home takeover and opening books ---

Status KoboNickelBridge::openBook(const QString &)
{
    return notYet("openBook", "30");
}

Status KoboNickelBridge::importFiles(const QStringList &)
{
    return notYet("importFiles", "30");
}

Status KoboNickelBridge::showNickelHome()
{
    return notYet("showNickelHome", "30");
}

void KoboNickelBridge::showToast(const QString &text)
{
    log::info("nickel", QStringLiteral("toast (not shown until task 30): %1").arg(text));
}

// --- Task 31: position writes ---

Status KoboNickelBridge::writePosition(const QString &, const QString &, double, const QDateTime &)
{
    return notYet("writePosition", "31");
}

// --- Task 33: status, rating, collections, removals ---

Status KoboNickelBridge::listCollections(QVector<NickelCollection> *)
{
    return notYet("listCollections", "33");
}

Status KoboNickelBridge::writeReadStatus(const QString &, int)
{
    return notYet("writeReadStatus", "33");
}

Status KoboNickelBridge::writeRating(const QString &, int)
{
    return notYet("writeRating", "33");
}

Status KoboNickelBridge::writeCollection(const NickelCollection &)
{
    return notYet("writeCollection", "33");
}

Status KoboNickelBridge::removeCollection(const QString &)
{
    return notYet("removeCollection", "33");
}

Status KoboNickelBridge::deleteBook(const QString &)
{
    return notYet("deleteBook", "33");
}

} // namespace kobo
} // namespace smalt
