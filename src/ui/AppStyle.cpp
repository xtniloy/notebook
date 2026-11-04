#include "ui/AppStyle.h"

#include "ui/Icons.h"
#include "ui/Shadow.h"
#include "ui/ThemeManager.h"

#include <QAction>
#include <QActionEvent>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>

#include <algorithm>
#include <array>

namespace notes::ui {

namespace {

constexpr qreal kHighlightRadius = 5.0;
constexpr int kIconSize = 16;
// Where a menu was last placed after shifting, so a menu shown again without
// being repositioned isn't shifted twice.
constexpr char kShiftedPosProperty[] = "notesMenuShiftedPos";

// Qt's own text menus (QTextEdit, QLineEdit) name their actions after the
// freedesktop icons they take from the desktop theme, which are colorful.
// These get the app's line icons instead, tinted like the rest.
void useLineIcon(QAction* action)
{
    struct Replacement
    {
        QLatin1StringView name;
        Icon icon = Icon::Count;
    };
    static constexpr std::array kReplacements{
        Replacement{.name = QLatin1StringView("edit-undo"), .icon = Icon::Undo},
        Replacement{.name = QLatin1StringView("edit-redo"), .icon = Icon::Redo},
        Replacement{.name = QLatin1StringView("edit-cut"), .icon = Icon::Cut},
        Replacement{.name = QLatin1StringView("edit-copy"), .icon = Icon::Copy},
        Replacement{.name = QLatin1StringView("edit-paste"), .icon = Icon::Paste},
        Replacement{.name = QLatin1StringView("edit-delete"), .icon = Icon::DeleteText},
        Replacement{.name = QLatin1StringView("select-all"), .icon = Icon::SelectAll},
        Replacement{.name = QLatin1StringView("link-copy"), .icon = Icon::Link},
    };
    const QString name = action->objectName();
    for (const Replacement& replacement : kReplacements) {
        if (name == replacement.name) {
            action->setIcon(icon(replacement.icon));
            return;
        }
    }
}

// Whether the menu reserves a left column for icons or check marks.
bool hasColumn(const QStyleOptionMenuItem& item)
{
    return item.maxIconWidth > 0 || item.menuHasCheckableItems;
}

// The rounded panel inside the shadow margin, in the menu's coordinates.
QRectF panelRect(const QRect& menu)
{
    constexpr int m = AppStyle::kShadowMargin;
    return QRectF(menu).adjusted(m, m, -m, -m);
}

void drawCheckMark(QPainter* painter, const QRect& box, const QColor& color)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    const QRectF r = QRectF(box).adjusted(3.5, 4.5, -3.5, -4.5);
    QPainterPath check;
    check.moveTo(r.left(), r.center().y());
    check.lineTo(r.left() + (r.width() * 0.38), r.bottom());
    check.lineTo(r.right(), r.top());
    painter->drawPath(check);
    painter->restore();
}

} // namespace

AppStyle::AppStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{}

void AppStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);
    if (auto* menu = qobject_cast<QMenu*>(widget)) {
        // Before the menu's window exists: transparency can't be added later.
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setWindowFlag(Qt::NoDropShadowWindowHint); // ours, not a square one
        menu->installEventFilter(this);
        // Actions added before the menu was polished; later ones: eventFilter.
        for (QAction* action : menu->actions()) {
            useLineIcon(action);
        }
    }
}

void AppStyle::unpolish(QWidget* widget)
{
    if (auto* menu = qobject_cast<QMenu*>(widget)) {
        menu->removeEventFilter(this);
    }
    QProxyStyle::unpolish(widget);
}

bool AppStyle::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ActionAdded) {
        if (const auto* added = dynamic_cast<QActionEvent*>(event)) {
            useLineIcon(added->action());
        }
    } else if (event->type() == QEvent::Show) {
        if (auto* menu = qobject_cast<QMenu*>(watched)) {
            // Qt placed the menu's window where the panel belongs; the panel
            // sits kShadowMargin inside it.
            const QVariant shifted = menu->property(kShiftedPosProperty);
            if (!shifted.isValid() || shifted.toPoint() != menu->pos()) {
                menu->move(menu->pos() - QPoint(kShadowMargin, kShadowMargin));
                menu->setProperty(kShiftedPosProperty, menu->pos());
            }
        }
    }
    return QProxyStyle::eventFilter(watched, event);
}

void AppStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                             QPainter* painter, const QWidget* widget) const
{
    switch (element) {
    case PE_PanelMenu: {
        // Shadow, then the panel on the base color with a thin border.
        const QRectF panel = panelRect(option->rect);
        // Light from slightly above: the shadow reaches a little further down.
        paintSoftShadow(*painter, panel.translated(0, 1), kPanelRadius, kShadowMargin - 1,
                        kShadowAlpha);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(option->palette.color(QPalette::Mid), 1));
        painter->setBrush(option->palette.color(QPalette::Base));
        painter->drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), kPanelRadius, kPanelRadius);
        painter->restore();
        return;
    }
    case PE_FrameMenu:
        return; // drawn with the panel
    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
}

void AppStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                           const QWidget* widget) const
{
    if (element == CE_MenuEmptyArea) {
        return; // the panel is already painted; the margin stays transparent
    }
    const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
    if (element == CE_MenuItem && item != nullptr) {
        drawMenuItem(*item, painter, widget);
        return;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

namespace {

QColor itemTextColor(const QPalette& palette, bool enabled, bool selected)
{
    if (selected) {
        return palette.color(QPalette::HighlightedText);
    }
    return palette.color(enabled ? QPalette::Text : QPalette::PlaceholderText);
}

QIcon::Mode itemIconMode(bool enabled, bool selected)
{
    if (!enabled) {
        return QIcon::Disabled;
    }
    return selected ? QIcon::Selected : QIcon::Normal;
}

} // namespace

void AppStyle::drawMenuItem(const QStyleOptionMenuItem& item, QPainter* painter,
                            const QWidget* widget) const
{
    const QPalette& palette = item.palette;
    const QRect row = item.rect.adjusted(kItemInset, 0, -kItemInset, 0);

    if (item.menuItemType == QStyleOptionMenuItem::Separator) {
        painter->fillRect(
            QRect(row.left() + kItemInset, row.center().y(), row.width() - (2 * kItemInset), 1),
            palette.color(QPalette::Mid));
        return;
    }

    const bool enabled = (item.state & State_Enabled) != 0;
    const bool selected = enabled && (item.state & State_Selected) != 0;
    if (selected) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.color(QPalette::Highlight));
        painter->drawRoundedRect(QRectF(row), kHighlightRadius, kHighlightRadius);
        painter->restore();
    }
    const QColor text = itemTextColor(palette, enabled, selected);
    const QColor muted = selected ? text : palette.color(QPalette::PlaceholderText);

    int left = row.left() + kTextPadding;
    if (hasColumn(item)) {
        const QRect column(left - 4, row.top(), kColumnWidth, row.height());
        if (item.checkType != QStyleOptionMenuItem::NotCheckable && item.checked) {
            drawCheckMark(painter, QRect(column.left(), column.center().y() - 7, 14, 14), text);
        } else if (!item.icon.isNull()) {
            const QRect iconRect(column.left(), column.center().y() - (kIconSize / 2), kIconSize,
                                 kIconSize);
            item.icon.paint(painter, iconRect, Qt::AlignCenter, itemIconMode(enabled, selected));
        }
        left += kColumnWidth;
    }

    int right = row.right() - kTextPadding + 1;
    if (item.menuItemType == QStyleOptionMenuItem::SubMenu) {
        const QRect arrow(right - kArrowWidth + 2, row.center().y() - 6, 12, 12);
        icon(Icon::Expand).paint(painter, arrow, Qt::AlignCenter, itemIconMode(true, selected));
        right -= kArrowWidth;
    }
    drawMenuItemText(item, QRect(left, row.top(), right - left, row.height()), text, muted, painter,
                     widget);
}

void AppStyle::drawMenuItemText(const QStyleOptionMenuItem& item, const QRect& rect,
                                const QColor& text, const QColor& muted, QPainter* painter,
                                const QWidget* widget) const
{
    // "Label\tShortcut": the label on the left, the shortcut right-aligned.
    const qsizetype tab = item.text.indexOf(QLatin1Char('\t'));
    const QString label = tab < 0 ? item.text : item.text.left(tab);
    const QString shortcut = tab < 0 ? QString() : item.text.mid(tab + 1);
    const int mnemonic = static_cast<int>(styleHint(SH_UnderlineShortcut, &item, widget) != 0
                                              ? Qt::TextShowMnemonic
                                              : Qt::TextHideMnemonic);
    painter->save();
    painter->setFont(item.font);
    if (!shortcut.isEmpty()) {
        painter->setPen(muted);
        painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, shortcut);
    }
    painter->setPen(text);
    painter->drawText(
        rect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine) | mnemonic,
        label);
    painter->restore();
}

QSize AppStyle::sizeFromContents(ContentsType type, const QStyleOption* option,
                                 const QSize& contentsSize, const QWidget* widget) const
{
    const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
    if (type != CT_MenuItem || item == nullptr) {
        return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    }
    if (item->menuItemType == QStyleOptionMenuItem::Separator) {
        return {contentsSize.width(), kSeparatorHeight};
    }
    // `contentsSize` is the label alone; QMenu adds the widest shortcut after.
    // A menu may draw a label of its own in a font of its own (FormatMenu's
    // styles): room for that too.
    const QString label = item->text.left(item->text.indexOf(QLatin1Char('\t')));
    const int labelWidth =
        item->fontMetrics.boundingRect(QRect(), Qt::TextSingleLine | Qt::TextShowMnemonic, label)
            .width();
    const int extra = widget != nullptr ? widget->property(kExtraItemPaddingProperty).toInt() : 0;
    const int height =
        std::max(item->fontMetrics.height(), kIconSize) + (2 * (kVerticalPadding + extra));
    int width = std::max(contentsSize.width(), labelWidth) + (2 * kItemInset) + (2 * kTextPadding) +
                kShortcutGap;
    if (hasColumn(*item)) {
        width += kColumnWidth;
    }
    if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
        width += kArrowWidth;
    }
    return {width, height};
}

