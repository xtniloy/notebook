#pragma once

#include "models/Note.h"
#include "models/NoteSummary.h"
#include "storage/DbResult.h"
#include "storage/NoteRepository.h"

#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>

namespace notes::app {
class IClock;
}

namespace notes::storage {
class DbThread;
}

namespace notes::services {

// Notes for the UI (task 2.7, ADR 0003). Each call runs NoteRepository on the
// DB thread and returns at once with a QFuture; continue with
// `.then(context, ...)`, never wait on it from the UI thread.
//
// The service works out a note's title and snippet from its text (NoteText),
// and understands the sidebar's virtual folders (models/SidebarRoles.h):
// All Notes and Recently Deleted.
//
// After a change succeeds, the matching signal is emitted on this object's
// thread, before the returned future finishes. Failed calls, and calls that
// change nothing (pinning a pinned note), emit nothing.
class NoteService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NoteService)

public:
    // `db` must have been migrated (storage::Migrator). Both must outlive this.
    NoteService(storage::DbThread& db, const app::IClock& clock, QObject* parent = nullptr);
    ~NoteService() override;

    // A new note in `folderId`, the folder selected in the sidebar. With All
    // Notes selected it goes into the default "Notes" folder; Recently Deleted
    // can't take new notes.
    QFuture<storage::DbResult<models::Note>> create(qint64 folderId, const QString& bodyHtml,
                                                    const QString& bodyText);
    // A new note in the same folder with the same body, titled "<title> copy"
    // (TC-UI-023); noteCreated(). Not for trashed or locked notes.
    QFuture<storage::DbResult<models::Note>> duplicate(qint64 id);
    [[nodiscard]] QFuture<storage::DbResult<models::Note>> get(qint64 id);
    // Saves the editor's content; the title and snippet follow from the text.
    QFuture<storage::DbResult<models::Note>> updateBody(qint64 id, const QString& bodyHtml,
                                                        const QString& bodyText);
    QFuture<storage::DbStatus> setPinned(qint64 id, bool pinned);
    QFuture<storage::DbStatus> moveToFolder(qint64 id, qint64 folderId);
    // NoteRepository::liveIdsForUuids(), for drops.
    [[nodiscard]] QFuture<storage::DbResult<QList<qint64>>>
    liveIdsForUuids(const QStringList& uuids);
    QFuture<storage::DbStatus> trash(qint64 id);
    QFuture<storage::DbStatus> restore(qint64 id);
    QFuture<storage::DbStatus> purge(qint64 id);
    // Purges notes trashed more than 30 days ago (task 2.10); notePurged() for
    // each. Returns their ids.
    QFuture<storage::DbResult<QList<qint64>>> purgeExpired();

    // The notes `folderId` lists in the sidebar: a folder, All Notes, or
    // Recently Deleted (which ignores `sort`: most recently deleted first).
    [[nodiscard]] QFuture<storage::DbResult<QList<models::NoteSummary>>>
    list(qint64 folderId, storage::NoteSort sort, int limit = -1, int offset = 0);

    // Note counts for the sidebar (NoteRepository::countsByFolder()).
    [[nodiscard]] QFuture<storage::DbResult<QHash<qint64, int>>> counts();

signals:
    void noteCreated(const notes::models::Note& note);
    void noteUpdated(const notes::models::Note& note);
    void notePinnedChanged(qint64 noteId, bool pinned);
    void noteMoved(qint64 noteId, qint64 fromFolderId, qint64 toFolderId);
    void noteTrashed(qint64 noteId);
    void noteRestored(qint64 noteId, qint64 folderId);
    void notePurged(qint64 noteId);

private:
    storage::DbThread& m_db;
    const app::IClock& m_clock;
};

} // namespace notes::services
