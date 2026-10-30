#include "storage/NoteRepository.h"

#include "app/IClock.h"
#include "models/Uuid.h"
#include "storage/Database.h"
#include "storage/FolderRepository.h"
#include "storage/Transaction.h"

namespace notes::storage {

using models::Note;
using models::NoteSummary;
using Kind = DbError::Kind;

namespace {

constexpr QLatin1StringView
    kNoteColumns("id, uuid, folder_id, title, snippet, body_html, body_text, is_pinned, is_locked,"
                 " created_at, updated_at, trashed_at, deleted_at, version, sync_state");

// What lists read: no body_html / body_text (TC-STO-014).
constexpr QLatin1StringView kSummaryColumns(
    "id, uuid, folder_id, title, snippet, is_pinned, is_locked, created_at, updated_at,"
    " trashed_at");

// What every change does to a row.
constexpr QLatin1StringView kBump("version = version + 1, sync_state = 0");

constexpr QLatin1StringView kLive("trashed_at IS NULL AND deleted_at IS NULL");

Note readNote(const Statement& row)
{
    Note note;
    note.id = row.column<qint64>(0);
    note.uuid = row.column<QString>(1);
    note.folderId = row.column<qint64>(2);
    note.title = row.column<QString>(3);
    note.snippet = row.column<QString>(4);
    note.bodyHtml = row.column<QString>(5);
    note.bodyText = row.column<QString>(6);
    note.pinned = row.column<bool>(7);
    note.locked = row.column<bool>(8);
    note.createdAt = row.column<qint64>(9);
    note.updatedAt = row.column<qint64>(10);
    note.trashedAt = row.column<std::optional<qint64>>(11);
    note.deletedAt = row.column<std::optional<qint64>>(12);
    note.version = row.column<int>(13);
    note.syncState = row.column<models::SyncState>(14);
    return note;
}

NoteSummary readSummary(const Statement& row)
{
    NoteSummary note;
    note.id = row.column<qint64>(0);
    note.uuid = row.column<QString>(1);
    note.folderId = row.column<qint64>(2);
    note.title = row.column<QString>(3);
    note.snippet = row.column<QString>(4);
    note.pinned = row.column<bool>(5);
    note.locked = row.column<bool>(6);
    note.createdAt = row.column<qint64>(7);
    note.updatedAt = row.column<qint64>(8);
    note.trashedAt = row.column<std::optional<qint64>>(9);
    return note;
}

// Pinned notes first in every mode (TC-STO-015); id breaks ties so paging is stable.
QLatin1StringView orderFor(NoteSort sort)
{
    switch (sort) {
    case NoteSort::DateEdited:
        return QLatin1StringView("is_pinned DESC, updated_at DESC, id DESC");
    case NoteSort::DateCreated:
        return QLatin1StringView("is_pinned DESC, created_at DESC, id DESC");
    case NoteSort::Title:
        return QLatin1StringView("is_pinned DESC, title COLLATE LOCALE, id");
    }
    return QLatin1StringView("is_pinned DESC, updated_at DESC, id DESC");
}

DbError notFound(qint64 id)
{
    return DbError::make(Kind::NotFound, QStringLiteral("No note with id %1").arg(id));
}

} // namespace

NoteRepository::NoteRepository(Database& db, const app::IClock& clock)
    : m_db(db)
    , m_clock(clock)
{}

DbResult<Note> NoteRepository::get(qint64 id)
{
    DbResult<ScopedStatement> select =
        m_db.prepare(QStringLiteral("SELECT %1 FROM notes WHERE id = ?").arg(kNoteColumns));
    if (!select) {
        return select.error();
    }
    DbResult<bool> row = select.value()->bind(1, id).step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return notFound(id);
    }
    return readNote(*select.value());
}

DbResult<Note> NoteRepository::getIn(qint64 id, State state)
{
    DbResult<Note> note = get(id);
    if (!note) {
        return note;
    }
    if (note.value().deletedAt) {
        return notFound(id); // purged
    }
    const bool trashed = note.value().isTrashed();
    if (state == State::Live && trashed) {
        return DbError::make(Kind::InvalidArgument,
                             QStringLiteral("Note %1 is in Recently Deleted").arg(id));
    }
    if (state == State::Trashed && !trashed) {
        return DbError::make(Kind::InvalidArgument,
                             QStringLiteral("Note %1 is not in Recently Deleted").arg(id));
    }
    return note;
}

