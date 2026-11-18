#include "bookorbit/client_config.h"

#include <QCryptographicHash>
#include <QFile>
#include <QMap>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

namespace smalt {

QByteArray md5Hex(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex();
}

Status readClientConfig(const QString &configIniPath, ClientConfig *config)
{
    if (!config)
        return Status::error(StatusCode::InvalidArgument, QStringLiteral("no config to fill"));

    QFile file(configIniPath);
    if (!file.exists())
        return Status::error(StatusCode::NotFound, QStringLiteral("config.ini does not exist"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return Status::error(StatusCode::IoError,
                             QStringLiteral("cannot read config.ini: %1").arg(file.errorString()));

    QMap<QString, QString> values; // "section/key" -> value
    QString section;
    QTextStream in(&file);
    in.setCodec("UTF-8");
    bool first = true;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (first) {
            first = false;
            if (line.startsWith(QChar(0xFEFF)))
                line.remove(0, 1);
        }
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))
            || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2).trimmed().toLower();
            continue;
        }
        const int eq = trimmed.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = trimmed.left(eq).trimmed().toLower();
        const QString value = trimmed.mid(eq + 1).trimmed();
        values.insert(section + QLatin1Char('/') + key, value);
    }

    const QString url = values.value(QStringLiteral("server/url"));
    const QString user = values.value(QStringLiteral("koreader/username"));
    const QString password = values.value(QStringLiteral("koreader/password"));
    if (url.isEmpty())
        return Status::error(StatusCode::InvalidArgument,
                             QStringLiteral("config.ini: [server] url is empty"));
    if (user.isEmpty())
        return Status::error(StatusCode::InvalidArgument,
                             QStringLiteral("config.ini: [koreader] username is empty"));
    if (password.isEmpty())
        return Status::error(StatusCode::InvalidArgument,
                             QStringLiteral("config.ini: [koreader] password is empty"));

    const QUrl parsed = QUrl::fromUserInput(url);
    const QString scheme = parsed.scheme();
    if (!parsed.isValid() || parsed.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")))
        return Status::error(StatusCode::InvalidArgument,
                             QStringLiteral("config.ini: [server] url is not an http(s) address"));

    config->serverUrl = parsed;
    config->username = user;
    config->keyMd5Hex = md5Hex(password);
    return Status::success();
}

} // namespace smalt
