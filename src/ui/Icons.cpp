#include "ui/Icons.h"

namespace notes::ui {

namespace {

// No `default:` so the compiler flags any Icon without a file.
const char* iconFileName(Icon icon)
{
    switch (icon) {
    case Icon::SidebarToggle:
        return "panel-left";
    case Icon::ListView:
        return "list";
    case Icon::GalleryView:
        return "layout-grid";
    case Icon::Trash:
        return "trash";
    case Icon::NewNote:
        return "square-pen";
    case Icon::Format:
        return "a-large-small";
    case Icon::Checklist:
        return "list-checks";
    case Icon::Table:
        return "table";
    case Icon::Attachment:
        return "paperclip";
    case Icon::Lock:
        return "lock";
    case Icon::Unlock:
        return "lock-open";
    case Icon::Share:
        return "share";
    case Icon::Search:
        return "search";
    case Icon::Clear:
        return "x";
    case Icon::Folder:
        return "folder";
    case Icon::NewFolder:
        return "folder-plus";
    case Icon::SmartFolder:
        return "folder-cog";
    case Icon::Tag:
        return "hash";
    case Icon::Pin:
        return "pin";
    case Icon::OnMyComputer:
        return "monitor";
    case Icon::Cloud:
        return "cloud";
    case Icon::Expand:
        return "chevron-right";
    case Icon::Collapse:
        return "chevron-down";
    case Icon::Count:
        break;
    }
    return nullptr;
}

} // namespace

QString iconPath(Icon icon)
{
    const char* name = iconFileName(icon);
    if (name == nullptr) {
        return {};
    }
    return QStringLiteral(":/icons/%1.svg").arg(QLatin1StringView(name));
}

QIcon icon(Icon icon)
{
    // Lucide strokes with `currentColor`, which QtSvg renders black. Theme-aware
    // tinting comes with the ThemeManager in task 1.8.
    return QIcon(iconPath(icon));
}

} // namespace notes::ui
