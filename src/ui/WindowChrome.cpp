#include "ui/WindowChrome.h"

#include "ui/Icons.h"
#include "ui/Shadow.h"

#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace notes::ui {

namespace {

constexpr int kShadowOffsetY = 2; // light comes from slightly above
// Opacity of the shadow right at the window's edge; it fades to 0 over
// kShadowSize. Light: a hint of depth, not a dark halo.
constexpr qreal kShadowAlpha = 0.09;
// The window edge: a faint line in the text colour's direction, not a grey
// line (a light seam between white panes and the shadow read as a stray line).
constexpr int kBorderAlpha = 28;
constexpr int kButtonSpacing = 8;
constexpr int kButtonSize = 22; // GNOME header bar buttons
constexpr int kButtonIconSize = 14;

QCursor cursorFor(Qt::Edges edges)
{
    const bool horizontal = (edges & (Qt::LeftEdge | Qt::RightEdge)) != 0;
    const bool vertical = (edges & (Qt::TopEdge | Qt::BottomEdge)) != 0;
    if (horizontal && vertical) {
        const bool mainDiagonal =
            edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge);
        return mainDiagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    }
    return horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor;
}

void paintShadow(QPainter& painter, const QRectF& rect, qreal radius, int size)
{
    paintSoftShadow(painter, rect, radius, size, kShadowAlpha);
}

QColor borderColor(const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    return dark ? QColor(255, 255, 255, kBorderAlpha) : QColor(0, 0, 0, kBorderAlpha);
}

// A round header button whose icon is drawn on whole screen pixels, centered on
// the circle. At fractional scaling the circle's center can fall between
// pixels (at 125% the 22 px circle is 27.5 pixels, and the buttons themselves
// sit at fractional positions), and the style then resampled the icon onto
// half pixels, blurring one side so it looked off-center. The circle still
// comes from the style sheet; only the icon is placed here.
class WindowButton : public QToolButton
{
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QStylePainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.icon = QIcon(); // background only; the icon follows
        painter.drawComplexControl(QStyle::CC_ToolButton, option);

        // In device pixels: where the circle's center is, and the icon sizes
        // within a pixel of the exact one (17.5 at 125%). An odd size centers
        // exactly on a half-pixel center, an even one on a whole-pixel center;
        // the best fit wins, and on a tie the size closest to the exact one.
        const QTransform toDevice = painter.deviceTransform();
        const QPointF center = toDevice.map(QRectF(rect()).center());
        const qreal exact = iconSize().width() * toDevice.m11();
        QRectF best;
        qreal bestScore = 0;
        const auto smallest = static_cast<int>(std::ceil(exact - 1));
        const auto largest = static_cast<int>(std::floor(exact + 1));
        for (int pixels = smallest; pixels <= largest; ++pixels) {
            const auto side = static_cast<qreal>(pixels);
            const QRectF candidate(std::round(center.x() - (side / 2)),
                                   std::round(center.y() - (side / 2)), side, side);
            const qreal score = (candidate.center() - center).manhattanLength() +
                                (std::abs(side - exact) / 100); // tie-break
            if (best.isNull() || score < bestScore) {
                best = candidate;
                bestScore = score;
            }
        }
        // Rendered at exactly that many pixels, then shown 1:1.
        QPixmap pixmap = icon().pixmap(best.size().toSize(), 1.0);
        if (pixmap.isNull()) {
            return;
        }
        pixmap.setDevicePixelRatio(devicePixelRatioF());
        painter.drawPixmap(toDevice.inverted().map(best.topLeft()), pixmap);
    }
};

QToolButton* windowButton(QWidget* parent, const char* name, Icon iconId, const QString& toolTip)
{
    auto* button = new WindowButton(parent);
    button->setObjectName(QLatin1StringView(name));
    button->setIcon(icon(iconId));
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip); // icon-only: screen readers need a name
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
    return button;
}

} // namespace

