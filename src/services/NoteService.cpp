#include "services/NoteService.h"

#include "app/IClock.h"
#include "models/SidebarRoles.h"
#include "services/NoteText.h"
#include "storage/AttachmentRepository.h"
#include "storage/DbThread.h"
#include "storage/FolderRepository.h"
#include "storage/Transaction.h"

namespace notes::services {

using models::Attachment;
using models::Note;
using models::NoteSummary;
using storage::AttachmentRepository;
using storage::Database;
using storage::DbError;
using storage::DbResult;
using storage::DbStatus;
using storage::FolderRepository;
using storage::NoteContent;
using storage::NoteRepository;
using storage::NoteSort;
using storage::Transaction;
using Kind = DbError::Kind;

namespace {

NoteContent contentFor(const QString& body, const QString& bodyText)
{
    return {.title = NoteText::title(bodyText),
            .snippet = NoteText::snippet(bodyText),
            .body = body,
            .bodyText = bodyText,
            .thumbnailUuid = {}};
}

} // namespace

NoteService::NoteService(storage::DbThread& db, const app::IClock& clock, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
{}

NoteService::~NoteService() = default;

QFuture<DbResult<Note>> NoteService::create(qint64 folderId, const QString& body,
                                            const QString& bodyText)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, folderId,
               content = contentFor(body, bodyText)](Database& db) -> DbResult<Note> {
            qint64 target = folderId;
            if (folderId == models::kRecentlyDeletedFolderId) {
                return DbError::make(Kind::InvalidArgument,
                                     QStringLiteral("Recently Deleted can't take new notes"));
            }
            if (folderId == models::kAllNotesFolderId) {
                // "All Notes" isn't a folder: new notes go to the default one.
                DbResult<models::Folder> fallback =
                    FolderRepository(db, *clock).ensureDefaultFolder();
                if (!fallback) {
                    return fallback.error();
                }
                target = fallback.value().id;
            }
            return NoteRepository(db, *clock).create(target, content);
        })
        .then(this, [this](DbResult<Note> result) {
            if (result) {
                emit noteCreated(result.value());
            }
            return result;
        });
}

QFuture<DbResult<Note>> NoteService::duplicate(qint64 id)
{
    const app::IClock* clock = &m_clock;
    //: The title of a duplicated note; %1 is the original's title.
    const QString titleFormat = tr("%1 copy");
    return m_db
        .post([clock, id, titleFormat](Database& db) -> DbResult<Note> {
            NoteRepository notes(db, *clock);
            AttachmentRepository attachments(db, *clock);
            DbResult<Note> original = notes.get(id);
            if (!original) {
                return original.error();
            }
            const Note& note = original.value();
            if (note.isTrashed() || note.deletedAt || note.locked) {
                return DbError::make(Kind::InvalidArgument,
                                     QStringLiteral("Only live, unlocked notes can be duplicated"));
            }
            DbResult<Transaction> tx = Transaction::begin(db);
            if (!tx) {
                return tx.error();
            }
            // Explicit title: the body (and so the derived title) is identical.
            NoteContent copy{.title =
                                 note.title.isEmpty() ? QString() : titleFormat.arg(note.title),
                             .snippet = note.snippet,
                             .body = note.body,
                             .bodyText = note.bodyText,
                             .thumbnailUuid = {}};
            DbResult<Note> created = notes.create(note.folderId, copy);
            if (!created) {
                return created;
            }
            // Its own copies of the images: an attachment belongs to one note,
            // so deleting an image from one copy keeps it in the other.
            DbResult<QHash<QString, QString>> renamed =
                attachments.copyAll(note.id, created.value().id);
            if (!renamed) {
                return renamed.error();
            }
            if (!renamed.value().isEmpty()) {
                for (auto it = renamed.value().cbegin(); it != renamed.value().cend(); ++it) {
                    copy.body.replace(QStringLiteral("attachment://") + it.key(),
                                      QStringLiteral("attachment://") + it.value());
                }
                copy.thumbnailUuid = renamed.value().value(note.thumbnailUuid);
                created = notes.updateBody(created.value().id, copy);
                if (!created) {
                    return created;
                }
            }
            if (const DbStatus status = tx.value().commit(); !status) {
                return status.error();
            }
            return created;
        })
        .then(this, [this](DbResult<Note> result) {
            if (result) {
                emit noteCreated(result.value());
            }
            return result;
        });
}

QFuture<DbResult<Note>> NoteService::get(qint64 id)
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock, id](Database& db) { return NoteRepository(db, *clock).get(id); });
}

QFuture<DbResult<OpenedNote>> NoteService::open(qint64 id)
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock, id](Database& db) -> DbResult<OpenedNote> {
        DbResult<Note> note = NoteRepository(db, *clock).get(id);
        if (!note) {
            return note.error();
        }
        OpenedNote opened{.note = note.value(), .attachments = {}};
        if (opened.note.locked) {
            return opened; // its content stays sealed (Phase 6)
        }
        DbResult<QList<Attachment>> attachments = AttachmentRepository(db, *clock).forNote(id);
        if (!attachments) {
            return attachments.error();
        }
        opened.attachments = attachments.value();
        return opened;
    });
}

