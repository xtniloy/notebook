#include "storage/AttachmentRepository.h"

#include "app/IClock.h"
#include "models/Uuid.h"
#include "storage/Database.h"

#include <QSet>

namespace notes::storage {

using models::Attachment;

namespace {

constexpr QLatin1StringView kColumns("id, uuid, note_id, file_name, mime_type, size_bytes, sha256,"
                                     " data, thumbnail, created_at");

Attachment readAttachment(const Statement& row)
{
    Attachment attachment;
    attachment.id = row.column<qint64>(0);
    attachment.uuid = row.column<QString>(1);
    attachment.noteId = row.column<qint64>(2);
    attachment.fileName = row.column<QString>(3);
    attachment.mimeType = row.column<QString>(4);
    attachment.sizeBytes = row.column<qint64>(5);
    attachment.sha256 = row.column<QString>(6);
    attachment.data = row.column<QByteArray>(7);
    attachment.thumbnail = row.column<QByteArray>(8);
    attachment.createdAt = row.column<qint64>(9);
    return attachment;
}

} // namespace

AttachmentRepository::AttachmentRepository(Database& db, const app::IClock& clock)
    : m_db(db)
    , m_clock(clock)
{}

DbResult<QList<Attachment>> AttachmentRepository::forNote(qint64 noteId)
{
    DbResult<ScopedStatement> select = m_db.prepare(
        QStringLiteral("SELECT %1 FROM attachments WHERE note_id = ? ORDER BY id").arg(kColumns));
    if (!select) {
        return select.error();
    }
    select.value()->bind(1, noteId);
    QList<Attachment> attachments;
    while (true) {
        DbResult<bool> row = select.value()->step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            return attachments;
        }
        attachments.append(readAttachment(*select.value()));
    }
}

DbResult<QString> AttachmentRepository::keepOnly(qint64 noteId, const QStringList& referenced,
                                                 const QList<Attachment>& available)
{
    DbResult<QHash<QString, qint64>> stored = idsForNote(noteId);
    if (!stored) {
        return stored.error();
    }
    QHash<QString, qint64> own = stored.value();

    QHash<QString, const Attachment*> copies;
    for (const Attachment& attachment : available) {
        copies.insert(attachment.uuid, &attachment);
    }
    QString first;
    for (const QString& uuid : referenced) {
        const Attachment* copy = copies.value(uuid);
        // Nothing to store without data: the body shows it as missing.
        if (!own.contains(uuid) && copy != nullptr && !copy->data.isEmpty()) {
            DbResult<bool> taken = exists(uuid);
            if (!taken) {
                return taken.error();
            }
            if (!taken.value()) { // else another note's
                if (const DbStatus status = insert(noteId, *copy); !status) {
                    return status.error();
                }
                own.insert(uuid, m_db.lastInsertRowId());
            }
        }
        if (first.isEmpty() && own.contains(uuid)) {
            first = uuid;
        }
    }

    const QSet<QString> keep(referenced.cbegin(), referenced.cend());
    DbResult<ScopedStatement> remove =
        m_db.prepare(QStringLiteral("DELETE FROM attachments WHERE id = ?"));
    if (!remove) {
        return remove.error();
    }
    for (auto it = own.cbegin(); it != own.cend(); ++it) {
        if (keep.contains(it.key())) {
            continue;
        }
        if (const DbStatus status = remove.value()->bind(1, it.value()).run(); !status) {
            return status.error();
        }
    }
    return first;
}

DbResult<QHash<QString, qint64>> AttachmentRepository::idsForNote(qint64 noteId)
{
    DbResult<ScopedStatement> select =
        m_db.prepare(QStringLiteral("SELECT id, uuid FROM attachments WHERE note_id = ?"));
    if (!select) {
        return select.error();
    }
    select.value()->bind(1, noteId);
    QHash<QString, qint64> ids;
    while (true) {
        DbResult<bool> row = select.value()->step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            return ids;
        }
        ids.insert(select.value()->column<QString>(1), select.value()->column<qint64>(0));
    }
}

DbResult<bool> AttachmentRepository::exists(const QString& uuid)
{
    DbResult<ScopedStatement> select =
        m_db.prepare(QStringLiteral("SELECT 1 FROM attachments WHERE uuid = ?"));
    if (!select) {
        return select.error();
    }
    return select.value()->bind(1, uuid).step();
}

DbResult<QHash<QString, QString>> AttachmentRepository::copyAll(qint64 fromNoteId, qint64 toNoteId)
{
    DbResult<QList<Attachment>> attachments = forNote(fromNoteId);
    if (!attachments) {
        return attachments.error();
    }
    QHash<QString, QString> renamed;
    for (Attachment copy : attachments.value()) {
        const QString newUuid = models::newUuid();
        renamed.insert(copy.uuid, newUuid);
        copy.uuid = newUuid;
        copy.createdAt = 0; // now
        if (const DbStatus status = insert(toNoteId, copy); !status) {
            return status.error();
        }
    }
    return renamed;
}

DbStatus AttachmentRepository::insert(qint64 noteId, const Attachment& attachment)
{
    DbResult<ScopedStatement> insert = m_db.prepare(QStringLiteral(
        "INSERT INTO attachments (uuid, note_id, file_name, mime_type, size_bytes, sha256, data,"
        " thumbnail, created_at) VALUES (:uuid, :note, :name, :mime, :size, :sha, :data, :thumb,"
        " :created)"));
    if (!insert) {
        return insert.error();
    }
    return insert.value()
        ->bind(":uuid", attachment.uuid)
        .bind(":note", noteId)
        .bind(":name", attachment.fileName)
        .bind(":mime", attachment.mimeType)
        .bind(":size", attachment.sizeBytes)
        .bind(":sha", attachment.sha256)
        .bind(":data", attachment.data)
        .bind(":thumb",
              attachment.thumbnail.isEmpty() ? std::optional<QByteArray>() : attachment.thumbnail)
        .bind(":created", attachment.createdAt > 0 ? attachment.createdAt : m_clock.nowMs())
        .run();
}

} // namespace notes::storage
