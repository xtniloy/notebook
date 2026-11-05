#pragma once

#include "editor/StyleManager.h"

#include <QMenu>

#include <array>

class QActionGroup;
class QToolButton;

namespace notes::ui {

// The toolbar's Aa menu (Plan §3.2), as in macOS Notes: a row of buttons for
// the inline formats (task 3.4), Bold, Italic, Underline and Strikethrough,
// pressed when the selection has them; then the paragraph styles (task 3.2),
// Title, Heading, Subheading, Body and Monospaced, and the lists (task 3.5),
// Bulleted, Dashed and Numbered, each shown in its own look, the current one
// checked. The same actions, with their shortcuts, are in the menu bar's
// Format menu, plain.
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

protected:
    // The styles' look: their font and list marker, without shortcuts.
    void initStyleOption(QStyleOptionMenuItem* option, const QAction* action) const override;

signals:
    void styleChosen(notes::editor::ParagraphStyle style);
    void inlineStyleToggled(notes::editor::InlineStyle style, bool on);

private:
    // Extra room above and below each item (AppStyle::kExtraItemPaddingProperty).
    static constexpr int kExtraItemPadding = 3;
    // Room under the last style, so it doesn't end right at the border.
    static constexpr int kBottomMargin = 8;
    static constexpr std::size_t kStyleCount = 8;
    static constexpr std::size_t kInlineCount = 4;
    std::array<QAction*, kStyleCount> m_actions{};
    std::array<QAction*, kInlineCount> m_inlineActions{};
    std::array<QToolButton*, kInlineCount> m_inlineButtons{};
    QActionGroup* m_group;
};

} // namespace notes::ui
