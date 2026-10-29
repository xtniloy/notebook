#pragma once

#include <Qt>

namespace notes::models {

// Data roles shared by every note list model (the fake one now, NoteListModel in
// task 2.8), so NoteListView works with either.
//
// Rows are either section headers (Qt::DisplayRole is the section title) or
// notes (Qt::DisplayRole is the note title). Headers are plain rows so the view
// can keep uniform row heights (Plan §10: virtualized list).

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

} // namespace notes::models
