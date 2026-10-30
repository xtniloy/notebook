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
    explicit NoteListView(QWidget* parent = nullptr);
    ~NoteListView() override;

    // Not owned. Selects the first note when none was selected before; keeps the
    // selected note across model resets.
    void setModel(QAbstractItemModel* model) override;

    [[nodiscard]] std::optional<qint64> currentNote() const { return m_currentNote; }
    // Returns false if no row has this id.
    bool selectNote(qint64 noteId);

signals:
    void noteSelected(qint64 noteId);

private:
    void onSelectionChanged();
    void restoreSelection();
    [[nodiscard]] QModelIndex indexForNote(qint64 noteId) const;

    std::optional<qint64> m_currentNote;
    bool m_restoring = false;
};

} // namespace notes::ui
