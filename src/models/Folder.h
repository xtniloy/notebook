#pragma once

#include "models/SyncState.h"

#include <QString>

#include <optional>

namespace notes::models {

// A row of the `folders` table (Plan §7). Plain value type: copied between the
// DB thread and the UI (ADR 0003). Times are UTC ms (ADR 0004); `id` is local
// only, `uuid` is the identity across devices (ADR 0005).
struct Folder
{
    qint64 id = 0; // 0 until stored
    QString uuid;
    std::optional<qint64> parentId; // empty for top-level folders
    QString name;
    int sortOrder = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    std::optional<qint64> deletedAt; // sync tombstone
    int version = 1;
    SyncState syncState = SyncState::Dirty;

    bool operator==(const Folder&) const = default;
};

} // namespace notes::models
