#pragma once

#include <QByteArray>
#include <QString>

namespace notes::models {

// A row of the `attachments` table (Plan §7): an image or file stored inside the
// (encrypted) database. `id` and `noteId` are local only; `uuid` is referenced
// from note bodies as `attachment://<uuid>` (ADR 0005).
struct Attachment
{
    qint64 id = 0; // 0 until stored
    QString uuid;
    qint64 noteId = 0;
    QString fileName;
    QString mimeType;
    qint64 sizeBytes = 0;
    QString sha256; // lowercase hex of `data`
    QByteArray data;
    QByteArray thumbnail; // empty if none
    qint64 createdAt = 0;

    bool operator==(const Attachment&) const = default;
};

} // namespace notes::models
