// Reads the BookOrbit login from .adds/bookorbit/config.ini.
//
// Format (packaging/resources/config.ini.example):
//
//     [server]
//     url = https://books.example.com
//     [koreader]
//     username = ...
//     password = ...
//
// Smalt sends only the MD5 of the password (the x-auth-key header), as the
// KOReader plugin does. The password is never logged, and error messages name
// the missing key, not its value.
//
// The file is parsed by hand because QSettings splits values on commas, which
// would mangle passwords. Lines starting with ';' or '#' are comments; a value
// runs to the end of its line (no inline comments, so ';' is fine in a password).
#pragma once

#include "smalt/bookorbit_client.h"
#include "smalt/status.h"

#include <QString>

namespace smalt {

// MD5 of `text` (UTF-8), lowercase hex.
QByteArray md5Hex(const QString &text);

// Fills serverUrl, username and keyMd5Hex of `config` (other fields are left
// alone). Status::NotFound when the file is missing, InvalidArgument when a
// required key is empty or the URL is not http(s).
Status readClientConfig(const QString &configIniPath, ClientConfig *config);

} // namespace smalt
