#include "ui/FormatMenu.h"

#include "ui/AppStyle.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QStyleOptionMenuItem>
#include <QToolButton>
#include <QWidgetAction>

#include <algorithm>

namespace notes::ui {

using editor::InlineStyle;
using editor::ParagraphStyle;

namespace {

struct StyleSpec
{
    ParagraphStyle style = ParagraphStyle::Body;
    const char* objectName = nullptr;
    const char* text = nullptr;
    QKeyCombination shortcut;
};

// macOS Notes' shortcuts, with Ctrl for Cmd. None clashes with the app's
// other shortcuts (Ctrl+Shift+L checklist, +A attach, +N new folder).
constexpr std::array<StyleSpec, 8> kStyles{{
    {.style = ParagraphStyle::Title,
     .objectName = "titleStyleAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Title"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_T},
    {.style = ParagraphStyle::Heading,
     .objectName = "headingStyleAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Heading"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_H},
    {.style = ParagraphStyle::Subheading,
     .objectName = "subheadingStyleAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Subheading"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_J},
    {.style = ParagraphStyle::Body,
     .objectName = "bodyStyleAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Body"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_B},
    {.style = ParagraphStyle::Monospaced,
     .objectName = "monospacedStyleAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Monospaced"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_M},
    {.style = ParagraphStyle::BulletedList,
     .objectName = "bulletedListAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "B&ulleted List"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_7},
    {.style = ParagraphStyle::DashedList,
     .objectName = "dashedListAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Dashed List"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_8},
    {.style = ParagraphStyle::NumberedList,
     .objectName = "numberedListAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Numbered List"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_9},
}};

constexpr bool inStyleOrder()
{
    for (std::size_t i = 0; i < kStyles.size(); ++i) {
        if (static_cast<std::size_t>(kStyles.at(i).style) != i) {
            return false;
        }
    }
    return true;
}
static_assert(inStyleOrder(), "kStyles must follow ParagraphStyle's order");

struct InlineSpec
{
    InlineStyle style = InlineStyle::Bold;
    const char* objectName = nullptr;
    const char* text = nullptr;
    QKeyCombination shortcut;
    const char* buttonName = nullptr;
    char glyph = 0; // the button's letter, drawn in the format
};

// The usual shortcuts; Strikethrough as macOS (Shift+Cmd+X).
constexpr std::array<InlineSpec, 4> kInlineStyles{{
    {.style = InlineStyle::Bold,
     .objectName = "boldAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "B&old"),
     .shortcut = Qt::CTRL | Qt::Key_B,
     .buttonName = "boldButton",
     .glyph = 'B'},
    {.style = InlineStyle::Italic,
     .objectName = "italicAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Italic"),
     .shortcut = Qt::CTRL | Qt::Key_I,
     .buttonName = "italicButton",
     .glyph = 'I'},
    {.style = InlineStyle::Underline,
     .objectName = "underlineAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Underline"),
     .shortcut = Qt::CTRL | Qt::Key_U,
     .buttonName = "underlineButton",
     .glyph = 'U'},
    {.style = InlineStyle::Strikethrough,
     .objectName = "strikethroughAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "Stri&kethrough"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_X,
     .buttonName = "strikethroughButton",
     .glyph = 'S'},
}};

constexpr bool inInlineOrder()
{
    for (std::size_t i = 0; i < kInlineStyles.size(); ++i) {
        if (static_cast<std::size_t>(kInlineStyles.at(i).style) != i) {
            return false;
        }
    }
    return true;
}
static_assert(inInlineOrder(), "kInlineStyles must follow InlineStyle's order");

// The letter on an inline format's button, drawn in that format.
QFont glyphFont(QFont font, InlineStyle style)
{
    font.setPointSizeF(font.pointSizeF() * 1.25);
    switch (style) {
    case InlineStyle::Bold:
        font.setBold(true);
        break;
    case InlineStyle::Italic:
        font.setItalic(true);
        break;
    case InlineStyle::Underline:
        font.setUnderline(true);
        break;
    case InlineStyle::Strikethrough:
        font.setStrikeOut(true);
        break;
    }
    return font;
}

// A paragraph style's look in the menu: its font, relative to the menu's.
QFont previewFont(QFont font, ParagraphStyle style)
{
    switch (style) {
    case ParagraphStyle::Title:
        font.setPointSizeF(font.pointSizeF() * editor::StyleManager::kTitleScale);
        font.setBold(true);
        break;
    case ParagraphStyle::Heading:
        font.setPointSizeF(font.pointSizeF() * editor::StyleManager::kHeadingScale);
        font.setBold(true);
        break;
    case ParagraphStyle::Subheading:
        font.setPointSizeF(font.pointSizeF() * editor::StyleManager::kSubheadingScale);
        font.setBold(true);
        break;
    case ParagraphStyle::Monospaced:
        font.setFamilies({QStringLiteral("JetBrains Mono")});
        font.setFixedPitch(true);
        break;
    default:
        break;
    }
    return font;
}

// A list's marker before its name, as the list draws it.
QString listMarker(ParagraphStyle style)
{
    switch (style) {
    case ParagraphStyle::BulletedList:
        return QStringLiteral("\u2022 ");
    case ParagraphStyle::DashedList:
        return QStringLiteral("\u2013 ");
    case ParagraphStyle::NumberedList:
        return QStringLiteral("1. ");
    default:
        return {};
    }
}

} // namespace

