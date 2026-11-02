#include "models/FolderTreeModel.h"

#include "models/DragMime.h"
#include "services/FolderService.h"
#include "services/NoteService.h"

#include <QLoggingCategory>
#include <QMimeData>

#include <algorithm>

namespace notes::models {

namespace {

Q_LOGGING_CATEGORY(lcFolderTree, "notes.models.foldertree")

using storage::DbResult;
using storage::FolderTreeEntry;

} // namespace

FolderTreeModel::FolderTreeModel(services::FolderService& folders, services::NoteService& notes,
                                 QObject* parent)
    : QAbstractItemModel(parent)
    , m_folders(folders)
    , m_notes(notes)
{
    rebuild({});

    using services::FolderService;
    connect(&m_folders, &FolderService::folderCreated, this, &FolderTreeModel::reload);
    connect(&m_folders, &FolderService::folderChanged, this, &FolderTreeModel::reload);
    connect(&m_folders, &FolderService::folderDeleted, this, &FolderTreeModel::reload);
    connect(&m_folders, &FolderService::foldersReordered, this, &FolderTreeModel::reload);

    using services::NoteService;
    connect(&m_notes, &NoteService::noteCreated, this, &FolderTreeModel::reloadCounts);
    connect(&m_notes, &NoteService::noteMoved, this, &FolderTreeModel::reloadCounts);
    connect(&m_notes, &NoteService::noteTrashed, this, &FolderTreeModel::reloadCounts);
    connect(&m_notes, &NoteService::noteRestored, this, &FolderTreeModel::reloadCounts);
    connect(&m_notes, &NoteService::notePurged, this, &FolderTreeModel::reloadCounts);

    reload();
}

FolderTreeModel::~FolderTreeModel() = default;

void FolderTreeModel::reload()
{
    // Jobs run in order on the DB thread, so the counts match the tree.
    m_folders.listTree().then(this, [this](const DbResult<QList<FolderTreeEntry>>& folders) {
        if (!folders) {
            // Category and code only: folder names are user content.
            qCWarning(lcFolderTree) << "Loading folders failed:" << folders.error().code;
            return;
        }
        beginResetModel();
        rebuild(folders.value());
        endResetModel();
    });
    reloadCounts();
}

void FolderTreeModel::reloadCounts()
{
    m_notes.counts().then(this, [this](const DbResult<QHash<qint64, int>>& counts) {
        if (!counts) {
            qCWarning(lcFolderTree) << "Loading note counts failed:" << counts.error().code;
            return;
        }
        applyCounts(counts.value());
        emit loaded();
    });
}

void FolderTreeModel::rebuild(const QList<FolderTreeEntry>& folders)
{
    m_nodes.clear();
    m_topLevel.clear();
    m_nodes.reserve(folders.size() + 3);

    using enum SidebarItemKind;
    const qsizetype account = addNode(AccountHeader, tr("On My Computer"), 0, -1);
    addNode(AllNotes, tr("All Notes"), kAllNotesFolderId, account);
    // listTree() puts each folder right after its parent, one level deeper, so
    // the last node seen at each depth is the parent of the next level down.
    QList<qsizetype> lastAtDepth{account};
    for (const FolderTreeEntry& entry : folders) {
        const auto depth = static_cast<qsizetype>(entry.depth);
        if (depth >= lastAtDepth.size()) {
            continue; // a skipped level can't happen; don't guess a parent
        }
        const qsizetype node = addNode(Folder, entry.folder.name, entry.folder.id,
                                       lastAtDepth.at(depth), entry.folder.uuid);
        lastAtDepth.resize(depth + 1);
        lastAtDepth.append(node);
    }
    addNode(RecentlyDeleted, tr("Recently Deleted"), kRecentlyDeletedFolderId, -1);
}

qsizetype FolderTreeModel::addNode(SidebarItemKind kind, const QString& name, qint64 folderId,
                                   qsizetype parent, const QString& uuid)
{
    const qsizetype node = m_nodes.size();
    QList<qsizetype>& siblings = parent < 0 ? m_topLevel : m_nodes[parent].children;
    m_nodes.append(
        Node{.kind = kind,
             .name = name,
             .folderId = folderId,
             .isDefault = uuid == QLatin1StringView(storage::FolderRepository::kDefaultFolderUuid),
             .uuid = uuid,
             .parent = parent,
             .row = static_cast<int>(siblings.size()),
             .children = {}});
    siblings.append(node);
    return node;
}

void FolderTreeModel::applyCounts(const QHash<qint64, int>& counts)
{
    m_counts = counts;
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).folderId != 0) {
            const QModelIndex changed = indexOf(i);
            emit dataChanged(changed, changed, {kCountRole});
        }
    }
}

