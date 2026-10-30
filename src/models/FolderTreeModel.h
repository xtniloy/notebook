#pragma once

#include "models/SidebarRoles.h"
#include "storage/FolderRepository.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QString>

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
        qsizetype parent = -1;  // -1: top level
        int row = 0;            // among its parent's children
        QList<qsizetype> children;
    };

    void rebuild(const QList<storage::FolderTreeEntry>& folders);
    qsizetype addNode(SidebarItemKind kind, const QString& name, qint64 folderId, qsizetype parent,
                      bool isDefault = false);
    void applyCounts(const QHash<qint64, int>& counts);
    [[nodiscard]] const QList<qsizetype>& childrenOf(const QModelIndex& parent) const;
    [[nodiscard]] QModelIndex indexOf(qsizetype node) const;

    services::FolderService& m_folders;
    services::NoteService& m_notes;
    QList<Node> m_nodes;
    QList<qsizetype> m_topLevel;
    QHash<qint64, int> m_counts; // folder id (or the two virtual ids) -> notes
};

} // namespace notes::models
