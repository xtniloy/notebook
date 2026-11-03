#include "ui/FormatMenu.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

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
constexpr std::array<StyleSpec, 5> kStyles{{
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
};

// The usual shortcuts; Strikethrough as macOS (Shift+Cmd+X).
constexpr std::array<InlineSpec, 4> kInlineStyles{{
    {.style = InlineStyle::Bold,
     .objectName = "boldAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "B&old"),
     .shortcut = Qt::CTRL | Qt::Key_B},
    {.style = InlineStyle::Italic,
     .objectName = "italicAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Italic"),
     .shortcut = Qt::CTRL | Qt::Key_I},
    {.style = InlineStyle::Underline,
     .objectName = "underlineAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "&Underline"),
     .shortcut = Qt::CTRL | Qt::Key_U},
    {.style = InlineStyle::Strikethrough,
     .objectName = "strikethroughAction",
     .text = QT_TRANSLATE_NOOP("notes::ui::FormatMenu", "Stri&kethrough"),
     .shortcut = Qt::CTRL | Qt::SHIFT | Qt::Key_X},
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

} // namespace

FormatMenu::FormatMenu(QWidget* parent)
    : QMenu(tr("Format"), parent)
    , m_group(new QActionGroup(this))
{
    setObjectName(QStringLiteral("formatMenu"));
    m_group->setExclusive(true);
    for (std::size_t i = 0; i < kStyles.size(); ++i) {
        const StyleSpec& spec = kStyles.at(i);
        auto* action = new QAction(tr(spec.text), this);
        action->setObjectName(QLatin1StringView(spec.objectName));
        action->setShortcut(QKeySequence(spec.shortcut));
        action->setCheckable(true);
        m_group->addAction(action);
        addAction(action);
        connect(action, &QAction::triggered, this,
                [this, style = spec.style] { emit styleChosen(style); });
        m_actions.at(i) = action;
    }
    addSeparator();
    for (std::size_t i = 0; i < kInlineStyles.size(); ++i) {
        const InlineSpec& spec = kInlineStyles.at(i);
        auto* action = new QAction(tr(spec.text), this);
        action->setObjectName(QLatin1StringView(spec.objectName));
        action->setShortcut(QKeySequence(spec.shortcut));
        action->setCheckable(true);
        addAction(action);
        connect(action, &QAction::triggered, this,
                [this, style = spec.style](bool on) { emit inlineStyleToggled(style, on); });
        m_inlineActions.at(i) = action;
    }
    setCurrentStyle(ParagraphStyle::Body);
    setStylesEnabled(false);
}

FormatMenu::~FormatMenu() = default;

QList<QAction*> FormatMenu::styleActions() const
{
    return {m_actions.begin(), m_actions.end()};
}

QAction* FormatMenu::styleAction(ParagraphStyle style) const
{
    return m_actions.at(static_cast<std::size_t>(style));
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
    styleAction(style)->setChecked(true);
}

void FormatMenu::setStylesEnabled(bool enabled)
{
    m_group->setEnabled(enabled);
    for (QAction* action : m_inlineActions) {
        action->setEnabled(enabled);
    }
}

} // namespace notes::ui
