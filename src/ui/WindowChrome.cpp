#include "ui/WindowChrome.h"

#include "ui/Icons.h"

#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QToolButton>
#include <QWindow>

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

// A soft shadow around the rounded rectangle `rect`, as nested rounded
// rectangles of tiny opacity whose sum fades out quadratically. Each layer is
// one continuous shape, so there are no seams at fractional display scales
// (125 %), unlike shadows pieced together from corner and side gradients.
void paintShadow(QPainter& painter, const QRectF& rect, qreal radius, int size)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const auto falloff = [size](int distance) {
        const qreal left = 1.0 - (static_cast<qreal>(distance) / size);
        return left * left;
    };
    for (int layer = size; layer >= 1; --layer) {
        const qreal alpha = kShadowAlpha * (falloff(layer - 1) - falloff(layer));
        painter.setBrush(QColor::fromRgbF(0, 0, 0, static_cast<float>(alpha)));
        const qreal grow = layer;
        painter.drawRoundedRect(rect.adjusted(-grow, -grow, grow, grow), radius + grow,
                                radius + grow);
    }
    painter.restore();
}

QColor borderColor(const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    return dark ? QColor(255, 255, 255, kBorderAlpha) : QColor(0, 0, 0, kBorderAlpha);
}

QToolButton* windowButton(QWidget* parent, const char* name, Icon iconId, const QString& toolTip)
{
    auto* button = new QToolButton(parent);
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

Qt::Edges WindowChrome::edgesAt(const QPoint& pos, const QSize& size, int margin)
{
    Qt::Edges edges;
    if (margin <= 0) {
        return edges;
    }
    if (pos.x() < margin) {
        edges |= Qt::LeftEdge;
    } else if (pos.x() >= size.width() - margin) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() < margin) {
        edges |= Qt::TopEdge;
    } else if (pos.y() >= size.height() - margin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
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
    // ...put the shadow back there...
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.save();
    painter.setClipPath(outside);
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
    m_overlay->setGeometry(m_window->rect());
    m_overlay->raise(); // above everything added since
    m_overlay->update();
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
    if (m_overlay != nullptr) {
        m_overlay->update();
    }
    if (m_maximizeButton != nullptr) {
        const bool maximized = m_window->isMaximized();
        m_maximizeButton->setIcon(icon(maximized ? Icon::WindowRestore : Icon::WindowMaximize));
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
