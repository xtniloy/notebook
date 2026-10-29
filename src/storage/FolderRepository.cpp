#include "storage/FolderRepository.h"

#include "app/IClock.h"
#include "models/Uuid.h"
#include "storage/Database.h"
#include "storage/Transaction.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <ranges>

namespace notes::storage {

using models::Folder;
using Kind = DbError::Kind;

namespace {

constexpr QLatin1StringView
    kColumns("id, uuid, parent_id, name, sort_order, created_at, updated_at, deleted_at, version, "
             "sync_state");

// What every change does to a row (task 2.5); binds :now.
constexpr QLatin1StringView kBump("version = version + 1, updated_at = :now, sync_state = 0");

// The folder `:root` and all its subfolders, as a CTE named `subtree(id)`.
constexpr QLatin1StringView
    kSubtree("WITH RECURSIVE subtree(id) AS ("
             " SELECT :root"
             " UNION ALL"
             " SELECT f.id FROM folders f JOIN subtree s ON f.parent_id = s.id"
             ") ");

Folder readFolder(const Statement& row)
{
    Folder folder;
    folder.id = row.column<qint64>(0);
    folder.uuid = row.column<QString>(1);
    folder.parentId = row.column<std::optional<qint64>>(2);
    folder.name = row.column<QString>(3);
    folder.sortOrder = row.column<int>(4);
    folder.createdAt = row.column<qint64>(5);
    folder.updatedAt = row.column<qint64>(6);
    folder.deletedAt = row.column<std::optional<qint64>>(7);
    folder.version = row.column<int>(8);
    folder.syncState = row.column<models::SyncState>(9);
    return folder;
}

// Trimmed name, or an error for empty / whitespace-only names (TC-STO-012).
DbResult<QString> validName(const QString& name)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return DbError::make(Kind::InvalidArgument, QStringLiteral("Folder name is empty"));
    }
    return trimmed;
}

DbError notFound(qint64 id)
{
    return DbError::make(Kind::NotFound, QStringLiteral("No folder with id %1").arg(id));
}

} // namespace

FolderRepository::FolderRepository(Database& db, const app::IClock& clock)
    : m_db(db)
    , m_clock(clock)
{}

DbResult<Folder> FolderRepository::get(qint64 id)
{
    DbResult<ScopedStatement> select =
        m_db.prepare(QStringLiteral("SELECT %1 FROM folders WHERE id = ?").arg(kColumns));
    if (!select) {
        return select.error();
    }
    DbResult<bool> row = select.value()->bind(1, id).step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return notFound(id);
    }
    return readFolder(*select.value());
}

DbResult<Folder> FolderRepository::getLive(qint64 id)
{
    DbResult<Folder> folder = get(id);
    if (folder && folder.value().deletedAt) {
        return notFound(id);
    }
    return folder;
}

DbStatus FolderRepository::checkMutable(const Folder& folder)
{
    if (folder.uuid == QLatin1StringView(kDefaultFolderUuid)) {
        return DbError::make(Kind::Protected,
                             QStringLiteral("The default folder can't be changed or deleted"));
    }
    return {};
}

DbResult<Folder> FolderRepository::ensureDefaultFolder()
{
    {
        DbResult<ScopedStatement> select =
            m_db.prepare(QStringLiteral("SELECT id FROM folders WHERE uuid = ?"));
        if (!select) {
            return select.error();
        }
        DbResult<bool> row =
            select.value()->bind(1, QString::fromLatin1(kDefaultFolderUuid)).step();
        if (!row) {
            return row.error();
        }
        if (row.value()) {
            return get(select.value()->column<qint64>(0));
        }
    }

    const qint64 now = m_clock.nowMs();
    DbResult<ScopedStatement> insert = m_db.prepare(QStringLiteral(
        "INSERT INTO folders (uuid, parent_id, name, sort_order, created_at, updated_at)"
        " VALUES (?, NULL, ?, 0, ?, ?)"));
    if (!insert) {
        return insert.error();
    }
    // Stored as "Notes"; the UI may show a translated name for this UUID.
    if (const DbStatus status = insert.value()
                                    ->bind(1, QString::fromLatin1(kDefaultFolderUuid))
                                    .bind(2, QStringLiteral("Notes"))
                                    .bind(3, now)
                                    .bind(4, now)
                                    .run();
        !status) {
        return status.error();
    }
    return get(m_db.lastInsertRowId());
}

