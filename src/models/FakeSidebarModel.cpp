#include "models/FakeSidebarModel.h"

#include <functional>
#include <utility>

namespace notes::models {

namespace {
constexpr int kNoCount = -1;
constexpr qint64 kNoFolder = 0;
} // namespace

struct FakeSidebarModel::Node
{
    Node(SidebarItemKind nodeKind, QString nodeName, qint64 nodeFolderId, int nodeCount,
         Node* parentNode)
        : kind(nodeKind)
        , name(std::move(nodeName))
        , folderId(nodeFolderId)
        , count(nodeCount)
        , parent(parentNode)
    {}

    SidebarItemKind kind;
    QString name;
    qint64 folderId;
    int count;
    Node* parent;
    std::vector<std::unique_ptr<Node>> children;

    Node* add(SidebarItemKind childKind, QString childName, qint64 childId = kNoFolder,
              int childCount = kNoCount)
    {
        return children
            .emplace_back(
                std::make_unique<Node>(childKind, std::move(childName), childId, childCount, this))
            .get();
    }

    [[nodiscard]] int row() const
    {
        if (parent == nullptr) {
            return 0;
        }
        for (std::size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == this) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }
};

FakeSidebarModel::FakeSidebarModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    populate();
}

FakeSidebarModel::~FakeSidebarModel() = default;

void FakeSidebarModel::populate()
{
    using enum SidebarItemKind;
    m_root = std::make_unique<Node>(SectionHeader, QString(), kNoFolder, kNoCount, nullptr);

    Node* account = m_root->add(AccountHeader, tr("On My Computer"));
    Node* allNotes = account->add(AllNotes, tr("All Notes"), kAllNotesFolderId);
    account->add(Folder, tr("Notes"), 1, 12);
    Node* work = account->add(Folder, QStringLiteral("Work"), 2, 8);
    Node* projects = work->add(Folder, QStringLiteral("Projects"), 3, 5);
    projects->add(Folder, QStringLiteral("Archive"), 4, 2);
    work->add(Folder, QStringLiteral("Meetings"), 5, 3);
    Node* personal = account->add(Folder, QStringLiteral("Personal"), 6, 6);
    personal->add(Folder, QStringLiteral("Recipes"), 7, 4);
    account->add(Folder, QStringLiteral("Passwords"), 8, 2);

    // Tags become real in Phase 7; for now they are display-only.
    Node* tags = m_root->add(SectionHeader, tr("Tags"));
    tags->add(Tag, QStringLiteral("#work"));
    tags->add(Tag, QStringLiteral("#home"));

    m_root->add(RecentlyDeleted, tr("Recently Deleted"), kRecentlyDeletedFolderId, 3);

    // All Notes counts every folder's own notes (Recently Deleted excluded).
    int total = 0;
    const std::function<void(const Node&)> sum = [&](const Node& node) {
        if (node.kind == Folder) {
            total += node.count;
        }
        for (const auto& child : node.children) {
            sum(*child);
        }
    };
    sum(*account);
    allNotes->count = total;
}

void FakeSidebarModel::reload()
{
    beginResetModel();
    populate();
    endResetModel();
}

const FakeSidebarModel::Node* FakeSidebarModel::nodeAt(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<const Node*>(index.internalPointer()) : m_root.get();
}

QModelIndex FakeSidebarModel::indexOf(const Node* node) const
{
    if (node == nullptr || node == m_root.get()) {
        return {};
    }
    // createIndex() takes a non-const pointer; nodes are never modified through it.
    return createIndex(node->row(), 0, const_cast<Node*>(node)); // NOLINT(*-const-cast)
}

QModelIndex FakeSidebarModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }
    return indexOf(nodeAt(parent)->children.at(static_cast<std::size_t>(row)).get());
}

QModelIndex FakeSidebarModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    return indexOf(nodeAt(child)->parent);
}

int FakeSidebarModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    return static_cast<int>(nodeAt(parent)->children.size());
}

int FakeSidebarModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 1;
}

QVariant FakeSidebarModel::data(const QModelIndex& index, int role) const
{
    // QAbstractItemModelTester passes invalid indexes on purpose; answer quietly.
    if (!index.isValid()) {
        return {};
    }
    Q_ASSERT(checkIndex(index));
    const Node* node = nodeAt(index);
    switch (role) {
    case Qt::DisplayRole:
        return node->name;
    case kKindRole:
        return static_cast<int>(node->kind);
    case kFolderIdRole:
        return node->folderId == kNoFolder ? QVariant() : QVariant(node->folderId);
    case kCountRole:
        return node->count == kNoCount ? QVariant() : QVariant(node->count);
    default:
        return {};
    }
}

Qt::ItemFlags FakeSidebarModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Q_ASSERT(checkIndex(index));
    // Only rows that select a set of notes are selectable.
    return nodeAt(index)->folderId == kNoFolder ? Qt::ItemIsEnabled
                                                : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace notes::models
