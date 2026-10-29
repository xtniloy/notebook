#include "ui/ActionRegistry.h"

#include "ui/Icons.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

namespace notes::ui {

namespace {

struct ActionSpec
{
    const char* objectName;
    QString text;
    Icon icon;
    // The `{}` lets specs omit these fields without tripping GCC's
    // -Wmissing-field-initializers, which clang-tidy can't see.
    QKeySequence shortcut{}; // NOLINT(readability-redundant-member-init)
    // Tooltip without the shortcut; empty means the text without mnemonics.
    QString toolTip{};    // NOLINT(readability-redundant-member-init)
    bool enabled = false; // only what is implemented so far
    bool checkable = false;
};

// No `default:` so the compiler flags any ActionId without a spec.
ActionSpec specFor(ActionId id)
{
    switch (id) {
    case ActionId::ToggleSidebar:
        return {.objectName = "toggleSidebarAction",
                .text = ActionRegistry::tr("Show &Sidebar"),
                .icon = Icon::SidebarToggle,
                .shortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
                .toolTip = ActionRegistry::tr("Show or Hide Sidebar"),
                .enabled = true,
                .checkable = true};
    case ActionId::ListView:
        return {.objectName = "listViewAction",
                .text = ActionRegistry::tr("as &List"),
                .icon = Icon::ListView,
                .shortcut = QKeySequence(Qt::CTRL | Qt::Key_1),
                .toolTip = ActionRegistry::tr("View Notes as List"),
                .checkable = true};
    case ActionId::GalleryView:
        return {.objectName = "galleryViewAction",
                .text = ActionRegistry::tr("as &Gallery"),
                .icon = Icon::GalleryView,
                .shortcut = QKeySequence(Qt::CTRL | Qt::Key_2),
                .toolTip = ActionRegistry::tr("View Notes as Gallery"),
                .checkable = true};
    case ActionId::DeleteNote:
        return {.objectName = "deleteNoteAction",
                .text = ActionRegistry::tr("&Delete Note"),
                .icon = Icon::Trash,
                .shortcut = QKeySequence(Qt::Key_Delete),
                .toolTip = ActionRegistry::tr("Move Note to Recently Deleted")};
    case ActionId::NewNote:
        return {.objectName = "newNoteAction",
                .text = ActionRegistry::tr("&New Note"),
                .icon = Icon::NewNote,
                .shortcut = QKeySequence(Qt::CTRL | Qt::Key_N)};
    case ActionId::Format:
        return {.objectName = "formatAction",
                .text = ActionRegistry::tr("&Text Style"),
                .icon = Icon::Format,
                .toolTip = ActionRegistry::tr("Format Text")};
    case ActionId::Checklist:
        return {.objectName = "checklistAction",
                .text = ActionRegistry::tr("&Checklist"),
                .icon = Icon::Checklist,
                .shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)};
    case ActionId::InsertTable:
        return {.objectName = "insertTableAction",
                .text = ActionRegistry::tr("&Table"),
                .icon = Icon::Table,
                .shortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T),
                .toolTip = ActionRegistry::tr("Insert Table")};
    case ActionId::Attachment:
        return {.objectName = "attachmentAction",
                .text = ActionRegistry::tr("&Attach File…"),
                .icon = Icon::Attachment,
                .shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A),
                .toolTip = ActionRegistry::tr("Add Image or File")};
    case ActionId::LockNote:
        return {.objectName = "lockNoteAction",
                .text = ActionRegistry::tr("&Lock Note"),
                .icon = Icon::Lock};
    case ActionId::ShareExport:
        return {.objectName = "shareExportAction",
                .text = ActionRegistry::tr("&Export…"),
                .icon = Icon::Share,
                .toolTip = ActionRegistry::tr("Export as PDF, Markdown or Plain Text")};
    case ActionId::FindInNote:
        return {.objectName = "findInNoteAction",
                .text = ActionRegistry::tr("&Find in Note…"),
                .icon = Icon::Search,
                .shortcut = QKeySequence(Qt::CTRL | Qt::Key_F)};
    case ActionId::SearchAllNotes:
        return {.objectName = "searchAllNotesAction",
                .text = ActionRegistry::tr("Search &All Notes"),
                .icon = Icon::Search,
                .shortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F),
                .enabled = true};
    case ActionId::Count:
        break;
    }
    Q_UNREACHABLE();
}

} // namespace

ActionRegistry::ActionRegistry(QObject* parent)
    : QObject(parent)
    , m_viewModeGroup(new QActionGroup(this))
{
    for (std::size_t i = 0; i < m_actions.size(); ++i) {
        const ActionSpec spec = specFor(static_cast<ActionId>(i));
        auto* action = new QAction(icon(spec.icon), spec.text, this);
        action->setObjectName(QLatin1StringView(spec.objectName));
        action->setShortcut(spec.shortcut);
        action->setCheckable(spec.checkable);
        action->setEnabled(spec.enabled);

        // QAction's default tooltip is the text without mnemonics and ellipsis.
        const QString toolTip = spec.toolTip.isEmpty() ? action->toolTip() : spec.toolTip;
        action->setToolTip(
            spec.shortcut.isEmpty()
                ? toolTip
                : tr("%1 (%2)").arg(toolTip, spec.shortcut.toString(QKeySequence::NativeText)));
        m_actions.at(i) = action;
    }

    m_viewModeGroup->setExclusive(true);
    m_viewModeGroup->addAction(action(ActionId::ListView));
    m_viewModeGroup->addAction(action(ActionId::GalleryView));
    action(ActionId::ListView)->setChecked(true);
}

ActionRegistry::~ActionRegistry() = default;

QAction* ActionRegistry::action(ActionId id) const
{
    return m_actions.at(static_cast<std::size_t>(id));
}

QList<QAction*> ActionRegistry::actions() const
{
    return {m_actions.begin(), m_actions.end()};
}

QList<QAction*> ActionRegistry::toolbarActions() const
{
    QList<QAction*> result = actions();
    result.removeOne(action(ActionId::FindInNote));
    result.removeOne(action(ActionId::SearchAllNotes));
    return result;
}

} // namespace notes::ui
