#pragma once

#include <QProxyStyle>

class QStyleOptionMenuItem;

namespace notes::ui {

// Fusion, with menus drawn the way modern desktop apps draw them (UI review):
// a rounded panel on the base color with a thin border and a soft drop shadow,
// items with a rounded highlight inset from the sides, shortcuts right-aligned
// in the muted color, a chevron for submenus, a clean check mark, and
// separators inset from the sides. Colors come from the palette, so both
// themes work. Applies to every QMenu: menu bar, right-click menus and Qt's
// own text field menus. Qt's text menus (Undo, Copy, Paste, ...) get the
// app's thin line icons instead of the desktop theme's colorful ones.
//
// Scroll bars, like other modern apps (UI review): a thin rounded handle with
// no arrows and no visible track; while the mouse is over the bar (or drags
// it) a rounded track appears behind the handle, and the handle darkens and
// widens to the track's full width.
//
// The shadow needs a transparent margin around the panel: menus get a
// translucent window, and are moved by that margin when they open, so the
// visible panel lands where Qt meant the menu to be (under the menu title, at
// the mouse). Keep theme.qss free of QMenu rules: with one, Qt's style sheet
// engine draws menus itself and skips this style. The same goes for QScrollBar.
class AppStyle : public QProxyStyle
{
    Q_OBJECT

public:
    AppStyle();

    // Around the panel: the shadow, then a 1 px border.
    static constexpr int kShadowMargin = 8;
    static constexpr int kPanelRadius = 8;
    static constexpr qreal kShadowAlpha = 0.16;
    static constexpr int kPanelPadding = 5; // above the first and below the last item

    // Layout of a menu item row.
    static constexpr int kItemInset = 5;    // highlight from the panel's sides
    static constexpr int kTextPadding = 12; // text from the highlight's left edge
    static constexpr int kColumnWidth = 22; // icon or check mark column, when used
    static constexpr int kShortcutGap = 32; // between the text and its shortcut
    static constexpr int kArrowWidth = 14;  // submenu chevron
    static constexpr int kVerticalPadding = 5;
    static constexpr int kSeparatorHeight = 9;
    // An int property a menu can set: more room above and below each of its
    // items than kVerticalPadding (FormatMenu's styles, as in macOS Notes).
    static constexpr const char* kExtraItemPaddingProperty = "extraItemPadding";

    // Scroll bars.
    static constexpr int kScrollBarExtent = 12;  // thickness
    static constexpr int kScrollHandleInset = 2; // handle from the bar's sides, at rest
    static constexpr int kScrollHandleMinimum = 32;

    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;
    using QProxyStyle::polish;
    using QProxyStyle::unpolish;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override;
    [[nodiscard]] QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                                         const QSize& contentsSize,
                                         const QWidget* widget = nullptr) const override;
    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                                  const QWidget* widget = nullptr) const override;
    [[nodiscard]] int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                                const QWidget* widget = nullptr,
                                QStyleHintReturn* returnData = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override;
    [[nodiscard]] QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                                       SubControl subControl,
                                       const QWidget* widget = nullptr) const override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void drawMenuItem(const QStyleOptionMenuItem& item, QPainter* painter,
                      const QWidget* widget) const;
    void drawMenuItemText(const QStyleOptionMenuItem& item, const QRect& rect, const QColor& text,
                          const QColor& muted, QPainter* painter, const QWidget* widget) const;
};

} // namespace notes::ui
