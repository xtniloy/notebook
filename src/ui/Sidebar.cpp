#include "ui/Sidebar.h"

#include "models/SidebarRoles.h"
#include "ui/Icons.h"
#include "ui/SidebarDelegate.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace notes::ui {

namespace {

std::optional<qint64> folderIdOf(const QModelIndex& index)
{
    const QVariant id = index.data(models::kFolderIdRole);
    if (!id.isValid()) {
        return std::nullopt;
    }
    return id.toLongLong();
}

// Visits every row of the model, parents before their children.
void forEachRow(const QAbstractItemModel& model,
                const std::function<void(const QModelIndex&)>& visit)
{
    QList<QModelIndex> pending{QModelIndex()};
    while (!pending.isEmpty()) {
        const QModelIndex parent = pending.takeLast();
        for (int row = model.rowCount(parent) - 1; row >= 0; --row) {
            pending.append(model.index(row, 0, parent));
        }
        if (parent.isValid()) {
            visit(parent);
        }
    }
}

} // namespace

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
    , m_tree(new QTreeView(this))
    , m_newFolderButton(new QToolButton(this))
{
    m_tree->setObjectName(QStringLiteral("sidebarTree"));
    m_tree->setHeaderHidden(true);
    // Section headers sit flush left without an expand arrow.
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setItemDelegate(new SidebarDelegate(m_tree));
    m_tree->setAttribute(Qt::WA_MacShowFocusRect, false);
    // The delegate draws its own rounded highlight; keep the style's square one
    // out of the row and branch area. Theme colors come with task 1.8.
    m_tree->setStyleSheet(
        QStringLiteral("QTreeView { background: transparent; show-decoration-selected: 0; }"
                       "QTreeView::item:selected { background: transparent; }"
                       "QTreeView::branch:has-children:closed { image: url(%1); }"
                       "QTreeView::branch:has-children:open { image: url(%2); }")
            .arg(iconPath(Icon::Expand), iconPath(Icon::Collapse)));

    connect(m_tree, &QTreeView::expanded, this,
            [this](const QModelIndex& index) { onExpandedChanged(index, true); });
    connect(m_tree, &QTreeView::collapsed, this,
            [this](const QModelIndex& index) { onExpandedChanged(index, false); });

    // Folder creation arrives with the FolderService (task 2.7).
    m_newFolderButton->setObjectName(QStringLiteral("newFolderButton"));
    m_newFolderButton->setIcon(icon(Icon::NewFolder));
    m_newFolderButton->setText(tr("New Folder"));
    m_newFolderButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_newFolderButton->setAutoRaise(true);
    m_newFolderButton->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(0);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_newFolderButton, 0, Qt::AlignLeft);
}

Sidebar::~Sidebar() = default;

void Sidebar::setModel(QAbstractItemModel* model)
{
    if (m_model != nullptr) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    // QAbstractItemView::setModel() replaces the selection model without deleting
    // the old one.
    const QItemSelectionModel* oldSelection = m_tree->selectionModel();
    m_model = model;
    m_tree->setModel(model);
    delete oldSelection;

    if (model == nullptr) {
        return;
    }
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection& selected) { onSelectionChanged(selected); });
    // Connected after m_tree->setModel(), so this runs after the view's own reset
    // handling, which collapses everything.
    connect(model, &QAbstractItemModel::modelReset, this, &Sidebar::restoreViewState);

    if (!m_currentFolder) {
        forEachRow(*model, [this](const QModelIndex& index) {
            if (!m_currentFolder && (index.flags() & Qt::ItemIsSelectable) &&
                index.data(models::kKindRole).toInt() ==
                    static_cast<int>(models::SidebarItemKind::Folder)) {
                m_currentFolder = folderIdOf(index);
            }
        });
    }
    restoreViewState();
}

bool Sidebar::selectFolder(qint64 folderId)
{
    const QModelIndex index = indexForFolder(folderId);
    if (!index.isValid() || !(index.flags() & Qt::ItemIsSelectable)) {
        return false;
    }
    m_tree->setCurrentIndex(index);
    return true;
}

void Sidebar::onSelectionChanged(const QItemSelection& selected)
{
    if (m_restoring) {
        return;
    }
    const QModelIndexList indexes = selected.indexes();
    const std::optional<qint64> id =
        indexes.isEmpty() ? std::nullopt : folderIdOf(indexes.constFirst());
    if (!id) {
        // Clicking a header or tag clears the selection; keep the folder selected.
        restoreViewState();
        return;
    }
    if (id != m_currentFolder) {
        m_currentFolder = id;
        emit folderSelected(*id);
    }
}

void Sidebar::onExpandedChanged(const QModelIndex& index, bool expanded)
{
    if (m_restoring) {
        return;
    }
    if (const std::optional<qint64> id = folderIdOf(index)) {
        if (expanded) {
            m_expandedFolders.insert(*id);
        } else {
            m_expandedFolders.remove(*id);
        }
    }
}

void Sidebar::restoreViewState()
{
    if (m_model == nullptr) {
        return;
    }
    m_restoring = true;
    forEachRow(*m_model, [this](const QModelIndex& index) {
        const std::optional<qint64> id = folderIdOf(index);
        // Section headers have no id and are always open.
        if (!id || m_expandedFolders.contains(*id)) {
            m_tree->expand(index);
        }
    });
    if (m_currentFolder) {
        const QModelIndex current = indexForFolder(*m_currentFolder);
        if (current.isValid()) {
            m_tree->setCurrentIndex(current);
        } else {
            m_currentFolder.reset(); // the folder is gone
        }
    }
    m_restoring = false;
}

QModelIndex Sidebar::indexForFolder(qint64 folderId) const
{
    if (m_model == nullptr) {
        return {};
    }
    const QModelIndexList matches =
        m_model->match(m_model->index(0, 0), models::kFolderIdRole, folderId, 1,
                       Qt::MatchExactly | Qt::MatchRecursive);
    return matches.value(0);
}

} // namespace notes::ui
