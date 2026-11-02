#include "ui/NoteListView.h"

#include "models/NoteListRoles.h"
#include "ui/NoteListDelegate.h"

#include <QItemSelectionModel>

#include <algorithm>

namespace notes::ui {

namespace {

std::optional<qint64> noteIdOf(const QModelIndex& index)
{
    const QVariant id = index.data(models::kNoteIdRole);
    if (!id.isValid()) {
        return std::nullopt;
    }
    return id.toLongLong();
}

} // namespace

NoteListView::NoteListView(QWidget* parent)
    : QListView(parent)
{
    // Not uniform: headers are compact (UI review). Row heights come from the
    // delegate's constant-time sizeHint(), so laying out 10,000 rows stays cheap.
    setUniformItemSizes(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Notes can be dragged onto sidebar folders (task 2.12); the list itself
    // takes no drops.
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::MoveAction);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    // The base color under the scroll bar too (AppStyle draws it transparent),
    // so the list is one white (or dark) column, not a gray strip at its edge.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    // A little space under the last note, so it doesn't touch the bottom edge
    // when scrolled to the end (UI review).
    setViewportMargins(0, 0, 0, kBottomPadding);
    setItemDelegate(new NoteListDelegate(this));
    // The delegate draws its own rounded highlight. Theme colors come with task 1.8.
    setStyleSheet(QStringLiteral("QListView { show-decoration-selected: 0; }"
                                 "QListView::item:selected { background: transparent; }"));
}

NoteListView::~NoteListView() = default;

void NoteListView::setModel(QAbstractItemModel* model)
{
    if (const QAbstractItemModel* old = this->model()) {
        disconnect(old, nullptr, this, nullptr);
    }
    // QAbstractItemView::setModel() replaces the selection model without deleting
    // the old one.
    const QItemSelectionModel* oldSelection = selectionModel();
    QListView::setModel(model);
    delete oldSelection;

    if (model == nullptr) {
        return;
    }
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &NoteListView::onSelectionChanged);
    // Connected after QListView::setModel(), so these run after the view's own
    // handling, which clears the selection on reset.
    connect(model, &QAbstractItemModel::modelReset, this, &NoteListView::restoreSelection);
    connect(model, &QAbstractItemModel::modelReset, this, &NoteListView::scheduleCheck);
    connect(model, &QAbstractItemModel::rowsInserted, this, &NoteListView::scheduleCheck);
    connect(model, &QAbstractItemModel::rowsRemoved, this, [this] {
        m_removingRows = false;
        // Undo Qt's move to a neighbour now, or it would be reported later as a
        // selection change. A note that is gone leaves nothing selected until
        // checkCurrentNote() picks one.
        m_restoring = true;
        if (const QModelIndex index = m_currentNote ? indexForNote(*m_currentNote) : QModelIndex();
            index.isValid()) {
            setCurrentIndex(index);
        } else {
            selectionModel()->clear();
        }
        m_restoring = false;
        scheduleCheck();
    });

    if (!m_currentNote) {
        for (int row = 0; row < model->rowCount(); ++row) {
            if (const std::optional<qint64> id = noteIdOf(model->index(row, 0))) {
                m_currentNote = id;
                break;
            }
        }
    }
    restoreSelection();
}

bool NoteListView::selectNote(qint64 noteId)
{
    const QModelIndex index = indexForNote(noteId);
    if (!index.isValid()) {
        return false;
    }
    setCurrentIndex(index);
    scrollTo(index);
    return true;
}

void NoteListView::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    m_removingRows = true;
    if (const int current = currentIndex().row();
        !parent.isValid() && current >= start && current <= end) {
        m_removedAt = start; // the rows after the block move up to here
    }
    QListView::rowsAboutToBeRemoved(parent, start, end);
}

void NoteListView::onSelectionChanged()
{
    if (m_restoring || m_removingRows) {
        return;
    }
    const QModelIndexList selected = selectionModel()->selectedIndexes();
    const std::optional<qint64> id =
        selected.isEmpty() ? std::nullopt : noteIdOf(selected.constFirst());
    if (!id) {
        // Nothing selectable was hit (e.g. Ctrl+click on the selected note).
        restoreSelection();
        return;
    }
    if (id != m_currentNote) {
        m_currentNote = id;
        emit noteSelected(*id);
    }
}

void NoteListView::restoreSelection()
{
    if (!m_currentNote) {
        return;
    }
    m_restoring = true;
    // If the note is gone, checkCurrentNote() moves on.
    if (const QModelIndex index = indexForNote(*m_currentNote); index.isValid()) {
        setCurrentIndex(index);
    }
    m_restoring = false;
}

void NoteListView::scheduleCheck()
{
    if (!m_checkPending) {
        m_checkPending = true;
        QMetaObject::invokeMethod(this, &NoteListView::checkCurrentNote, Qt::QueuedConnection);
    }
}

void NoteListView::checkCurrentNote()
{
    m_checkPending = false;
    if (model() == nullptr) {
        return;
    }
    if (m_currentNote && indexForNote(*m_currentNote).isValid()) {
        m_removedAt = -1;
        restoreSelection(); // back after a move
        return;
    }
    const bool hadNote = m_currentNote.has_value();
    m_currentNote.reset();
    const int rows = model()->rowCount();
    // The note after the removed one, else the one before; the first note if
    // nothing was removed (e.g. the first page arrived).
    const int from = m_removedAt < 0 ? 0 : std::min(m_removedAt, rows - 1);
    m_removedAt = -1;
    std::optional<qint64> next;
    for (int row = from; row < rows && !next; ++row) {
        next = noteIdOf(model()->index(row, 0));
    }
    for (int row = from - 1; row >= 0 && !next; --row) {
        next = noteIdOf(model()->index(row, 0));
    }
    if (next) {
        m_currentNote = next;
        restoreSelection();
        emit noteSelected(*next);
        return;
    }
    if (hadNote) {
        emit currentNoteCleared();
    }
}

QModelIndex NoteListView::indexForNote(qint64 noteId) const
{
    const QAbstractItemModel* model = this->model();
    if (model == nullptr || model->rowCount() == 0) {
        return {};
    }
    return model->match(model->index(0, 0), models::kNoteIdRole, noteId, 1, Qt::MatchExactly)
        .value(0);
}

} // namespace notes::ui