DbResult<Folder> FolderRepository::create(const QString& name, std::optional<qint64> parentId)
{
    DbResult<QString> trimmed = validName(name);
    if (!trimmed) {
        return trimmed.error();
    }
    if (parentId) {
        if (const DbResult<Folder> parent = getLive(*parentId); !parent) {
            return parent.error();
        }
    }

    const qint64 now = m_clock.nowMs();
    DbResult<ScopedStatement> insert = m_db.prepare(QStringLiteral(
        "INSERT INTO folders (uuid, parent_id, name, sort_order, created_at, updated_at)"
        " VALUES (:uuid, :parent, :name,"
        "  (SELECT coalesce(max(sort_order), -1) + 1 FROM folders"
        "   WHERE parent_id IS :parent AND deleted_at IS NULL),"
        "  :now, :now)"));
    if (!insert) {
        return insert.error();
    }
    if (const DbStatus status = insert.value()
                                    ->bind(":uuid", models::newUuid())
                                    .bind(":parent", parentId)
                                    .bind(":name", trimmed.value())
                                    .bind(":now", now)
                                    .run();
        !status) {
        return status.error();
    }
    return get(m_db.lastInsertRowId());
}

DbResult<Folder> FolderRepository::rename(qint64 id, const QString& name)
{
    DbResult<QString> trimmed = validName(name);
    if (!trimmed) {
        return trimmed.error();
    }
    DbResult<Folder> folder = getLive(id);
    if (!folder) {
        return folder.error();
    }
    if (const DbStatus status = checkMutable(folder.value()); !status) {
        return status.error();
    }

    DbResult<ScopedStatement> update = m_db.prepare(
        QStringLiteral("UPDATE folders SET name = :name, %1 WHERE id = :id").arg(kBump));
    if (!update) {
        return update.error();
    }
    if (const DbStatus status = update.value()
                                    ->bind(":name", trimmed.value())
                                    .bind(":now", m_clock.nowMs())
                                    .bind(":id", id)
                                    .run();
        !status) {
        return status.error();
    }
    return get(id);
}

DbResult<bool> FolderRepository::isInSubtree(qint64 rootId, qint64 candidateId)
{
    DbResult<ScopedStatement> select = m_db.prepare(
        QStringLiteral("%1 SELECT 1 FROM subtree WHERE id = :candidate").arg(kSubtree));
    if (!select) {
        return select.error();
    }
    return select.value()->bind(":root", rootId).bind(":candidate", candidateId).step();
}

DbResult<Folder> FolderRepository::move(qint64 id, std::optional<qint64> newParentId)
{
    DbResult<Folder> folder = getLive(id);
    if (!folder) {
        return folder.error();
    }
    if (const DbStatus status = checkMutable(folder.value()); !status) {
        return status.error();
    }
    if (folder.value().parentId == newParentId) {
        return folder; // already there: not a change
    }
    if (newParentId) {
        if (const DbResult<Folder> parent = getLive(*newParentId); !parent) {
            return parent.error();
        }
        DbResult<bool> cycle = isInSubtree(id, *newParentId);
        if (!cycle) {
            return cycle.error();
        }
        if (cycle.value()) {
            return DbError::make(Kind::Cycle,
                                 QStringLiteral("Folder %1 can't move into its own subfolder %2")
                                     .arg(id)
                                     .arg(*newParentId));
        }
    }

    // Goes to the end of its new siblings.
    DbResult<ScopedStatement> update = m_db.prepare(
        QStringLiteral("UPDATE folders SET parent_id = :parent,"
                       " sort_order = (SELECT coalesce(max(sort_order), -1) + 1 FROM folders"
                       "               WHERE parent_id IS :parent AND deleted_at IS NULL),"
                       " %1 WHERE id = :id")
            .arg(kBump));
    if (!update) {
        return update.error();
    }
    if (const DbStatus status = update.value()
                                    ->bind(":parent", newParentId)
                                    .bind(":now", m_clock.nowMs())
                                    .bind(":id", id)
                                    .run();
        !status) {
        return status.error();
    }
    return get(id);
}

DbStatus FolderRepository::softDelete(qint64 id)
{
    DbResult<Folder> folder = getLive(id);
    if (!folder) {
        return folder.error();
    }
    if (const DbStatus status = checkMutable(folder.value()); !status) {
        return status;
    }

    DbResult<Transaction> tx = Transaction::begin(m_db);
    if (!tx) {
        return tx.error();
    }
    const qint64 now = m_clock.nowMs();
    // Notes first: they move to Recently Deleted and keep their folder_id, so
    // restoring can return them to it, or to "Notes" if it is gone (task 2.10).
    {
        DbResult<ScopedStatement> trashNotes =
            m_db.prepare(QStringLiteral("%1 UPDATE notes SET trashed_at = :now, %2"
                                        " WHERE folder_id IN subtree"
                                        " AND trashed_at IS NULL AND deleted_at IS NULL")
                             .arg(kSubtree, kBump));
        if (!trashNotes) {
            return trashNotes.error();
        }
        if (const DbStatus status = trashNotes.value()->bind(":root", id).bind(":now", now).run();
            !status) {
            return status;
        }
    }
    {
        DbResult<ScopedStatement> deleteFolders =
            m_db.prepare(QStringLiteral("%1 UPDATE folders SET deleted_at = :now, %2"
                                        " WHERE id IN subtree AND deleted_at IS NULL")
                             .arg(kSubtree, kBump));
        if (!deleteFolders) {
            return deleteFolders.error();
        }
        if (const DbStatus status =
                deleteFolders.value()->bind(":root", id).bind(":now", now).run();
            !status) {
            return status;
        }
    }
    return tx.value().commit();
}

