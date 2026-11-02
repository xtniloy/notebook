#pragma once

#include "models/NoteSummary.h"
#include "models/SidebarRoles.h"
#include "storage/NoteRepository.h"

#include <QAbstractListModel>
#include <QCollator>
#include <QList>
#include <QString>

namespace notes::app {
class IClock;
}

namespace notes::services {
class NoteService;
}

namespace notes::models {

struct Note;

// The note list for one sidebar entry (a folder, All Notes or Recently
// Deleted), from NoteService (task 2.8). Provides the roles in NoteListRoles.h,
// so NoteListView shows it like the Phase 1 fake model.
//
// Loads lazily, kPageSize notes at a time (canFetchMore()/fetchMore(); the view
// asks as the user scrolls), and puts section header rows ("Pinned", "Today",
// ...) between them. Keeps itself up to date from NoteService's signals with
// row-level changes, not resets: an edited note that stays in place only gets
// dataChanged() for its row (TC-STO-024).
class NoteListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NoteListModel)

public:
    static constexpr int kPageSize = 200;

    // Both must outlive the model. Empty until setFolder(): the window waits for
    // the sidebar's first choice, so the list doesn't load twice at startup.
    NoteListModel(services::NoteService& notes, const app::IClock& clock,
                  QObject* parent = nullptr);
    ~NoteListModel() override;

    // Shows `folderId` (models/SidebarRoles.h ids included) sorted by `sort`,
    // from the first page. Resets the model.
    void setFolder(qint64 folderId, storage::NoteSort sort = storage::NoteSort::DateEdited);
    [[nodiscard]] qint64 folderId() const { return m_folderId; }
    [[nodiscard]] storage::NoteSort noteSort() const { return m_sort; }

    // Notes loaded so far (header rows excluded).
    [[nodiscard]] int loadedNoteCount() const { return static_cast<int>(m_notes.size()); }
    [[nodiscard]] bool isLoading() const { return m_loading; }

    // Recomputes "Today", "Yesterday", ... and times from the clock, e.g. after
    // midnight. Sections are otherwise fixed while the model is shown.
    void refreshDates();

    // "30 days remaining" for a note trashed at `trashedAtMs`: Recently
    // Deleted's rows show this instead of a time (task 2.10).
    [[nodiscard]] static QString daysRemainingText(qint64 trashedAtMs, qint64 nowMs);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    // Notes can be dragged onto sidebar folders (task 2.12), as kNoteUuidsMime.
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

signals:
    void loadingChanged(bool loading);

private:
    struct Row
    {
        bool isHeader = false;
        QString section; // header title, or the note's section
        QString timeText;
        NoteSummary note; // notes only

        [[nodiscard]] bool sameKey(const Row& other) const;
        bool operator==(const Row&) const = default;
    };

    void fetchPage();
    // Makes `notes` the loaded list (display order) and updates the rows with
    // the smallest remove / insert / dataChanged steps.
    void applyNotes(QList<NoteSummary> notes);
    [[nodiscard]] QList<Row> buildRows(const QList<NoteSummary>& notes) const;

    [[nodiscard]] bool belongsHere(const NoteSummary& note) const;
    [[nodiscard]] bool before(const NoteSummary& a, const NoteSummary& b) const;
    [[nodiscard]] qsizetype indexOf(qint64 noteId) const;
    // Puts `note` where it belongs in the loaded list, if it belongs to this view
    // and to the part loaded so far; removes it otherwise.
    void upsert(const NoteSummary& note);
    void remove(qint64 noteId);
    // Reloads one note from the service, then upserts (or removes) it.
    void refreshNote(qint64 noteId);

    [[nodiscard]] QString sectionOf(const NoteSummary& note) const;
    [[nodiscard]] qint64 timestampOf(const NoteSummary& note) const;
    // The row's time; in Recently Deleted, the days until it is purged.
    [[nodiscard]] QString timeTextOf(const NoteSummary& note) const;

    services::NoteService& m_service;
    const app::IClock& m_clock;
    QCollator m_collator; // the LOCALE collation's rules (Database), for Title order

    qint64 m_folderId = 0; // 0: none yet
    storage::NoteSort m_sort = storage::NoteSort::DateEdited;
    qint64 m_now = 0; // for sections and times; see refreshDates()

    QList<NoteSummary> m_notes; // loaded, in display order
    QList<Row> m_rows;
    int m_offset = 0; // rows of the query consumed so far (for the next page)
    bool m_exhausted = false;
    bool m_loading = false;
    quint64 m_generation = 0; // bumped by setFolder(); stale results are dropped
};

} // namespace notes::models
