#include "ui/AppStyle.h"

#include "ui/Icons.h"
#include "ui/Shadow.h"

#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>

#include <algorithm>

namespace notes::ui {

namespace {

constexpr qreal kHighlightRadius = 5.0;
constexpr int kIconSize = 16;
// Where a menu was last placed after shifting, so a menu shown again without
// being repositioned isn't shifted twice.
constexpr char kShiftedPosProperty[] = "notesMenuShiftedPos";

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
    if (event->type() == QEvent::Show) {
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
    drawItemText(item, QRect(left, row.top(), right - left, row.height()), text, muted, painter,
                 widget);
}

void AppStyle::drawItemText(const QStyleOptionMenuItem& item, const QRect& rect, const QColor& text,
                            const QColor& muted, QPainter* painter, const QWidget* widget) const
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
    const int height = std::max(item->fontMetrics.height(), kIconSize) + (2 * kVerticalPadding);
    int width = contentsSize.width() + (2 * kItemInset) + (2 * kTextPadding) + kShortcutGap;
    if (hasColumn(*item)) {
        width += kColumnWidth;
    }
    if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
        width += kArrowWidth;
    }
    return {width, height};
}

int AppStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                          const QWidget* widget) const
{
    switch (metric) {
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
