#pragma once

#include "models/SidebarRoles.h"
#include "storage/FolderRepository.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace notes::services {
class FolderService;
class NoteService;
} // namespace notes::services

namespace notes::models {

// The sidebar tree from FolderService and NoteService (task 2.8), with the
// roles in SidebarRoles.h, so the Sidebar widget shows it like the Phase 1
// fake model (Plan §3.3):
//
//   On My Computer
//     All Notes          (kAllNotesFolderId)
//     Notes, Work, ...   (folders, nested)
//   Recently Deleted     (kRecentlyDeletedFolderId)
//
// Tags join in Phase 7. Loads asynchronously: it starts with All Notes and
// Recently Deleted only. Folder changes reload the tree with a model reset
// (the Sidebar keeps its selection and expanded folders across resets); note
// changes only update the count badges, with dataChanged().
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FolderTreeModel)

public:
    // Both must outlive the model.
    FolderTreeModel(services::FolderService& folders, services::NoteService& notes,
                    QObject* parent = nullptr);
    ~FolderTreeModel() override;

    // Reloads folders and counts (done on folder signals; also after sync).
    void reload();
    // Reloads the count badges only (done on note signals).
    void reloadCounts();

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Drag & drop (task 2.12). Notes (kNoteUuidsMime) can be dropped on a folder
    // to move them there, or on Recently Deleted to delete them. Folders
    // (kFolderUuidsMime) can be dropped on a folder to nest them, on "On My
    // Computer" to make them top-level, or between rows to reorder them. A
    // folder never goes into itself or its own subfolders (TC-UI-026), and
    // "Notes" only reorders. Drops act through the services; the tree and
    // counts update from their signals.
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;
    [[nodiscard]] bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                       int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

signals:
    // After each finished reload() or reloadCounts(), for tests and startup.
    void loaded();

private:
    // Rows live in a flat list; index() stores a row's position in it.
    struct Node
    {
        SidebarItemKind kind = SidebarItemKind::SectionHeader;
        QString name;
        qint64 folderId = 0;    // 0: selects nothing (headers)
        bool isDefault = false; // the "Notes" folder
        QString uuid;           // folders only, for drag & drop
        qsizetype parent = -1;  // -1: top level
        int row = 0;            // among its parent's children
        QList<qsizetype> children;
    };

    void rebuild(const QList<storage::FolderTreeEntry>& folders);
    qsizetype addNode(SidebarItemKind kind, const QString& name, qint64 folderId, qsizetype parent,
                      const QString& uuid = {});
    void applyCounts(const QHash<qint64, int>& counts);
    [[nodiscard]] const QList<qsizetype>& childrenOf(const QModelIndex& parent) const;
    [[nodiscard]] QModelIndex indexOf(qsizetype node) const;
    [[nodiscard]] const Node* nodeAt(const QModelIndex& index) const;

    // Where a folder drop lands: the new parent (nullopt: top level), and the
    // position among its subfolders (-1: keep the order, append).
    struct FolderDrop
    {
        std::optional<qint64> parentId;
        qsizetype parentNode = -1;
        int position = -1;
    };
    [[nodiscard]] std::optional<FolderDrop> folderDropTarget(int row,
                                                             const QModelIndex& parent) const;
    // The dragged folders' nodes; empty if any is unknown.
    [[nodiscard]] QList<qsizetype> draggedFolders(const QMimeData* data) const;
    [[nodiscard]] bool isSelfOrAncestor(qsizetype candidate, qsizetype node) const;
    void dropNotes(const QStringList& uuids, const Node& target);
    void dropFolders(const QList<qsizetype>& dragged, const FolderDrop& drop);
    // The new parent's subfolder order: the dragged ones at the drop position.
    [[nodiscard]] QList<qint64> orderAfterDrop(const QList<qint64>& draggedIds,
                                               const FolderDrop& drop) const;

    services::FolderService& m_folders;
    services::NoteService& m_notes;
    QList<Node> m_nodes;
    QList<qsizetype> m_topLevel;
    QHash<qint64, int> m_counts; // folder id (or the two virtual ids) -> notes
};

} // namespace notes::models
