#pragma once

#include <QSet>
#include <QWidget>

#include <optional>

class QAbstractItemModel;
class QItemSelection;
class QToolButton;
class QTreeView;

namespace notes::ui {

// Folder sidebar (Plan §3.3): account section with All Notes, Notes and user
// folders, Tags, Recently Deleted, and a "New Folder" button at the bottom.
// Works with any model that provides the roles in models/SidebarRoles.h.
//
// Which folders are expanded and which one is selected survive model resets
// for the rest of the session (they are not saved to QSettings).
class Sidebar : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Sidebar)

public:
    explicit Sidebar(QWidget* parent = nullptr);
    ~Sidebar() override;

    // Not owned. Selects the first folder row ("Notes" with the default data)
    // when nothing was selected before.
    void setModel(QAbstractItemModel* model);

    [[nodiscard]] std::optional<qint64> currentFolder() const { return m_currentFolder; }
    // Returns false if no selectable row has this id.
    bool selectFolder(qint64 folderId);

signals:
    // A folder, All Notes or Recently Deleted was selected (ids in SidebarRoles.h).
    void folderSelected(qint64 folderId);

private:
    void onSelectionChanged(const QItemSelection& selected);
    void onExpandedChanged(const QModelIndex& index, bool expanded);
    void restoreViewState();
    [[nodiscard]] QModelIndex indexForFolder(qint64 folderId) const;

    QTreeView* m_tree;
    QToolButton* m_newFolderButton;
    QAbstractItemModel* m_model = nullptr;

    QSet<qint64> m_expandedFolders;
    std::optional<qint64> m_currentFolder;
    // Set while restoreViewState() expands rows and reselects, so those changes
    // aren't recorded or emitted as user actions.
    bool m_restoring = false;
};

} // namespace notes::ui
