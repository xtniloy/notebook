#pragma once

#include <QIcon>
#include <QString>

#include <cstdint>

namespace notes::ui {

// Every icon the app uses (Lucide, resources/icons/). Refer to icons only through
// this enum so tst_icons can check that each one exists and renders.
enum class Icon : std::uint8_t {
    SidebarToggle,
    ListView,
    GalleryView,
    Trash,
    NewNote,
    Format,
    Checklist,
    Table,
    Attachment,
    Lock,
    Unlock,
    Share,
    Search,
    Clear,
    Folder,
    NewFolder,
    SmartFolder,
    Tag,
    Pin,
    OnMyComputer,
    Cloud,
    Expand,
    Collapse,
    // Window buttons (close uses Clear).
    WindowMinimize,
    WindowMaximize,
    WindowRestore,

    Count // not an icon; keep last
};

// Resource path, e.g. ":/icons/search.svg".
QString iconPath(Icon icon);

// Drawn in the application palette's text color (HighlightedText for
// QIcon::Selected, the disabled text color for QIcon::Disabled).
QIcon icon(Icon icon);

} // namespace notes::ui