DbResult<QList<qint64>> FolderRepository::childIds(std::optional<qint64> parentId)
{
    DbResult<ScopedStatement> select = m_db.prepare(QStringLiteral(
        "SELECT id FROM folders WHERE parent_id IS ? AND deleted_at IS NULL ORDER BY sort_order"));
    if (!select) {
        return select.error();
    }
    select.value()->bind(1, parentId);
    QList<qint64> ids;
    while (true) {
        DbResult<bool> row = select.value()->step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            return ids;
        }
        ids.append(select.value()->column<qint64>(0));
    }
}

DbStatus FolderRepository::reorder(std::optional<qint64> parentId, const QList<qint64>& orderedIds)
{
    DbResult<QList<qint64>> current = childIds(parentId);
    if (!current) {
        return current.error();
    }
    const QSet<qint64> expected(current.value().begin(), current.value().end());
    const QSet<qint64> given(orderedIds.begin(), orderedIds.end());
    if (given != expected || given.size() != orderedIds.size()) {
        return DbError::make(Kind::InvalidArgument,
                             QStringLiteral("reorder() needs each subfolder exactly once"));
    }

    DbResult<Transaction> tx = Transaction::begin(m_db);
    if (!tx) {
        return tx.error();
    }
    {
        // Only rows whose position changes count as changed.
        DbResult<ScopedStatement> update =
            m_db.prepare(QStringLiteral("UPDATE folders SET sort_order = :order, %1"
                                        " WHERE id = :id AND sort_order IS NOT :order")
                             .arg(kBump));
        if (!update) {
            return update.error();
        }
        const qint64 now = m_clock.nowMs();
        for (qsizetype i = 0; i < orderedIds.size(); ++i) {
            if (const DbStatus status = update.value()
                                            ->bind(":order", i)
                                            .bind(":now", now)
                                            .bind(":id", orderedIds.at(i))
                                            .run();
                !status) {
                return status;
            }
        }
    }
    return tx.value().commit();
}

DbResult<std::vector<FolderNode>> FolderRepository::listTree()
{
    DbResult<ScopedStatement> select =
        m_db.prepare(QStringLiteral("SELECT %1 FROM folders WHERE deleted_at IS NULL"
                                    " ORDER BY sort_order, name COLLATE NOCASE, id")
                         .arg(kColumns));
    if (!select) {
        return select.error();
    }
    QList<Folder> folders;
    while (true) {
        DbResult<bool> row = select.value()->step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            break;
        }
        folders.append(readFolder(*select.value()));
    }

    // Children per parent, in query (sort) order; a folder whose parent is gone
    // or deleted shows at the top level rather than disappearing.
    QSet<qint64> live;
    for (const Folder& folder : std::as_const(folders)) {
        live.insert(folder.id);
    }
    QHash<qint64, QList<qsizetype>> children; // parent id -> indexes into `folders`
    QList<qsizetype> roots;
    for (qsizetype i = 0; i < folders.size(); ++i) {
        const std::optional<qint64>& parent = folders.at(i).parentId;
        if (parent && live.contains(*parent)) {
            children[*parent].append(i);
        } else {
            roots.append(i);
        }
    }

    // Built iteratively; folder trees are shallow, but recursion is avoided by policy.
    struct Pending
    {
        qsizetype index;
        std::vector<FolderNode>* into;
    };
    std::vector<FolderNode> tree;
    QList<Pending> pending;
    // Pushed in reverse so they come off the stack in sort order.
    for (const qsizetype index : roots | std::views::reverse) {
        pending.append(Pending{.index = index, .into = &tree});
    }
    // Reserve so pointers into the vectors stay valid while they are filled.
    tree.reserve(static_cast<std::size_t>(roots.size()));
    while (!pending.isEmpty()) {
        const Pending next = pending.takeLast();
        const Folder& folder = folders.at(next.index);
        FolderNode& node = next.into->emplace_back(FolderNode{.folder = folder, .children = {}});
        const QList<qsizetype> kids = children.value(folder.id);
        node.children.reserve(static_cast<std::size_t>(kids.size()));
        for (const qsizetype index : kids | std::views::reverse) {
            pending.append(Pending{.index = index, .into = &node.children});
        }
    }
    return tree;
}

} // namespace notes::storage
