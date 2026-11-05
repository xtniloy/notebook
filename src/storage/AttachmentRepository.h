#pragma once

#include "models/Attachment.h"
#include "storage/DbResult.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace notes::app {
class IClock;
}

namespace notes::storage {

class Database;

// Images and files in the `attachments` table (task 3.7), stored inside the
// (encrypted) database, never as loose files. Note bodies refer to them as
// `attachment://<uuid>`. Runs on the DB thread (ADR 0003).
class AttachmentRepository
{
public:
    AttachmentRepository(Database& db, const app::IClock& clock);
    ~AttachmentRepository() = default;
    // Bound to one connection and clock for its whole life.
    AttachmentRepository(const AttachmentRepository&) = delete;
    AttachmentRepository& operator=(const AttachmentRepository&) = delete;
    AttachmentRepository(AttachmentRepository&&) = delete;
    AttachmentRepository& operator=(AttachmentRepository&&) = delete;

    // The note's attachments with their data, oldest first.
    [[nodiscard]] DbResult<QList<models::Attachment>> forNote(qint64 noteId);

    // After a save of the note's body, which refers to `referenced` (uuids,
    // in body order): those not stored yet are added from `available` (the
    // editor's copies; without one, nothing to add), and the note's others,
    // no longer in the body, are deleted (orphan cleanup). An attachment
    // another note owns is never moved or changed. Returns the first of
    // `referenced` the note has now (its thumbnail), or an empty string. Run
    // inside the save's transaction.
    [[nodiscard]] DbResult<QString> keepOnly(qint64 noteId, const QStringList& referenced,
                                             const QList<models::Attachment>& available);

    // Copies every attachment of `fromNoteId` to `toNoteId` under new uuids
    // (duplicating a note); returns old uuid -> new uuid.
    [[nodiscard]] DbResult<QHash<QString, QString>> copyAll(qint64 fromNoteId, qint64 toNoteId);

private:
    DbStatus insert(qint64 noteId, const models::Attachment& attachment);
    // The note's attachments now: uuid -> id.
    [[nodiscard]] DbResult<QHash<QString, qint64>> idsForNote(qint64 noteId);
    // Whether any note has an attachment with `uuid`.
    [[nodiscard]] DbResult<bool> exists(const QString& uuid);

    Database& m_db;
    const app::IClock& m_clock;
};

} // namespace notes::storage
