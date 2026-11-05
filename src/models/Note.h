#pragma once

#include "models/SyncState.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace notes::models {

// A row of the `notes` table (Plan §7). Plain value type: copied between the DB
// thread and the UI (ADR 0003). Times are UTC ms (ADR 0004); `id` and `folderId`
// are local only, `uuid` is the identity across devices (ADR 0005).
//
// The locked-note columns (`locked_blob`, `locked_nonce`, `wrapped_key`) are
// added in Phase 6 together with SecureBuffer, so key material never sits in a
// plain QByteArray here.
struct Note
{
    qint64 id = 0; // 0 until stored
    QString uuid;
    qint64 folderId = 0;
    QString title;
    QString snippet; // list preview; empty for locked notes
    // The note format's JSON (editor/NoteFormat, ADR 0006; the core only
    // stores and passes it on). Empty for locked notes (TC-STO-014), and for
    // notes from before the format, which show `bodyText`.
    QString body;
    QString bodyText; // plain text for search; empty for locked notes
    // The note list's thumbnail: its first image's (task 3.7), with that
    // attachment's uuid. Empty when the note has no image.
    QString thumbnailUuid;
    QByteArray thumbnail;
    bool pinned = false;
    bool locked = false;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    std::optional<qint64> trashedAt; // in Recently Deleted since
    std::optional<qint64> deletedAt; // sync tombstone
    int version = 1;
    SyncState syncState = SyncState::Dirty;

    [[nodiscard]] bool isTrashed() const { return trashedAt.has_value(); }

    bool operator==(const Note&) const = default;
};

} // namespace notes::models
