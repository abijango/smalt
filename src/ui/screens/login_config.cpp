#include "login_config.h"

#include "smalt/log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

namespace smalt {
namespace screens {

namespace {

const char *const kDefaultTemplate = "; Smalt configuration. Holds your login: never share it.\n"
                                     "\n"
                                     "[server]\n"
                                     "url =\n"
                                     "\n"
                                     "[koreader]\n"
                                     "username =\n"
                                     "password =\n";

QStringList splitLines(const QByteArray &bytes)
{
    QString text = QString::fromUtf8(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

bool parseKeyValue(const QString &line, QString *key, QString *value)
{
    const QString t = line.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char(';')) || t.startsWith(QLatin1Char('#')))
        return false;
    const int eq = t.indexOf(QLatin1Char('='));
    if (eq <= 0)
        return false;
    *key = t.left(eq).trimmed();
    // A value runs to the end of its line, so ';' and ',' are fine in a password.
    *value = t.mid(eq + 1).trimmed();
    return true;
}

QString sectionOf(const QString &line)
{
    const QString t = line.trimmed();
    if (t.size() > 2 && t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))
        return t.mid(1, t.size() - 2).trimmed().toLower();
    return QString();
}

} // namespace

bool isValidServerUrl(const QString &url)
{
    const QUrl u(url.trimmed());
    return u.isValid()
            && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https"))
            && !u.host().isEmpty();
}

Status readLoginFields(const QString &configIniPath, LoginFields *out)
{
    *out = LoginFields();
    QFile f(configIniPath);
    if (!f.exists())
        return Status::error(StatusCode::NotFound, QStringLiteral("config.ini is missing"));
    if (!f.open(QIODevice::ReadOnly))
        return Status::error(StatusCode::IoError, QStringLiteral("cannot read config.ini"));
    QString section;
    const QStringList lines = splitLines(f.readAll());
    for (const QString &line : lines) {
        const QString s = sectionOf(line);
        if (!s.isEmpty()) {
            section = s;
            continue;
        }
        QString key, value;
        if (!parseKeyValue(line, &key, &value))
            continue;
        if (section == QLatin1String("server") && key == QLatin1String("url"))
            out->url = value;
        else if (section == QLatin1String("koreader") && key == QLatin1String("username"))
            out->username = value;
        else if (section == QLatin1String("koreader") && key == QLatin1String("password"))
            out->password = value;
    }
    return Status::success();
}

Status writeLoginFields(const Paths &paths, const LoginFields &fields)
{
    const QString path = paths.configIni();
    QByteArray original;
    QFile in(path);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly))
            return Status::error(StatusCode::IoError, QStringLiteral("cannot read config.ini"));
        original = in.readAll();
        in.close();
    } else {
        QFile example(paths.configIniExample());
        if (example.open(QIODevice::ReadOnly))
            original = example.readAll();
        else
            original = QByteArray(kDefaultTemplate);
    }

    struct Target
    {
        const char *section;
        const char *key;
        QString value;
        bool done;
    };

    Target targets[3] = { { "server", "url", fields.url.trimmed(), false },
                          { "koreader", "username", fields.username.trimmed(), false },
                          { "koreader", "password", fields.password, false } };

    QStringList out;
    QString section;
    const QStringList originalLines = splitLines(original);
    for (const QString &line : originalLines) {
        const QString s = sectionOf(line);
        if (!s.isEmpty()) {
            section = s;
            out << line;
            continue;
        }
        QString key, value;
        bool replaced = false;
        if (parseKeyValue(line, &key, &value)) {
            for (Target &t : targets) {
                if (!t.done && section == QLatin1String(t.section) && key == QLatin1String(t.key)) {
                    out << QStringLiteral("%1 = %2").arg(key, t.value);
                    t.done = true;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            out << line;
    }
    for (const Target &t : targets) {
        if (t.done)
            continue;
        int at = -1;
        for (int i = 0; i < out.size(); ++i)
            if (sectionOf(out.at(i)) == QLatin1String(t.section))
                at = i;
        const QString entry = QStringLiteral("%1 = %2").arg(QLatin1String(t.key), t.value);
        if (at < 0) {
            if (!out.isEmpty() && !out.last().isEmpty())
                out << QString();
            out << QStringLiteral("[%1]").arg(QLatin1String(t.section)) << entry;
        } else {
            int end = at + 1;
            while (end < out.size() && sectionOf(out.at(end)).isEmpty())
                ++end;
            while (end > at + 1 && out.at(end - 1).trimmed().isEmpty())
                --end;
            out.insert(end, entry);
        }
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return Status::error(StatusCode::IoError, QStringLiteral("cannot create the folder"));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return Status::error(StatusCode::IoError, QStringLiteral("cannot write config.ini"));
    f.write((out.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    if (!f.commit()) {
        log::warn("screens", QStringLiteral("config.ini could not be saved"));
        return Status::error(StatusCode::IoError, QStringLiteral("cannot save config.ini"));
    }
    return Status::success();
}

} // namespace screens
} // namespace smalt
