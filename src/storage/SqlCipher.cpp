#include "storage/SqlCipher.h"

#include <sqlite3.h>

#include <memory>

namespace notes::storage {

QString sqlcipherVersion()
{
    sqlite3* rawDb = nullptr;
    const int openRc =
        sqlite3_open_v2(":memory:", &rawDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    // sqlite3_open_v2 may allocate a handle even on failure, so always take ownership.
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(rawDb, &sqlite3_close);
    if (openRc != SQLITE_OK) {
        return {};
    }

    sqlite3_stmt* rawStmt = nullptr;
    if (sqlite3_prepare_v2(db.get(), "PRAGMA cipher_version;", -1, &rawStmt, nullptr) !=
        SQLITE_OK) {
        return {};
    }
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(rawStmt,
                                                                          &sqlite3_finalize);

    // Plain SQLite ignores the unknown pragma and returns no row.
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return {};
    }
    const unsigned char* text = sqlite3_column_text(stmt.get(), 0);
    if (text == nullptr) {
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(text));
}

} // namespace notes::storage