// Sits above all content and paints WindowChrome::paintCorners(). Mouse events
// pass through to the widgets below.
class CornerOverlay : public QWidget
{
public:
    CornerOverlay(const WindowChrome& chrome, QWidget* window)
        : QWidget(window)
        , m_chrome(chrome)
    {
        setObjectName(QStringLiteral("windowCornerOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        m_chrome.paintCorners(painter);
    }

private:
    const WindowChrome& m_chrome;
};

WindowChrome::WindowChrome(QMainWindow* window)
    : QObject(window)
    , m_window(window)
    , m_overlay(new CornerOverlay(*this, window))
{
    m_window->setWindowFlag(Qt::FramelessWindowHint);
    m_window->setAttribute(Qt::WA_TranslucentBackground);
    // Hover events in the margin, to show resize cursors.
    m_window->setMouseTracking(true);
    m_window->installEventFilter(this);
    updateFrame();
}

WindowChrome::~WindowChrome() = default;

QMargins WindowChrome::frameMargins() const
{
    if (m_window->isMaximized() || m_window->isFullScreen()) {
        return {};
    }
    const int margin = kShadowSize + kBorderWidth;
    return {margin, margin, margin, margin};
}

QMargins WindowChrome::layoutMargins() const
{
    const QMargins frame = frameMargins();
    if (frame.isNull()) {
        return frame;
    }
    return frame - QMargins(kOverhang, kOverhang, kOverhang, kOverhang);
}

namespace {

// edgesAt() inside the visible window: only the grips in the corners.
Qt::Edges gripEdgesAt(const QPoint& pos, const QRect& visible, int grip)
{
    const bool left = pos.x() < visible.left() + grip;
    const bool right = pos.x() > visible.right() - grip;
    const bool top = pos.y() < visible.top() + grip;
    const bool bottom = pos.y() > visible.bottom() - grip;
    if (!(left || right) || !(top || bottom)) {
        return {};
    }
    return Qt::Edges(left ? Qt::LeftEdge : Qt::RightEdge) | (top ? Qt::TopEdge : Qt::BottomEdge);
}

// edgesAt() in the band outside the visible window: near a corner, along
// either edge, diagonally.
Qt::Edges bandEdgesAt(const QPoint& pos, const QRect& visible, int corner)
{
    const bool nearLeft = pos.x() < visible.left() + corner;
    const bool nearRight = pos.x() > visible.right() - corner;
    const bool nearTop = pos.y() < visible.top() + corner;
    const bool nearBottom = pos.y() > visible.bottom() - corner;
    const bool outsideX = pos.x() < visible.left() || pos.x() > visible.right();
    const bool outsideY = pos.y() < visible.top() || pos.y() > visible.bottom();
    Qt::Edges edges;
    if (outsideX || (outsideY && (nearLeft || nearRight))) {
        edges |= nearLeft ? Qt::LeftEdge : Qt::RightEdge;
    }
    if (outsideY || (outsideX && (nearTop || nearBottom))) {
        edges |= nearTop ? Qt::TopEdge : Qt::BottomEdge;
    }
    return edges;
}

} // namespace

Qt::Edges WindowChrome::edgesAt(const QPoint& pos, const QSize& size, int margin)
{
    if (margin <= 0) {
        return {};
    }
    const QRect visible =
        QRect(QPoint(0, 0), size).marginsRemoved(QMargins(margin, margin, margin, margin));
    const int band = std::min(kResizeBand, margin);
    if (!visible.adjusted(-band, -band, band, band).contains(pos)) {
        return {}; // the outer part of the shadow
    }
    return visible.contains(pos) ? gripEdgesAt(pos, visible, kCornerGrip)
                                 : bandEdgesAt(pos, visible, kResizeCorner);
}

QRect WindowChrome::contentRect() const
{
    return m_window->rect().marginsRemoved(frameMargins());
}

void WindowChrome::paint(QPainter& painter) const
{
    const QColor background = m_window->palette().color(QPalette::Window);
    if (frameMargins().isNull()) {
        painter.fillRect(m_window->rect(), background);
        return;
    }
    const QRectF content = contentRect();
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(m_window->rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // The shadow only shows in the margin; clipping keeps its layers from being
    // painted behind the content too.
    painter.save();
    painter.setClipRegion(QRegion(m_window->rect()) - QRegion(content.toRect()));
    paintShadow(painter, content.translated(0, kShadowOffsetY), kCornerRadius, kShadowSize);
    painter.restore();

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(content, kCornerRadius, kCornerRadius);
}

void WindowChrome::paintCorners(QPainter& painter) const
{
    if (frameMargins().isNull()) {
        return; // maximised: square, edge to edge
    }
    const QRectF content = contentRect();
    // Everything the content widgets may cover (they overhang the content by
    // kOverhang, see layoutMargins()) outside the rounded shape.
    QPainterPath around;
    around.addRect(content.adjusted(-kOverhang, -kOverhang, kOverhang, kOverhang));
    QPainterPath rounded;
    rounded.addRoundedRect(content, kCornerRadius, kCornerRadius);
    const QPainterPath outside = around.subtracted(rounded);

    painter.setRenderHint(QPainter::Antialiasing);
    // Erase what the content widgets painted outside the rounded shape...
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.fillPath(outside, Qt::black);
    // ...put the shadow back *behind* what is left: fully erased pixels get the
    // whole shadow, half-erased edge pixels exactly their missing share. (A clip
    // path instead isn't anti-aliased, which left a stepped light seam along the
    // curve.) Limited to the corners and the overhang strip around the content;
    // elsewhere the content is opaque anyway.
    const QRect inner = content.toRect();
    const int r = kCornerRadius;
    painter.save();
    painter.setClipRegion(
        QRegion(content.adjusted(-kOverhang, -kOverhang, kOverhang, kOverhang).toAlignedRect()) -
        QRegion(inner.adjusted(r, 0, -r, 0)) - QRegion(inner.adjusted(0, r, 0, -r)));
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOver);
    paintShadow(painter, content.translated(0, kShadowOffsetY), kCornerRadius, kShadowSize);
    painter.restore();
    // ...and outline the shape, just outside the content.
    painter.setPen(QPen(borderColor(m_window->palette()), kBorderWidth));
    painter.setBrush(Qt::NoBrush);
    const qreal half = kBorderWidth / 2.0;
    painter.drawRoundedRect(content.adjusted(-half, -half, half, half), kCornerRadius + half,
                            kCornerRadius + half);
}

void WindowChrome::placeOverlay()
{
    placeCornerGrips();
    m_overlay->setGeometry(m_window->rect());
    m_overlay->raise(); // above everything added since
    m_overlay->update();
}

void WindowChrome::placeCornerGrips()
{
    if (m_cornerGrips.isEmpty()) {
        for (const Qt::Edges corner :
             {Qt::TopEdge | Qt::LeftEdge, Qt::TopEdge | Qt::RightEdge,
              Qt::BottomEdge | Qt::LeftEdge, Qt::BottomEdge | Qt::RightEdge}) {
            auto* grip = new QWidget(m_window);
            grip->setObjectName(QStringLiteral("windowCornerGrip"));
            grip->setProperty("edges", static_cast<int>(corner));
            grip->setCursor(cursorFor(corner));
            grip->installEventFilter(this);
            m_cornerGrips.append(grip);
        }
    }
    const QRect visible = contentRect();
    const bool framed = !frameMargins().isNull();
    for (QWidget* grip : std::as_const(m_cornerGrips)) {
        const auto corner = static_cast<Qt::Edges>(grip->property("edges").toInt());
        const int x =
            corner.testFlag(Qt::LeftEdge) ? visible.left() : visible.right() - kCornerGrip + 1;
        const int y =
            corner.testFlag(Qt::TopEdge) ? visible.top() : visible.bottom() - kCornerGrip + 1;
        grip->setGeometry(x, y, kCornerGrip, kCornerGrip);
        grip->setVisible(framed);
        grip->raise(); // above the content; the overlay goes above all
    }
}

void WindowChrome::toggleMaximized()
{
    if (m_window->isMaximized()) {
        m_window->showNormal();
    } else {
        m_window->showMaximized();
    }
}

QWidget* WindowChrome::createWindowButtons(QWidget* parent)
{
    auto* buttons = new QWidget(parent);
    buttons->setObjectName(QStringLiteral("windowButtons"));
    auto* layout = new QHBoxLayout(buttons);
    // Clear of the rounded top-right corner; vertical room comes from the
    // header's padding, so the buttons centre on the menu titles.
    layout->setContentsMargins(kButtonSpacing, 0, kButtonSpacing + 4, 0);
    layout->setSpacing(kButtonSpacing);

    // Order and side follow GNOME's default (":minimize,maximize,close").
    QToolButton* minimize =
        windowButton(buttons, "windowMinimizeButton", Icon::WindowMinimize, tr("Minimize"));
    m_maximizeButton =
        windowButton(buttons, "windowMaximizeButton", Icon::WindowMaximize, tr("Maximize"));
    QToolButton* close = windowButton(buttons, "windowCloseButton", Icon::Clear, tr("Close"));
    connect(minimize, &QToolButton::clicked, m_window, &QWidget::showMinimized);
    connect(m_maximizeButton, &QToolButton::clicked, this, &WindowChrome::toggleMaximized);
    connect(close, &QToolButton::clicked, m_window, &QWidget::close);
    layout->addWidget(minimize);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(close);

    updateFrame();
    return buttons;
}

void WindowChrome::addDragArea(QWidget* area)
{
    m_dragAreas.insert(area);
    area->installEventFilter(this);
}

void WindowChrome::updateFrame()
{
    m_window->setContentsMargins(layoutMargins());
    placeCornerGrips();
    if (m_overlay != nullptr) {
        m_overlay->update();
    }
    if (m_maximizeButton != nullptr) {
        const bool maximized = m_window->isMaximized();
        m_maximizeButton->setIcon(icon(maximized ? Icon::Copy : Icon::WindowMaximize));
        const QString label = maximized ? tr("Restore") : tr("Maximize");
        m_maximizeButton->setToolTip(label);
        m_maximizeButton->setAccessibleName(label);
    }
    m_window->update();
}

bool WindowChrome::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        return handleWindowEvent(event);
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr) {
        return false;
    }
    if (m_cornerGrips.contains(widget)) {
        const auto* mouse = dynamic_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress && mouse != nullptr &&
            mouse->button() == Qt::LeftButton && m_window->windowHandle() != nullptr) {
            m_window->windowHandle()->startSystemResize(
                static_cast<Qt::Edges>(widget->property("edges").toInt()));
            return true;
        }
        return false;
    }
    if (event->type() == QEvent::Enter && widget->parentWidget() == m_window) {
        // Leaving the margin for the content: drop any resize cursor, which the
        // content would otherwise inherit.
        m_window->unsetCursor();
        return false;
    }
    if (!m_dragAreas.contains(widget)) {
        return false; // a child watched only for Enter
    }
    return handleDragAreaEvent(widget, event);
}

