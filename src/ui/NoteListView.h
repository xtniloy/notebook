#pragma once

#include <QListView>

#include <optional>

namespace notes::ui {

// The note list (Plan §3.4): notes under date section headers, newest first.
// Works with any model that provides the roles in models/NoteListRoles.h.
// Only visible rows are painted, so 10,000 notes scroll smoothly (TC-PERF-001).
// Section headers are shorter than note rows, so item sizes aren't uniform.
class NoteListView : public QListView
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NoteListView)

public:
    // Space under the last note (UI review).
    static constexpr int kBottomPadding = 12;

    explicit NoteListView(QWidget* parent = nullptr);
    ~NoteListView() override;

    // Not owned. Selects the first note when none was selected before; keeps the
    // selected note across model resets. Rows may arrive later (the real model
    // loads asynchronously): then the first note is selected and announced with
    // noteSelected(), and if the selected note goes away for good, the first
    // following one is (the previous one at the end; TC-UI-022), or
    // currentNoteCleared() says the list is empty.
    void setModel(QAbstractItemModel* model) override;

    [[nodiscard]] std::optional<qint64> currentNote() const { return m_currentNote; }
    // Returns false if no row has this id.
    bool selectNote(qint64 noteId);

signals:
    void noteSelected(qint64 noteId);
    void currentNoteCleared();

protected:
    // Qt moves the current row to a neighbour when it is removed; that's not
    // the user picking a note, so selection changes are ignored until the
    // removal is done (checkCurrentNote() settles it afterwards).
    void rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) override;

private:
    void onSelectionChanged();
    void restoreSelection();
    // Queued after row changes: a model that moves a row removes and reinserts
    // it in one go, so the selected note is only gone if it is still missing then.
    void scheduleCheck();
    void checkCurrentNote();
    [[nodiscard]] QModelIndex indexForNote(qint64 noteId) const;

    std::optional<qint64> m_currentNote;
    bool m_restoring = false;
    bool m_checkPending = false;
    bool m_removingRows = false;
    // Where the current note's row was removed, to pick its successor; -1 if
    // the current note wasn't among the removed rows.
    int m_removedAt = -1;
};

} // namespace notes::ui
