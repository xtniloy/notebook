#pragma once

#include "models/Note.h"
#include "models/NoteSummary.h"
#include "storage/DbResult.h"

#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

namespace notes::app {
class IClock;
}

namespace notes::storage {

class Database;

// What a note's editor produces. The note service (task 2.7) derives the title
// and snippet from the text; the repository stores them as given.
struct NoteContent
{
    QString title;
    QString snippet;
    QString bodyHtml;
    QString bodyText;
};

enum class NoteSort : std::uint8_t {
    DateEdited,  // newest edit first
    DateCreated, // newest first
    Title,       // A to Z in the user's locale, ignoring case
};

// Notes in the `notes` table (task 2.6). Runs on the DB thread (ADR 0003).
//
// Every change bumps `version` and marks the note dirty for sync. Only content
// changes (create, updateBody) set `updated_at`, which is the "date edited"
// the note list sorts and groups by: pinning, moving or trashing a note doesn't
// make it look recently edited.
//
// A note is *live* until trashed (Recently Deleted) and *deleted* once purged;
// purged notes stay as content-free tombstones for sync (Plan §9.5).
class NoteRepository
{
public:
    NoteRepository(Database& db, const app::IClock& clock);
    ~NoteRepository() = default;
    // Bound to one connection and clock for its whole life.
    NoteRepository(const NoteRepository&) = delete;
    NoteRepository& operator=(const NoteRepository&) = delete;
    NoteRepository(NoteRepository&&) = delete;
    NoteRepository& operator=(NoteRepository&&) = delete;

    // A new, unpinned note in the live folder `folderId`.
    [[nodiscard]] DbResult<models::Note> create(qint64 folderId, const NoteContent& content);
    // Any note, including trashed ones and tombstones.
    [[nodiscard]] DbResult<models::Note> get(qint64 id);

    [[nodiscard]] DbResult<models::Note> updateBody(qint64 id, const NoteContent& content);
    DbStatus setPinned(qint64 id, bool pinned);
    DbStatus moveToFolder(qint64 id, qint64 folderId);

    // To Recently Deleted. The note keeps its folder_id, for restore().
    DbStatus trash(qint64 id);
    // Back from Recently Deleted into its folder, or into the default "Notes"
    // folder if that one has been deleted since.
    DbStatus restore(qint64 id);
    // Deletes the note for good: its attachments go, and the row becomes a
    // tombstone without content, so sync can pass the deletion on.
    DbStatus purge(qint64 id);

    // Pinned notes first, then by `sort`. Never loads bodies (TC-STO-014).
    // `limit` < 0 means no limit; `offset` skips rows (paging, task 2.8).
    [[nodiscard]] DbResult<QList<models::NoteSummary>>
    listForFolder(qint64 folderId, NoteSort sort, int limit = -1, int offset = 0);
    // Every live note in every folder ("All Notes").
    [[nodiscard]] DbResult<QList<models::NoteSummary>> listAll(NoteSort sort, int limit = -1,
                                                               int offset = 0);
    // Recently Deleted, most recently trashed first.
    [[nodiscard]] DbResult<QList<models::NoteSummary>> listTrashed(int limit = -1, int offset = 0);

private:
    enum class State : std::uint8_t {
        Live,
        Trashed,
        NotDeleted, // live or trashed
    };

    // The note, if it exists and is in `state`.
    [[nodiscard]] DbResult<models::Note> getIn(qint64 id, State state);
    [[nodiscard]] DbStatus checkLiveFolder(qint64 folderId);
    [[nodiscard]] DbResult<QList<models::NoteSummary>> list(QLatin1StringView where,
                                                            QLatin1StringView orderBy,
                                                            std::optional<qint64> folderId,
                                                            int limit, int offset);

    Database& m_db;
    const app::IClock& m_clock;
};

} // namespace notes::storage
