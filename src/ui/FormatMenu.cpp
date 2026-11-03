#include "ui/FormatMenu.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

namespace notes::ui {

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

void FormatMenu::setCurrentStyle(ParagraphStyle style) const
{
    styleAction(style)->setChecked(true);
}

void FormatMenu::setStylesEnabled(bool enabled)
{
    m_group->setEnabled(enabled);
}

} // namespace notes::ui
