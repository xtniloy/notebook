#pragma once

#include <Qt>

namespace notes::models {

// Data roles shared by every note list model (the fake one now, NoteListModel in
// task 2.8), so NoteListView works with either.
//
// Rows are either section headers (Qt::DisplayRole is the section title) or
// notes (Qt::DisplayRole is the note title). Headers are plain rows, so the
// view needs no special layout for them.

// bool: the row is a section header ("Pinned", "Today", ...).
inline constexpr int kIsSectionHeaderRole = Qt::UserRole + 1;
// qint64, notes only.
inline constexpr int kNoteIdRole = Qt::UserRole + 2;
// QString: first line of the body. Empty for locked notes, which never expose
// their content to the list.
inline constexpr int kPreviewRole = Qt::UserRole + 3;
// QString: "9:41 PM", "Yesterday", "Monday", or a short date.
inline constexpr int kTimeTextRole = Qt::UserRole + 4;
// qint64: last edit, UTC milliseconds since the epoch.
inline constexpr int kModifiedRole = Qt::UserRole + 5;
// bool flags.
inline constexpr int kPinnedRole = Qt::UserRole + 6;
inline constexpr int kLockedRole = Qt::UserRole + 7;
inline constexpr int kHasThumbnailRole = Qt::UserRole + 8;
// QString, notes only: the title of the section the note is in ("Pinned",
// "Today", "March", ...), so views can group without header rows.
inline constexpr int kSectionRole = Qt::UserRole + 9;
// qint64, notes only: the folder the note is in (or was in, when trashed).
inline constexpr int kNoteFolderIdRole = Qt::UserRole + 10;
// QByteArray: the encoded thumbnail of the note's first image (PNG or JPEG),
// empty without one or when locked; kHasThumbnailRole says whether there is one.
inline constexpr int kThumbnailRole = Qt::UserRole + 11;
// QString: what identifies that thumbnail (its attachment's uuid; a thumbnail
// never changes), so views can cache the decoded image.
inline constexpr int kThumbnailKeyRole = Qt::UserRole + 12;

} // namespace notes::models
