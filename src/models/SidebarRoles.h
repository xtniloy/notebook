#pragma once

#include <Qt>
#include <QtTypes>

#include <cstdint>

namespace notes::models {

// What a sidebar row stands for (Plan §3.3). Stored in kKindRole.
enum class SidebarItemKind : std::uint8_t {
    AccountHeader, // "On My Computer", or the Google account once sync is on
    SectionHeader, // e.g. "Tags"
    AllNotes,
    Folder, // the default "Notes" folder and user folders
    Tag,
    RecentlyDeleted,
};

// Data roles shared by every sidebar model (the fake one now, FolderTreeModel in
// task 2.8), so the Sidebar widget works with either.
// SidebarItemKind, as int.
inline constexpr int kKindRole = Qt::UserRole + 1;
// qint64, on AllNotes, Folder and RecentlyDeleted rows only.
inline constexpr int kFolderIdRole = Qt::UserRole + 2;
// int note count; an invalid QVariant means no badge.
inline constexpr int kCountRole = Qt::UserRole + 3;

// Folder ids are SQLite rowids (always positive), so negative ids are free for
// the two entries that select notes without being a folder.
inline constexpr qint64 kAllNotesFolderId = -1;
inline constexpr qint64 kRecentlyDeletedFolderId = -2;

} // namespace notes::models
