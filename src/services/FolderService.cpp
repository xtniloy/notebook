#include "services/FolderService.h"

#include "app/IClock.h"
#include "storage/DbThread.h"

#include <utility>

namespace notes::services {

using models::Folder;
using storage::Database;
using storage::DbResult;
using storage::DbStatus;
using storage::FolderRepository;
using storage::FolderTreeEntry;

FolderService::FolderService(storage::DbThread& db, const app::IClock& clock, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
{}

FolderService::~FolderService() = default;

QFuture<DbResult<Folder>> FolderService::ensureDefaultFolder()
{
    const app::IClock* clock = &m_clock;
    return m_db.post(
        [clock](Database& db) { return FolderRepository(db, *clock).ensureDefaultFolder(); });
}

QFuture<DbResult<QList<FolderTreeEntry>>> FolderService::listTree()
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock](Database& db) { return FolderRepository(db, *clock).listTree(); });
}

QFuture<DbResult<Folder>> FolderService::create(const QString& name, std::optional<qint64> parentId)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, name, parentId](Database& db) {
            return FolderRepository(db, *clock).create(name, parentId);
        })
        .then(this, [this](DbResult<Folder> result) {
            if (result) {
                emit folderCreated(result.value());
            }
            return result;
        });
}

QFuture<DbResult<Folder>> FolderService::rename(qint64 id, const QString& name)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id, name](Database& db) {
            return FolderRepository(db, *clock).rename(id, name);
        })
        .then(this, [this](DbResult<Folder> result) {
            if (result) {
                emit folderChanged(result.value());
            }
            return result;
        });
}

QFuture<DbResult<Folder>> FolderService::move(qint64 id, std::optional<qint64> newParentId)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id, newParentId](Database& db) {
            return FolderRepository(db, *clock).move(id, newParentId);
        })
        .then(this, [this](DbResult<Folder> result) {
            if (result) {
                emit folderChanged(result.value());
            }
            return result;
        });
}

QFuture<DbStatus> FolderService::remove(qint64 id)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id](Database& db) { return FolderRepository(db, *clock).softDelete(id); })
        .then(this, [this, id](DbStatus status) {
            if (status) {
                emit folderDeleted(id);
            }
            return status;
        });
}

QFuture<DbStatus> FolderService::reorder(std::optional<qint64> parentId, QList<qint64> orderedIds)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, parentId, ids = std::move(orderedIds)](Database& db) {
            return FolderRepository(db, *clock).reorder(parentId, ids);
        })
        .then(this, [this](DbStatus status) {
            if (status) {
                emit foldersReordered();
            }
            return status;
        });
}

} // namespace notes::services
