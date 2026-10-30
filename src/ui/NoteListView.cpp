#include "ui/NoteListView.h"

#include "models/NoteListRoles.h"
#include "ui/NoteListDelegate.h"

#include <QItemSelectionModel>

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
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
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
    // Connected after QListView::setModel(), so this runs after the view's own
    // reset handling, which clears the selection.
    connect(model, &QAbstractItemModel::modelReset, this, &NoteListView::restoreSelection);

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

void NoteListView::onSelectionChanged()
{
    if (m_restoring) {
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
    const QModelIndex index = indexForNote(*m_currentNote);
    if (index.isValid()) {
        setCurrentIndex(index);
    } else {
        m_currentNote.reset(); // the note is gone
    }
    m_restoring = false;
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
