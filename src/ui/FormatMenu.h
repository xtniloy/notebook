#pragma once

#include "editor/StyleManager.h"

#include <QMenu>

#include <array>

class QActionGroup;

namespace notes::ui {

// The paragraph styles (Plan §3.2 "Aa", task 3.2): Title, Heading,
// Subheading, Body and Monospaced, then the lists (task 3.5): Bulleted,
// Dashed and Numbered, the current one checked; then the inline
// formats (task 3.4): Bold, Italic, Underline and Strikethrough, checked when
// the text at the cursor has them. Pops up from the toolbar's Aa button; the
// same actions, with their shortcuts, are in the menu bar's Format menu.
class FormatMenu : public QMenu
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FormatMenu)

public:
    explicit FormatMenu(QWidget* parent = nullptr);
    ~FormatMenu() override;

    // In the Format menu's order (editor::ParagraphStyle's).
    [[nodiscard]] QList<QAction*> styleActions() const;
    // Null for Checklist, which isn't here (it has its own action).
    [[nodiscard]] QAction* styleAction(editor::ParagraphStyle style) const;

    // In the Format menu's order (editor::InlineStyle's).
    [[nodiscard]] QList<QAction*> inlineActions() const;
    [[nodiscard]] QAction* inlineAction(editor::InlineStyle style) const;

    // Checks `style`'s action; none for Checklist.
    void setCurrentStyle(editor::ParagraphStyle style) const;
    // While no note can be edited, no style or inline format can be chosen.
    void setStylesEnabled(bool enabled);

signals:
    void styleChosen(notes::editor::ParagraphStyle style);
    void inlineStyleToggled(notes::editor::InlineStyle style, bool on);

private:
    static constexpr std::size_t kStyleCount = 8;
    static constexpr std::size_t kInlineCount = 4;
    std::array<QAction*, kStyleCount> m_actions{};
    std::array<QAction*, kInlineCount> m_inlineActions{};
    QActionGroup* m_group;
};

} // namespace notes::ui