const QList<qsizetype>& FolderTreeModel::childrenOf(const QModelIndex& parent) const
{
    return parent.isValid() ? m_nodes.at(static_cast<qsizetype>(parent.internalId())).children
                            : m_topLevel;
}

QModelIndex FolderTreeModel::indexOf(qsizetype node) const
{
    if (node < 0) {
        return {};
    }
    return createIndex(m_nodes.at(node).row, 0, static_cast<quintptr>(node));
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }
    return indexOf(childrenOf(parent).at(row));
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    return indexOf(m_nodes.at(static_cast<qsizetype>(child.internalId())).parent);
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    return static_cast<int>(childrenOf(parent).size());
}

int FolderTreeModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    // QAbstractItemModelTester passes invalid indexes on purpose; answer quietly.
    if (!index.isValid()) {
        return {};
    }
    Q_ASSERT(checkIndex(index));
    const Node& node = m_nodes.at(static_cast<qsizetype>(index.internalId()));
    switch (role) {
    case Qt::DisplayRole:
        return node.name;
    case kKindRole:
        return static_cast<int>(node.kind);
    case kFolderIdRole:
        return node.folderId == 0 ? QVariant() : QVariant(node.folderId);
    case kCountRole:
        return node.folderId == 0 ? QVariant() : QVariant(m_counts.value(node.folderId));
    case kIsDefaultFolderRole:
        return node.kind == SidebarItemKind::Folder ? QVariant(node.isDefault) : QVariant();
    default:
        return {};
    }
}

Qt::ItemFlags FolderTreeModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Q_ASSERT(checkIndex(index));
    // Only rows that select a set of notes are selectable.
    const Node& node = m_nodes.at(static_cast<qsizetype>(index.internalId()));
    Qt::ItemFlags flags =
        node.folderId == 0 ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    switch (node.kind) {
    case SidebarItemKind::Folder:
        flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        break;
    case SidebarItemKind::AccountHeader:   // folders: to the top level
    case SidebarItemKind::RecentlyDeleted: // notes: deleted
        flags |= Qt::ItemIsDropEnabled;
        break;
    default:
        break;
    }
    return flags;
}

const FolderTreeModel::Node* FolderTreeModel::nodeAt(const QModelIndex& index) const
{
    return index.isValid() ? &m_nodes.at(static_cast<qsizetype>(index.internalId())) : nullptr;
}

QStringList FolderTreeModel::mimeTypes() const
{
    return {QString(kNoteUuidsMime), QString(kFolderUuidsMime)};
}

QMimeData* FolderTreeModel::mimeData(const QModelIndexList& indexes) const
{
    QStringList uuids;
    for (const QModelIndex& index : indexes) {
        const Node* node = nodeAt(index);
        if (node != nullptr && node->kind == SidebarItemKind::Folder &&
            !uuids.contains(node->uuid)) {
            uuids.append(node->uuid);
        }
    }
    if (uuids.isEmpty()) {
        return nullptr;
    }
    auto* data = new QMimeData;
    data->setData(QString(kFolderUuidsMime), encodeUuids(uuids));
    return data;
}

Qt::DropActions FolderTreeModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions FolderTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QList<qsizetype> FolderTreeModel::draggedFolders(const QMimeData* data) const
{
    QList<qsizetype> nodes;
    for (const QString& uuid : decodeUuids(data->data(QString(kFolderUuidsMime)))) {
        const auto found = std::ranges::find_if(m_nodes, [&](const Node& node) {
            return node.kind == SidebarItemKind::Folder && node.uuid == uuid;
        });
        if (found == m_nodes.end()) {
            return {}; // gone meanwhile
        }
        nodes.append(std::distance(m_nodes.begin(), found));
    }
    return nodes;
}

bool FolderTreeModel::isSelfOrAncestor(qsizetype candidate, qsizetype node) const
{
    for (qsizetype current = node; current >= 0; current = m_nodes.at(current).parent) {
        if (current == candidate) {
            return true;
        }
    }
    return false;
}

