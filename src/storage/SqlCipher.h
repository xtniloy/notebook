#pragma once

#include <QString>

namespace notes::storage {

// Version reported by `PRAGMA cipher_version`, or an empty string when the
// linked library is plain SQLite. The app must refuse to run without SQLCipher.
QString sqlcipherVersion();

} // namespace notes::storage
