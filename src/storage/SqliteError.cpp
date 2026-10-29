#include "storage/SqliteError.h"

#include <sqlite3.h>

namespace notes::storage {

DbError sqliteError(sqlite3* db, int rc)
{
    // sqlite3_errmsg() describes the most recent failure on this connection; fall
    // back to the generic text for the code when it doesn't match.
    const char* message = db != nullptr && sqlite3_extended_errcode(db) == rc ? sqlite3_errmsg(db)
                                                                              : sqlite3_errstr(rc);
    return {.code = rc, .message = QString::fromUtf8(message)};
}

} // namespace notes::storage
