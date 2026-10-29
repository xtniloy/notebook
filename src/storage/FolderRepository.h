#pragma once

#include "models/Folder.h"
#include "storage/DbResult.h"

#include <QList>
#include <QString>

#include <optional>
#include <vector>

namespace notes::app {
class IClock;
}

namespace notes::storage {

class Database;

// A folder with its subfolders, as listTree() returns them.
struct FolderNode
{
    models::Folder folder;
    std::vector<FolderNode> children;
};

// Folders in the `folders` table (task 2.5). Runs on the DB thread (ADR 0003);
// services wrap it in task 2.7.
//
// Every change bumps `version`, sets `updated_at` to the clock's now (ADR 0004)
// and marks the row dirty for sync (`sync_state = 0`). Deleted folders stay as
// tombstones (`deleted_at`) for sync and are invisible to everything but get().
class FolderRepository
{
public:
    // The default "Notes" folder has this fixed UUID on every device, so the
    // copies from two devices are one folder after sync (ADR 0005). Never change it.
    static constexpr char kDefaultFolderUuid[] = "d5cf1c5f-0bc7-419e-b2f2-096709ed4f5b";

    FolderRepository(Database& db, const app::IClock& clock);
    ~FolderRepository() = default;
    // Bound to one connection and clock for its whole life.
    FolderRepository(const FolderRepository&) = delete;
    FolderRepository& operator=(const FolderRepository&) = delete;
    FolderRepository(FolderRepository&&) = delete;
    FolderRepository& operator=(FolderRepository&&) = delete;

    // Creates the default "Notes" folder unless it exists, and returns it. Call
    // after migrating, on every start.
    [[nodiscard]] DbResult<models::Folder> ensureDefaultFolder();

    // A new folder at the end of `parentId`'s subfolders (top level if empty).
    // The name is trimmed and must not be empty.
    [[nodiscard]] DbResult<models::Folder> create(const QString& name,
                                                  std::optional<qint64> parentId = {});
    // Any folder, including deleted ones.
    [[nodiscard]] DbResult<models::Folder> get(qint64 id);

    [[nodiscard]] DbResult<models::Folder> rename(qint64 id, const QString& name);
    // Fails with Kind::Cycle if `newParentId` is the folder or one of its subfolders.
    [[nodiscard]] DbResult<models::Folder> move(qint64 id, std::optional<qint64> newParentId);
    // Deletes the folder and its subfolders, and moves their notes to Recently
    // Deleted (TC-STO-010). The default folder can't be deleted.
    DbStatus softDelete(qint64 id);
    // Sets the order of `parentId`'s subfolders; `orderedIds` must list each of
    // them exactly once.
    DbStatus reorder(std::optional<qint64> parentId, const QList<qint64>& orderedIds);

    // Every folder that isn't deleted, as a tree. Siblings are in sort order.
    [[nodiscard]] DbResult<std::vector<FolderNode>> listTree();

private:
    [[nodiscard]] DbResult<models::Folder> getLive(qint64 id);
    [[nodiscard]] DbResult<QList<qint64>> childIds(std::optional<qint64> parentId);
    [[nodiscard]] DbResult<bool> isInSubtree(qint64 rootId, qint64 candidateId);
    [[nodiscard]] static DbStatus checkMutable(const models::Folder& folder);

    Database& m_db;
    const app::IClock& m_clock;
};

} // namespace notes::storage
