#pragma once

#include <QByteArray>
#include <QLatin1StringView>
#include <QStringList>

namespace notes::models {

// Drag & drop payloads (task 2.12): UUIDs, one per line, UTF-8. UUIDs rather
// than row ids so a drop never acts on a different note or folder that took
// over an id.
inline constexpr QLatin1StringView kNoteUuidsMime("application/x-notesapp-note-uuids");
inline constexpr QLatin1StringView kFolderUuidsMime("application/x-notesapp-folder-uuids");

[[nodiscard]] inline QByteArray encodeUuids(const QStringList& uuids)
{
    return uuids.join(QLatin1Char('\n')).toUtf8();
}

[[nodiscard]] inline QStringList decodeUuids(const QByteArray& data)
{
    return QString::fromUtf8(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace notes::models
