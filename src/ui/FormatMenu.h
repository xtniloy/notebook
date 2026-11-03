#pragma once

#include "editor/StyleManager.h"

#include <QMenu>

#include <array>

class QActionGroup;

namespace notes::ui {

// The paragraph styles (Plan §3.2 "Aa", task 3.2): Title, Heading,
// Subheading, Body and Monospaced, the current one checked. Pops up from the
// toolbar's Aa button; the same actions, with their shortcuts, head the menu
// bar's Format menu.
class FormatMenu : public QMenu
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FormatMenu)

public:
    explicit FormatMenu(QWidget* parent = nullptr);
    ~FormatMenu() override;

    // In the Format menu's order (editor::ParagraphStyle's).
    [[nodiscard]] QList<QAction*> styleActions() const;
    [[nodiscard]] QAction* styleAction(editor::ParagraphStyle style) const;

    // Checks `style`'s action.
    void setCurrentStyle(editor::ParagraphStyle style) const;
    // While no note can be edited, no style can be chosen.
    void setStylesEnabled(bool enabled);

signals:
    void styleChosen(notes::editor::ParagraphStyle style);

private:
    static constexpr std::size_t kStyleCount = 5;
    std::array<QAction*, kStyleCount> m_actions{};
    QActionGroup* m_group;
};

} // namespace notes::ui