FormatMenu::FormatMenu(QWidget* parent)
    : QMenu(tr("Format"), parent)
    , m_group(new QActionGroup(this))
{
    setObjectName(QStringLiteral("formatMenu"));
    // A little more room between the styles than in other menus.
    setProperty(AppStyle::kExtraItemPaddingProperty, kExtraItemPadding);
    setContentsMargins(0, 0, 0, kBottomMargin);

    // The inline formats: a row of buttons, as in macOS Notes.
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("formatInlineRow"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(12, 4, 12, 4);
    rowLayout->setSpacing(6);
    rowLayout->addStretch();
    auto* rowAction = new QWidgetAction(this);
    rowAction->setDefaultWidget(row);
    addAction(rowAction);
    addSeparator();

    // Optional: in a checklist, which has no style here, none is checked.
    m_group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    for (std::size_t i = 0; i < kStyles.size(); ++i) {
        const StyleSpec& spec = kStyles.at(i);
        auto* action = new QAction(tr(spec.text), this);
        action->setObjectName(QLatin1StringView(spec.objectName));
        action->setShortcut(QKeySequence(spec.shortcut));
        // Not in this pop-up (no room kept for it either, so the menu is only
        // as wide as its styles); the menu bar's Format menu still shows it.
        action->setShortcutVisibleInContextMenu(false);
        action->setCheckable(true);
        m_group->addAction(action);
        addAction(action);
        connect(action, &QAction::triggered, this,
                [this, style = spec.style] { emit styleChosen(style); });
        m_actions.at(i) = action;
    }
    for (std::size_t i = 0; i < kInlineStyles.size(); ++i) {
        const InlineSpec& spec = kInlineStyles.at(i);
        // Not items here (the buttons are); the menu bar's Format menu lists
        // them, which keeps their shortcuts working.
        auto* action = new QAction(tr(spec.text), this);
        action->setObjectName(QLatin1StringView(spec.objectName));
        action->setShortcut(QKeySequence(spec.shortcut));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this,
                [this, style = spec.style](bool on) { emit inlineStyleToggled(style, on); });
        m_inlineActions.at(i) = action;

        auto* button = new QToolButton(row);
        button->setObjectName(QLatin1StringView(spec.buttonName));
        button->setText(QString(QLatin1Char(spec.glyph)));
        button->setFont(glyphFont(button->font(), spec.style));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(
            QStringLiteral("%1 (%2)").arg(action->text().remove(QLatin1Char('&')),
                                          action->shortcut().toString(QKeySequence::NativeText)));
        // The menu stays open: more than one format can be set at once.
        connect(button, &QToolButton::clicked, action, [action] { action->trigger(); });
        connect(action, &QAction::changed, button, [button, action] {
            button->setChecked(action->isChecked());
            button->setEnabled(action->isEnabled());
        });
        rowLayout->addWidget(button);
        m_inlineButtons.at(i) = button;
    }
    rowLayout->addStretch();
    setCurrentStyle(ParagraphStyle::Body);
    setStylesEnabled(false);
}

FormatMenu::~FormatMenu() = default;

void FormatMenu::initStyleOption(QStyleOptionMenuItem* option, const QAction* action) const
{
    QMenu::initStyleOption(option, action);
    const auto* found = std::ranges::find(m_actions, action);
    if (found == m_actions.end()) {
        return;
    }
    const auto style = static_cast<ParagraphStyle>(std::distance(m_actions.begin(), found));
    option->font = previewFont(option->font, style);
    option->fontMetrics = QFontMetrics(option->font);
    // The look explains the style; the menu bar's Format menu has the
    // shortcuts and the underlined access keys.
    QString text = action->text();
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QLatin1Char('\x01'), QStringLiteral("&&"));
    option->text = listMarker(style) + text;
    option->reservedShortcutWidth = 0;
}

QList<QAction*> FormatMenu::styleActions() const
{
    return {m_actions.begin(), m_actions.end()};
}

QAction* FormatMenu::styleAction(ParagraphStyle style) const
{
    const auto index = static_cast<std::size_t>(style);
    return index < m_actions.size() ? m_actions.at(index) : nullptr;
}

QList<QAction*> FormatMenu::inlineActions() const
{
    return {m_inlineActions.begin(), m_inlineActions.end()};
}

QAction* FormatMenu::inlineAction(InlineStyle style) const
{
    return m_inlineActions.at(static_cast<std::size_t>(style));
}

void FormatMenu::setCurrentStyle(ParagraphStyle style) const
{
    if (QAction* action = styleAction(style)) {
        action->setChecked(true);
    } else if (QAction* checked = m_group->checkedAction()) {
        checked->setChecked(false);
    }
}

void FormatMenu::setStylesEnabled(bool enabled)
{
    m_group->setEnabled(enabled);
    for (QAction* action : m_inlineActions) {
        action->setEnabled(enabled);
    }
}

} // namespace notes::ui