namespace {

// The theme's scroll bar colors, chosen by the palette's brightness.
const ThemeColors& themeColors(const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    return ThemeManager::colors(dark ? Theme::Dark : Theme::Light);
}

} // namespace

void AppStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                  QPainter* painter, const QWidget* widget) const
{
    const auto* bar = qstyleoption_cast<const QStyleOptionSlider*>(option);
    if (control != CC_ScrollBar || bar == nullptr) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
    const ThemeColors& colors = themeColors(bar->palette);
    const bool dragging =
        (bar->state & State_Sunken) != 0 && (bar->activeSubControls & SC_ScrollBarSlider) != 0;
    const bool hovered = (bar->state & State_MouseOver) != 0 || dragging;
    const bool vertical = bar->orientation == Qt::Vertical;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    const bool scrollable = bar->maximum > bar->minimum;
    if (hovered && scrollable) { // nothing to show for a bar with nothing to scroll
        // The track, with the same inset along the bar as the handle's ends.
        const QRectF track =
            vertical ? QRectF(bar->rect).adjusted(0, kScrollHandleInset, 0, -kScrollHandleInset)
                     : QRectF(bar->rect).adjusted(kScrollHandleInset, 0, -kScrollHandleInset, 0);
        const qreal radius = kScrollBarExtent / 2.0;
        painter->setBrush(colors.scrollTrack);
        painter->drawRoundedRect(track, radius, radius);
    }
    const QRect slider = subControlRect(CC_ScrollBar, bar, SC_ScrollBarSlider, widget);
    if (slider.isValid() && scrollable) {
        // Thin at rest; on hover as wide as the track behind it (UI review).
        const int inset = hovered ? 0 : kScrollHandleInset;
        const QRectF handle = vertical ? QRectF(slider).adjusted(inset, 0, -inset, 0)
                                       : QRectF(slider).adjusted(0, inset, 0, -inset);
        const qreal radius = (kScrollBarExtent - (2 * inset)) / 2.0;
        painter->setBrush(hovered ? colors.scrollHandleHover : colors.scrollHandle);
        painter->drawRoundedRect(handle, radius, radius);
    }
    painter->restore();
}

QRect AppStyle::subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                               SubControl subControl, const QWidget* widget) const
{
    const auto* bar = qstyleoption_cast<const QStyleOptionSlider*>(option);
    if (control != CC_ScrollBar || bar == nullptr) {
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }
    // No arrow buttons: the groove is the whole bar, minus a small inset at
    // the ends so the handle's rounded ends clear the track's.
    const bool vertical = bar->orientation == Qt::Vertical;
    const QRect groove = vertical
                             ? bar->rect.adjusted(0, kScrollHandleInset, 0, -kScrollHandleInset)
                             : bar->rect.adjusted(kScrollHandleInset, 0, -kScrollHandleInset, 0);
    const int length = vertical ? groove.height() : groove.width();
    const int range = bar->maximum - bar->minimum;
    int sliderLength = length;
    if (range > 0) {
        const qint64 proportional =
            (static_cast<qint64>(bar->pageStep) * length) / (range + bar->pageStep);
        sliderLength = std::clamp(static_cast<int>(proportional),
                                  std::min(kScrollHandleMinimum, length), length);
    }
    const int start = sliderPositionFromValue(bar->minimum, bar->maximum, bar->sliderPosition,
                                              length - sliderLength, bar->upsideDown);
    const auto along = [&](int from, int size) {
        return vertical ? QRect(groove.left(), groove.top() + from, groove.width(), size)
                        : QRect(groove.left() + from, groove.top(), size, groove.height());
    };
    switch (subControl) {
    case SC_ScrollBarSlider:
        return along(start, sliderLength);
    case SC_ScrollBarSubPage:
        return along(0, start);
    case SC_ScrollBarAddPage:
        return along(start + sliderLength, length - start - sliderLength);
    case SC_ScrollBarGroove:
        return groove;
    case SC_ScrollBarAddLine:
    case SC_ScrollBarSubLine:
    case SC_ScrollBarFirst:
    case SC_ScrollBarLast:
        return {};
    default:
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }
}

int AppStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                          const QWidget* widget) const
{
    switch (metric) {
    case PM_ScrollBarExtent:
        return kScrollBarExtent;
    case PM_ScrollBarSliderMin:
        return kScrollHandleMinimum;
    case PM_MenuPanelWidth:
        return kShadowMargin + 1; // the shadow, then the border
    case PM_MenuHMargin:
        return 0;
    case PM_MenuVMargin:
        return kPanelPadding;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int AppStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                        QStyleHintReturn* returnData) const
{
    if (hint == SH_Menu_Mask) {
        return 0; // the rounded corners are transparent, no window mask
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

} // namespace notes::ui
