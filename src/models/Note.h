#pragma once

#include "models/SyncState.h"

#include <QString>

#include <optional>

namespace notes::models {

// notes.format_version: how `Note::body` is encoded (ADR 0006). The codec is
// editor/NoteFormat, in the UI library; the core only stores and passes on.
inline constexpr int kBodyFormatLegacyHtml = 1; // Qt HTML from the Phase 2 plain editor
inline constexpr int kBodyFormatJson = 2;       // the note format
inline constexpr int kBodyFormatCurrent = kBodyFormatJson;

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
    QString body;    // in `formatVersion`'s encoding; empty for locked notes (TC-STO-014)
    int formatVersion = kBodyFormatCurrent;
    QString bodyText; // plain text for search; empty for locked notes
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
