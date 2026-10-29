#pragma once

#include <QList>
#include <QObject>

#include <array>
#include <cstdint>

class QAction;
class QActionGroup;

namespace notes::ui {

// The toolbar actions from Plan §3.2, in the documented left-to-right order.
// The search box at the end of the toolbar is backed by FindInNote and
// SearchAllNotes.
enum class ActionId : std::uint8_t {
    ToggleSidebar,
    ListView,
    GalleryView,
    DeleteNote,
    NewNote,
    Format,
    Checklist,
    InsertTable,
    Attachment,
    LockNote,
    ShareExport,
    FindInNote,
    SearchAllNotes,

    Count // not an action; keep last
};

// Creates and owns every app-wide QAction, so menus, the toolbar and tests all
// share one instance per command. Actions that aren't implemented yet start
// disabled; the feature that implements one enables it.
class ActionRegistry : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ActionRegistry)

public:
    explicit ActionRegistry(QObject* parent = nullptr);
    ~ActionRegistry() override;

    [[nodiscard]] QAction* action(ActionId id) const;
    // Every action, in ActionId order.
    [[nodiscard]] QList<QAction*> actions() const;
    // The actions shown as toolbar buttons, left to right (Plan §3.2). The search
    // actions are not buttons; they belong to the search box.
    [[nodiscard]] QList<QAction*> toolbarActions() const;

    // ListView and GalleryView are mutually exclusive.
    [[nodiscard]] QActionGroup* viewModeGroup() const { return m_viewModeGroup; }

private:
    std::array<QAction*, static_cast<std::size_t>(ActionId::Count)> m_actions{};
    QActionGroup* m_viewModeGroup;
};

} // namespace notes::ui
