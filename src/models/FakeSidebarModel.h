#pragma once

#include "models/SidebarRoles.h"

#include <QAbstractItemModel>
#include <QString>

#include <memory>
#include <vector>

namespace notes::models {

// In-memory sidebar tree with sample folders and tags, for the Phase 1 UI.
// Replaced by the database-backed FolderTreeModel in task 2.8.
class FakeSidebarModel : public QAbstractItemModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FakeSidebarModel)

public:
    explicit FakeSidebarModel(QObject* parent = nullptr);
    ~FakeSidebarModel() override;

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Rebuilds the sample data with a model reset, the way the real model will
    // reload after sync. Lets tests check that view state survives resets.
    void reload();

private:
    struct Node;

    void populate();
    [[nodiscard]] const Node* nodeAt(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexOf(const Node* node) const;

    std::unique_ptr<Node> m_root;
};

} // namespace notes::models