std::optional<FolderTreeModel::FolderDrop>
FolderTreeModel::folderDropTarget(int row, const QModelIndex& parent) const
{
    const Node* target = nodeAt(parent);
    if (target == nullptr) {
        return std::nullopt; // beside "On My Computer" or Recently Deleted
    }
    const auto targetNode = static_cast<qsizetype>(parent.internalId());
    if (target->kind == SidebarItemKind::AccountHeader) {
        // Its first row is All Notes: folders go after it.
        return FolderDrop{.parentId = std::nullopt,
                          .parentNode = targetNode,
                          .position = row < 0 ? -1 : std::max(0, row - 1)};
    }
    if (target->kind == SidebarItemKind::Folder) {
        return FolderDrop{.parentId = target->folderId, .parentNode = targetNode, .position = row};
    }
    return std::nullopt;
}

bool FolderTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                      int /*column*/, const QModelIndex& parent) const
{
    if (data == nullptr || action != Qt::MoveAction) {
        return false;
    }
    if (data->hasFormat(QString(kNoteUuidsMime))) {
        // Onto a folder or Recently Deleted, not between rows.
        const Node* target = nodeAt(parent);
        return row < 0 && target != nullptr &&
               (target->kind == SidebarItemKind::Folder ||
                target->kind == SidebarItemKind::RecentlyDeleted);
    }
    if (!data->hasFormat(QString(kFolderUuidsMime))) {
        return false;
    }
    const QList<qsizetype> dragged = draggedFolders(data);
    const std::optional<FolderDrop> drop = folderDropTarget(row, parent);
    if (dragged.isEmpty() || !drop) {
        return false;
    }
    // Never into itself or one of its subfolders (TC-UI-026); "Notes" stays at
    // the top level.
    return std::ranges::all_of(dragged, [&](qsizetype node) {
        return !isSelfOrAncestor(node, drop->parentNode) &&
               !(m_nodes.at(node).isDefault && drop->parentId);
    });
}

bool FolderTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                   int column, const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent)) {
        return false;
    }
    if (data->hasFormat(QString(kNoteUuidsMime))) {
        dropNotes(decodeUuids(data->data(QString(kNoteUuidsMime))), *nodeAt(parent));
        return true;
    }
    const std::optional<FolderDrop> drop = folderDropTarget(row, parent);
    if (!drop) {
        return false;
    }
    dropFolders(draggedFolders(data), *drop);
    return true;
}

void FolderTreeModel::dropNotes(const QStringList& uuids, const Node& target)
{
    const bool toTrash = target.kind == SidebarItemKind::RecentlyDeleted;
    const qint64 folderId = target.folderId;
    m_notes.liveIdsForUuids(uuids).then(
        this, [this, toTrash, folderId](const DbResult<QList<qint64>>& ids) {
            if (!ids) {
                qCWarning(lcFolderTree) << "Dropping notes failed:" << ids.error().code;
                return;
            }
            for (const qint64 id : ids.value()) {
                if (toTrash) {
                    (void)m_notes.trash(id);
                } else {
                    (void)m_notes.moveToFolder(id, folderId);
                }
            }
        });
}

void FolderTreeModel::dropFolders(const QList<qsizetype>& dragged, const FolderDrop& drop)
{
    QList<qint64> draggedIds;
    for (const qsizetype node : dragged) {
        const Node& folder = m_nodes.at(node);
        draggedIds.append(folder.folderId);
        const Node& oldParent = m_nodes.at(folder.parent);
        const std::optional<qint64> oldParentId = oldParent.kind == SidebarItemKind::Folder
                                                      ? std::optional<qint64>(oldParent.folderId)
                                                      : std::nullopt;
        if (oldParentId != drop.parentId) {
            (void)m_folders.move(folder.folderId, drop.parentId);
        }
    }
    if (drop.position >= 0) {
        // Queued after the moves, so the dragged folders are already there.
        (void)m_folders.reorder(drop.parentId, orderAfterDrop(draggedIds, drop));
    }
}

QList<qint64> FolderTreeModel::orderAfterDrop(const QList<qint64>& draggedIds,
                                              const FolderDrop& drop) const
{
    QList<qint64> order;
    int position = drop.position;
    int index = 0;
    for (const qsizetype child : m_nodes.at(drop.parentNode).children) {
        const Node& sibling = m_nodes.at(child);
        if (sibling.kind != SidebarItemKind::Folder) {
            continue; // All Notes, under "On My Computer"
        }
        if (!draggedIds.contains(sibling.folderId)) {
            order.append(sibling.folderId);
        } else if (index < drop.position) {
            --position; // the gap it leaves closes up
        }
        ++index;
    }
    position = std::clamp(position, 0, static_cast<int>(order.size()));
    for (qsizetype i = 0; i < draggedIds.size(); ++i) {
        order.insert(position + i, draggedIds.at(i));
    }
    return order;
}

} // namespace notes::models