DbStatus NoteRepository::checkLiveFolder(qint64 folderId)
{
    DbResult<ScopedStatement> select =
        m_db.prepare(QStringLiteral("SELECT 1 FROM folders WHERE id = ? AND deleted_at IS NULL"));
    if (!select) {
        return select.error();
    }
    DbResult<bool> row = select.value()->bind(1, folderId).step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return DbError::make(Kind::NotFound, QStringLiteral("No folder with id %1").arg(folderId));
    }
    return {};
}

DbResult<Note> NoteRepository::create(qint64 folderId, const NoteContent& content)
{
    if (const DbStatus folder = checkLiveFolder(folderId); !folder) {
        return folder.error();
    }
    const qint64 now = m_clock.nowMs();
    DbResult<ScopedStatement> insert = m_db.prepare(QStringLiteral(
        "INSERT INTO notes (uuid, folder_id, title, snippet, body_html, body_text,"
        " created_at, updated_at) VALUES (:uuid, :folder, :title, :snippet, :html, :text,"
        " :now, :now)"));
    if (!insert) {
        return insert.error();
    }
    if (const DbStatus status = insert.value()
                                    ->bind(":uuid", models::newUuid())
                                    .bind(":folder", folderId)
                                    .bind(":title", content.title)
                                    .bind(":snippet", content.snippet)
                                    .bind(":html", content.bodyHtml)
                                    .bind(":text", content.bodyText)
                                    .bind(":now", now)
                                    .run();
        !status) {
        return status.error();
    }
    return get(m_db.lastInsertRowId());
}

DbResult<Note> NoteRepository::updateBody(qint64 id, const NoteContent& content)
{
    DbResult<Note> note = getIn(id, State::Live);
    if (!note) {
        return note;
    }
    if (note.value().locked) {
        // Locked notes are re-encrypted as a whole (Phase 6), never edited in plain.
        return DbError::make(Kind::Protected, QStringLiteral("Note %1 is locked").arg(id));
    }
    DbResult<ScopedStatement> update = m_db.prepare(
        QStringLiteral("UPDATE notes SET title = :title, snippet = :snippet, body_html = :html,"
                       " body_text = :text, updated_at = :now, %1 WHERE id = :id")
            .arg(kBump));
    if (!update) {
        return update.error();
    }
    if (const DbStatus status = update.value()
                                    ->bind(":title", content.title)
                                    .bind(":snippet", content.snippet)
                                    .bind(":html", content.bodyHtml)
                                    .bind(":text", content.bodyText)
                                    .bind(":now", m_clock.nowMs())
                                    .bind(":id", id)
                                    .run();
        !status) {
        return status.error();
    }
    return get(id);
}

DbStatus NoteRepository::setPinned(qint64 id, bool pinned)
{
    DbResult<Note> note = getIn(id, State::Live);
    if (!note) {
        return note.error();
    }
    if (note.value().pinned == pinned) {
        return {}; // not a change
    }
    DbResult<ScopedStatement> update =
        m_db.prepare(QStringLiteral("UPDATE notes SET is_pinned = ?, %1 WHERE id = ?").arg(kBump));
    if (!update) {
        return update.error();
    }
    return update.value()->bind(1, pinned).bind(2, id).run();
}

DbStatus NoteRepository::moveToFolder(qint64 id, qint64 folderId)
{
    DbResult<Note> note = getIn(id, State::Live);
    if (!note) {
        return note.error();
    }
    if (const DbStatus folder = checkLiveFolder(folderId); !folder) {
        return folder;
    }
    if (note.value().folderId == folderId) {
        return {}; // not a change
    }
    DbResult<ScopedStatement> update =
        m_db.prepare(QStringLiteral("UPDATE notes SET folder_id = ?, %1 WHERE id = ?").arg(kBump));
    if (!update) {
        return update.error();
    }
    return update.value()->bind(1, folderId).bind(2, id).run();
}

DbStatus NoteRepository::trash(qint64 id)
{
    if (const DbResult<Note> note = getIn(id, State::Live); !note) {
        return note.error();
    }
    DbResult<ScopedStatement> update =
        m_db.prepare(QStringLiteral("UPDATE notes SET trashed_at = ?, %1 WHERE id = ?").arg(kBump));
    if (!update) {
        return update.error();
    }
    return update.value()->bind(1, m_clock.nowMs()).bind(2, id).run();
}

