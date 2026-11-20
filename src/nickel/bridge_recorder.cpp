// KoboNickelBridge, recorder part. Owned by task 20; see bridge.h.
// Scaffold placeholder (task 00): reports "not ready" and no open book until
// task 20 resolves Nickel's symbols and installs the hooks in hooks_recorder.h.
#include "bridge.h"

namespace smalt {
namespace kobo {

bool KoboNickelBridge::isReady() const
{
    return false;
}

DeviceModel KoboNickelBridge::deviceModel() const
{
    return DeviceModel::Unknown;
}

QString KoboNickelBridge::openContentId() const
{
    return QString();
}

Status KoboNickelBridge::readContent(const QString &, ContentRow *)
{
    return Status::error(StatusCode::NotImplemented, QStringLiteral("readContent (task 20)"));
}

} // namespace kobo
} // namespace smalt
