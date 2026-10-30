#pragma once

#include "models/Folder.h"
#include "storage/DbResult.h"
#include "storage/FolderRepository.h"

#include <QFuture>
#include <QList>
#include <QObject>

#include <optional>

namespace notes::app {
class IClock;
}

namespace notes::storage {
class DbThread;
}

namespace notes::services {

// Folders for the UI (task 2.7, ADR 0003). Each call runs FolderRepository on
// the DB thread and returns at once with a QFuture; continue with
// `.then(context, ...)`, never wait on it from the UI thread.
//
// After a change succeeds, the matching signal is emitted on this object's
// thread, before the returned future finishes. Failed calls emit nothing.
class FolderService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FolderService)

public:
    // `db` must have been migrated (storage::Migrator). Both must outlive this.
    FolderService(storage::DbThread& db, const app::IClock& clock, QObject* parent = nullptr);
    ~FolderService() override;

    // The default "Notes" folder, created on first run.
    [[nodiscard]] QFuture<storage::DbResult<models::Folder>> ensureDefaultFolder();
    // Display order, each folder followed by its subfolders (FolderTreeEntry).
    [[nodiscard]] QFuture<storage::DbResult<QList<storage::FolderTreeEntry>>> listTree();

    QFuture<storage::DbResult<models::Folder>> create(const QString& name,
                                                      std::optional<qint64> parentId = {});
    QFuture<storage::DbResult<models::Folder>> rename(qint64 id, const QString& name);
    QFuture<storage::DbResult<models::Folder>> move(qint64 id, std::optional<qint64> newParentId);
    // Deletes the folder and its subfolders; their notes go to Recently Deleted.
    QFuture<storage::DbStatus> remove(qint64 id);
    QFuture<storage::DbStatus> reorder(std::optional<qint64> parentId, QList<qint64> orderedIds);

signals:
    void folderCreated(const notes::models::Folder& folder);
    // Renamed or moved.
    void folderChanged(const notes::models::Folder& folder);
    // Its subfolders went too, and its notes moved to Recently Deleted: reload.
    void folderDeleted(qint64 folderId);
    void foldersReordered();

private:
    storage::DbThread& m_db;
    const app::IClock& m_clock;
};

} // namespace notes::services
