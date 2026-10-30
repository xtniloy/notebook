#include "models/FolderTreeModel.h"

#include "services/FolderService.h"
#include "services/NoteService.h"

#include <QLoggingCategory>

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
        const qsizetype node = addNode(
            Folder, entry.folder.name, entry.folder.id, lastAtDepth.at(depth),
            entry.folder.uuid == QLatin1StringView(storage::FolderRepository::kDefaultFolderUuid));
        lastAtDepth.resize(depth + 1);
        lastAtDepth.append(node);
    }
    addNode(RecentlyDeleted, tr("Recently Deleted"), kRecentlyDeletedFolderId, -1);
}

qsizetype FolderTreeModel::addNode(SidebarItemKind kind, const QString& name, qint64 folderId,
                                   qsizetype parent, bool isDefault)
{
    const qsizetype node = m_nodes.size();
    QList<qsizetype>& siblings = parent < 0 ? m_topLevel : m_nodes[parent].children;
    m_nodes.append(Node{.kind = kind,
                        .name = name,
                        .folderId = folderId,
                        .isDefault = isDefault,
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
    return m_nodes.at(static_cast<qsizetype>(index.internalId())).folderId == 0
               ? Qt::ItemIsEnabled
               : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace notes::models
