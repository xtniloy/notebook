#pragma once

#include "storage/DbResult.h"

struct sqlite3;

namespace notes::storage {

// A DbError for result code `rc`, with SQLite's message for `db` (which may be
// null when opening failed before a handle existed). Storage-internal.
[[nodiscard]] DbError sqliteError(sqlite3* db, int rc);

} // namespace notes::storage