bool WindowChrome::handleWindowEvent(QEvent* event)
{
    switch (event->type()) {
    case QEvent::WindowStateChange:
        updateFrame();
        return false;
    case QEvent::Show:
    case QEvent::Resize:
        placeOverlay();
        return false;
    case QEvent::ChildAdded: {
        // Watch the window's direct children (menu bar, toolbar, central widget)
        // for Enter, to reset the resize cursor.
        const auto* added = dynamic_cast<QChildEvent*>(event);
        if (added != nullptr && added->child()->isWidgetType()) {
            added->child()->installEventFilter(this);
        }
        return false;
    }
    case QEvent::MouseMove: {
        const auto* mouse = dynamic_cast<QMouseEvent*>(event);
        if (mouse == nullptr) {
            return false;
        }
        const Qt::Edges edges =
            edgesAt(mouse->position().toPoint(), m_window->size(), frameMargins().left());
        if (edges != Qt::Edges{}) {
            m_window->setCursor(cursorFor(edges));
        } else {
            m_window->unsetCursor();
        }
        return false;
    }
    case QEvent::MouseButtonPress: {
        const auto* mouse = dynamic_cast<QMouseEvent*>(event);
        if (mouse == nullptr) {
            return false;
        }
        const Qt::Edges edges =
            edgesAt(mouse->position().toPoint(), m_window->size(), frameMargins().left());
        if (mouse->button() == Qt::LeftButton && edges != Qt::Edges{} &&
            m_window->windowHandle() != nullptr) {
            m_window->windowHandle()->startSystemResize(edges);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

bool WindowChrome::handleDragAreaEvent(QWidget* area, QEvent* event)
{
    const bool press = event->type() == QEvent::MouseButtonPress;
    const bool doubleClick = event->type() == QEvent::MouseButtonDblClick;
    if (!press && !doubleClick) {
        return false;
    }
    const auto* mouse = dynamic_cast<QMouseEvent*>(event);
    if (mouse == nullptr || mouse->button() != Qt::LeftButton) {
        return false;
    }
    // Menu titles keep working as menus.
    if (const auto* menuBar = qobject_cast<QMenuBar*>(area);
        menuBar != nullptr && menuBar->actionAt(mouse->position().toPoint()) != nullptr) {
        return false;
    }
    if (doubleClick) {
        toggleMaximized();
        return true;
    }
    if (m_window->windowHandle() != nullptr) {
        m_window->windowHandle()->startSystemMove();
    }
    return true;
}

} // namespace notes::ui
