// Reads and writes the login in .adds/bookorbit/config.ini for the Settings
// screen.
//
// src/bookorbit/client_config.h only reads the file and keeps just the MD5 of
// the password; Settings has to show the URL and username and keep the
// password when only one of them changes. The format is the documented one
// (packaging/resources/config.ini.example): [server] url, [koreader] username
// and password, ';' and '#' comment lines, values to the end of the line. The
// writer keeps comments and unknown lines. Proposal for the maintainer: move
// this next to the reader in src/bookorbit/.
#pragma once

#include "smalt/paths.h"
#include "smalt/status.h"

#include <QString>

namespace smalt {
namespace screens {

struct LoginFields
{
    QString url;
    QString username;
    QString password;
};

// NotFound when the file is missing (fields stay empty); IoError otherwise.
Status readLoginFields(const QString &configIniPath, LoginFields *out);

// Atomic (QSaveFile). Starts from the shipped example when the file is
// missing. The password is never logged.
Status writeLoginFields(const Paths &paths, const LoginFields &fields);

// True when `url` is an http or https address with a host.
bool isValidServerUrl(const QString &url);

} // namespace screens
} // namespace smalt
