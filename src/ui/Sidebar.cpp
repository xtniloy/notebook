#include "ui/Sidebar.h"

#include "models/SidebarRoles.h"
#include "ui/Icons.h"
#include "ui/SidebarDelegate.h"

#include <QDragMoveEvent>
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

// Draws the expand/collapse arrows with the themed icons; a style sheet
// `image: url(...)` can't follow the palette, so the arrows vanished in dark mode.
class SidebarTree : public QTreeView
{
public:
    using QTreeView::QTreeView;

protected:
    // Drop highlight (task 2.12): the row a drag would drop onto is outlined by
    // SidebarDelegate, which reads it from the kDropTargetProperty property.
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        QTreeView::dragMoveEvent(event);
        const bool onItem = event->isAccepted() && dropIndicatorPosition() == OnItem;
        setDropTarget(onItem ? indexAt(event->position().toPoint()) : QModelIndex());
    }
    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        QTreeView::dragLeaveEvent(event);
        setDropTarget({});
    }
    void dropEvent(QDropEvent* event) override
    {
        QTreeView::dropEvent(event);
        setDropTarget({});
    }

    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override
    {
        if (model() == nullptr || !model()->hasChildren(index)) {
            return;
        }
        const int side = indentation();
        const QRect arrow(rect.right() - side + 1, rect.top(), side, rect.height());
        const bool selected = selectionModel() != nullptr && selectionModel()->isSelected(index);
        icon(isExpanded(index) ? Icon::Collapse : Icon::Expand)
            .paint(painter, arrow.adjusted(4, 4, -4, -4), Qt::AlignCenter,
                   selected ? QIcon::Selected : QIcon::Normal);
    }

private:
    void setDropTarget(const QModelIndex& index)
    {
        const QPersistentModelIndex target(index);
        if (property(SidebarDelegate::kDropTargetProperty).value<QPersistentModelIndex>() !=
            target) {
            setProperty(SidebarDelegate::kDropTargetProperty, QVariant::fromValue(target));
            viewport()->update();
        }
    }
};

} // namespace

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
    , m_tree(new SidebarTree(this))
    , m_newFolderButton(new QToolButton(this))
{
    // Lets the style sheet paint the pane background (#sidebarPane).
    setAttribute(Qt::WA_StyledBackground);

    m_tree->setObjectName(QStringLiteral("sidebarTree"));
    m_tree->setHeaderHidden(true);
    // Section headers sit flush left without an expand arrow.
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Notes dragged from the list, and folders moved within the tree (task 2.12).
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);
    m_tree->setItemDelegate(new SidebarDelegate(m_tree));
    m_tree->setAttribute(Qt::WA_MacShowFocusRect, false);
    // The delegate draws its own rounded highlight; keep the style's square one
    // out of the row and branch area. Theme colors come with task 1.8.
    m_tree->setStyleSheet(
        QStringLiteral("QTreeView { background: transparent; show-decoration-selected: 0; }"
                       "QTreeView::item:selected { background: transparent; }"));

    connect(m_tree, &QTreeView::expanded, this,
            [this](const QModelIndex& index) { onExpandedChanged(index, true); });
    connect(m_tree, &QTreeView::collapsed, this,
            [this](const QModelIndex& index) { onExpandedChanged(index, false); });

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = m_tree->indexAt(position);
        const std::optional<qint64> id = folderIdOf(index);
        if (id && index.data(models::kKindRole).toInt() ==
                      static_cast<int>(models::SidebarItemKind::Folder)) {
            emit folderMenuRequested(*id, m_tree->viewport()->mapToGlobal(position));
        }
    });

    m_newFolderButton->setObjectName(QStringLiteral("newFolderButton"));
    m_newFolderButton->setIcon(icon(Icon::NewFolder));
    m_newFolderButton->setText(tr("New Folder"));
    m_newFolderButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_newFolderButton->setAutoRaise(true);
    connect(m_newFolderButton, &QToolButton::clicked, this, &Sidebar::newFolderRequested);

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
    connect(model, &QAbstractItemModel::modelReset, this, &Sidebar::adoptFirstFolder);
    connect(model, &QAbstractItemModel::rowsInserted, this, &Sidebar::adoptFirstFolder);

    if (!m_currentFolder) {
        pickFirstFolder();
    }
    restoreViewState();
}

bool Sidebar::pickFirstFolder()
{
    forEachRow(*m_model, [this](const QModelIndex& index) {
        if (!m_currentFolder && (index.flags() & Qt::ItemIsSelectable) &&
            index.data(models::kKindRole).toInt() ==
                static_cast<int>(models::SidebarItemKind::Folder)) {
            m_currentFolder = folderIdOf(index);
        }
    });
    return m_currentFolder.has_value();
}

void Sidebar::adoptFirstFolder()
{
    restoreViewState(); // drops the current folder if it is gone
    if (m_currentFolder || !pickFirstFolder()) {
        return;
    }
    const qint64 folderId = m_currentFolder.value_or(0); // set by pickFirstFolder()
    restoreViewState();
    emit folderSelected(folderId);
}

bool Sidebar::selectFolder(qint64 folderId)
{
    const QModelIndex index = indexForFolder(folderId);
    if (!index.isValid() || !(index.flags() & Qt::ItemIsSelectable)) {
        return false;
    }
    m_tree->scrollTo(index); // expands its parents
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