DbStatus NoteRepository::restore(qint64 id)
{
    DbResult<Note> note = getIn(id, State::Trashed);
    if (!note) {
        return note.error();
    }

    // Its own folder if that still exists, otherwise the default "Notes" folder.
    qint64 folderId = note.value().folderId;
    if (const DbStatus folder = checkLiveFolder(folderId); !folder) {
        if (folder.error().kind != Kind::NotFound) {
            return folder.error();
        }
        DbResult<ScopedStatement> fallback = m_db.prepare(
            QStringLiteral("SELECT id FROM folders WHERE uuid = ? AND deleted_at IS NULL"));
        if (!fallback) {
            return fallback.error();
        }
        DbResult<bool> row =
            fallback.value()
                ->bind(1, QString::fromLatin1(FolderRepository::kDefaultFolderUuid))
                .step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            return DbError::make(Kind::NotFound, QStringLiteral("The default folder is missing"));
        }
        folderId = fallback.value()->column<qint64>(0);
    }

    DbResult<ScopedStatement> update = m_db.prepare(
        QStringLiteral("UPDATE notes SET trashed_at = NULL, folder_id = ?, %1 WHERE id = ?")
            .arg(kBump));
    if (!update) {
        return update.error();
    }
    return update.value()->bind(1, folderId).bind(2, id).run();
}

DbStatus NoteRepository::purge(qint64 id)
{
    if (const DbResult<Note> note = getIn(id, State::NotDeleted); !note) {
        return note.error();
    }
    DbResult<Transaction> tx = Transaction::begin(m_db);
    if (!tx) {
        return tx.error();
    }
    for (const QLatin1StringView sql :
         {QLatin1StringView("DELETE FROM attachments WHERE note_id = ?"),
          QLatin1StringView("DELETE FROM note_tags WHERE note_id = ?")}) {
        DbResult<ScopedStatement> remove = m_db.prepare(QString(sql));
        if (!remove) {
            return remove.error();
        }
        if (const DbStatus status = remove.value()->bind(1, id).run(); !status) {
            return status;
        }
    }
    {
        // A tombstone: identity and timestamps stay for sync, the content goes.
        DbResult<ScopedStatement> wipe = m_db.prepare(
            QStringLiteral(
                "UPDATE notes SET title = '', snippet = NULL, body_html = NULL, body_text = NULL,"
                " locked_blob = NULL, locked_nonce = NULL, wrapped_key = NULL, is_pinned = 0,"
                " deleted_at = ?, %1 WHERE id = ?")
                .arg(kBump));
        if (!wipe) {
            return wipe.error();
        }
        if (const DbStatus status = wipe.value()->bind(1, m_clock.nowMs()).bind(2, id).run();
            !status) {
            return status;
        }
    }
    return tx.value().commit();
}

DbResult<QList<NoteSummary>> NoteRepository::list(QLatin1StringView where,
                                                  QLatin1StringView orderBy,
                                                  std::optional<qint64> folderId, int limit,
                                                  int offset)
{
    DbResult<ScopedStatement> select = m_db.prepare(
        QStringLiteral("SELECT %1 FROM notes WHERE %2 ORDER BY %3 LIMIT :limit OFFSET :offset")
            .arg(kSummaryColumns, where, orderBy));
    if (!select) {
        return select.error();
    }
    Statement& statement = *select.value();
    statement.bind(":limit", limit).bind(":offset", offset);
    if (folderId) {
        statement.bind(":folder", *folderId);
    }

    QList<NoteSummary> notes;
    while (true) {
        DbResult<bool> row = statement.step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            return notes;
        }
        notes.append(readSummary(statement));
    }
}

DbResult<QList<NoteSummary>> NoteRepository::listForFolder(qint64 folderId, NoteSort sort,
                                                           int limit, int offset)
{
    return list(
        QLatin1StringView("folder_id = :folder AND trashed_at IS NULL AND deleted_at IS NULL"),
        orderFor(sort), folderId, limit, offset);
}

DbResult<QList<NoteSummary>> NoteRepository::listAll(NoteSort sort, int limit, int offset)
{
    return list(kLive, orderFor(sort), std::nullopt, limit, offset);
}

DbResult<QList<NoteSummary>> NoteRepository::listTrashed(int limit, int offset)
{
    return list(QLatin1StringView("trashed_at IS NOT NULL AND deleted_at IS NULL"),
                QLatin1StringView("trashed_at DESC, id DESC"), std::nullopt, limit, offset);
}

} // namespace notes::storage