QFuture<DbResult<Note>> NoteService::updateBody(qint64 id, const QString& body,
                                                const QString& bodyText,
                                                const QList<Attachment>& attachments)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id, content = contentFor(body, bodyText),
               attachments](Database& db) mutable -> DbResult<Note> {
            DbResult<Transaction> tx = Transaction::begin(db);
            if (!tx) {
                return tx.error();
            }
            // Attachments first, for the thumbnail; a body that can't be saved
            // (locked, trashed) rolls them back too.
            DbResult<QString> thumbnail =
                AttachmentRepository(db, *clock)
                    .keepOnly(id, NoteText::attachmentUuids(content.body), attachments);
            if (!thumbnail) {
                return thumbnail.error();
            }
            content.thumbnailUuid = thumbnail.value();
            DbResult<Note> saved = NoteRepository(db, *clock).updateBody(id, content);
            if (!saved) {
                return saved;
            }
            if (const DbStatus status = tx.value().commit(); !status) {
                return status.error();
            }
            return saved;
        })
        .then(this, [this](DbResult<Note> result) {
            if (result) {
                emit noteUpdated(result.value());
            }
            return result;
        });
}

QFuture<DbStatus> NoteService::setPinned(qint64 id, bool pinned)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id, pinned](Database& db) -> DbResult<bool> {
            NoteRepository notes(db, *clock);
            DbResult<Note> before = notes.get(id);
            if (!before) {
                return before.error();
            }
            if (const DbStatus status = notes.setPinned(id, pinned); !status) {
                return status.error();
            }
            return before.value().pinned != pinned; // changed?
        })
        .then(this, [this, id, pinned](DbResult<bool> changed) -> DbStatus {
            if (!changed) {
                return changed.error();
            }
            if (changed.value()) {
                emit notePinnedChanged(id, pinned);
            }
            return {};
        });
}

QFuture<DbStatus> NoteService::moveToFolder(qint64 id, qint64 folderId)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id, folderId](Database& db) -> DbResult<qint64> {
            NoteRepository notes(db, *clock);
            DbResult<Note> before = notes.get(id);
            if (!before) {
                return before.error();
            }
            if (const DbStatus status = notes.moveToFolder(id, folderId); !status) {
                return status.error();
            }
            return before.value().folderId;
        })
        .then(this, [this, id, folderId](DbResult<qint64> from) -> DbStatus {
            if (!from) {
                return from.error();
            }
            if (from.value() != folderId) {
                emit noteMoved(id, from.value(), folderId);
            }
            return {};
        });
}

QFuture<DbResult<QList<qint64>>> NoteService::liveIdsForUuids(const QStringList& uuids)
{
    const app::IClock* clock = &m_clock;
    return m_db.post(
        [clock, uuids](Database& db) { return NoteRepository(db, *clock).liveIdsForUuids(uuids); });
}

QFuture<DbStatus> NoteService::trash(qint64 id)
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock, id](Database& db) { return NoteRepository(db, *clock).trash(id); })
        .then(this, [this, id](DbStatus status) {
            if (status) {
                emit noteTrashed(id);
            }
            return status;
        });
}

QFuture<DbStatus> NoteService::restore(qint64 id)
{
    const app::IClock* clock = &m_clock;
    return m_db
        .post([clock, id](Database& db) -> DbResult<qint64> {
            NoteRepository notes(db, *clock);
            if (const DbStatus status = notes.restore(id); !status) {
                return status.error();
            }
            // Its own folder, or "Notes" if that is gone.
            DbResult<Note> after = notes.get(id);
            if (!after) {
                return after.error();
            }
            return after.value().folderId;
        })
        .then(this, [this, id](DbResult<qint64> folderId) -> DbStatus {
            if (!folderId) {
                return folderId.error();
            }
            emit noteRestored(id, folderId.value());
            return {};
        });
}

QFuture<DbStatus> NoteService::purge(qint64 id)
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock, id](Database& db) { return NoteRepository(db, *clock).purge(id); })
        .then(this, [this, id](DbStatus status) {
            if (status) {
                emit notePurged(id);
            }
            return status;
        });
}

QFuture<DbResult<QList<qint64>>> NoteService::purgeExpired()
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock](Database& db) { return NoteRepository(db, *clock).purgeExpired(); })
        .then(this, [this](DbResult<QList<qint64>> purged) {
            if (purged) {
                for (const qint64 id : purged.value()) {
                    emit notePurged(id);
                }
            }
            return purged;
        });
}

QFuture<DbResult<QList<NoteSummary>>> NoteService::list(qint64 folderId, NoteSort sort, int limit,
                                                        int offset)
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock, folderId, sort, limit, offset](Database& db) {
        NoteRepository notes(db, *clock);
        if (folderId == models::kAllNotesFolderId) {
            return notes.listAll(sort, limit, offset);
        }
        if (folderId == models::kRecentlyDeletedFolderId) {
            return notes.listTrashed(limit, offset);
        }
        return notes.listForFolder(folderId, sort, limit, offset);
    });
}

QFuture<DbResult<QHash<qint64, int>>> NoteService::counts()
{
    const app::IClock* clock = &m_clock;
    return m_db.post([clock](Database& db) { return NoteRepository(db, *clock).countsByFolder(); });
}

} // namespace notes::services
