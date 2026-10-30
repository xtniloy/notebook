#include "ui/WindowChrome.h"

#include "ui/Icons.h"

#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QToolButton>
#include <QWindow>

namespace notes::ui {

namespace {

constexpr int kShadowOffsetY = 3; // light comes from slightly above
constexpr int kShadowAlpha = 38;  // soft: a hint of depth, not a dark halo
constexpr int kButtonSpacing = 6;

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

// A soft shadow around the rounded rectangle `rect`: radial gradients at the
// corners, linear along the sides (a nine-patch of gradients; no blur pass).
void paintShadow(QPainter& painter, const QRectF& rect, qreal radius, qreal size)
{
    const QColor dark(0, 0, 0, kShadowAlpha);
    const QColor clear(0, 0, 0, 0);
    const qreal r = radius;
    const qreal s = size;
    const qreal reach = r + s;

    const auto corner = [&](QPointF center, QRectF area) {
        QRadialGradient gradient(center, reach);
        gradient.setColorAt(0, dark);
        gradient.setColorAt(r / reach, dark);
        gradient.setColorAt(1, clear);
        painter.fillRect(area, gradient);
    };
    corner(rect.topLeft() + QPointF(r, r), QRectF(rect.left() - s, rect.top() - s, reach, reach));
    corner(rect.topRight() + QPointF(-r, r),
           QRectF(rect.right() - r, rect.top() - s, reach, reach));
    corner(rect.bottomLeft() + QPointF(r, -r),
           QRectF(rect.left() - s, rect.bottom() - r, reach, reach));
    corner(rect.bottomRight() + QPointF(-r, -r),
           QRectF(rect.right() - r, rect.bottom() - r, reach, reach));

    const auto side = [&](QPointF from, QPointF to, QRectF area) {
        QLinearGradient gradient(from, to);
        gradient.setColorAt(0, dark);
        gradient.setColorAt(1, clear);
        painter.fillRect(area, gradient);
    };
    const qreal innerWidth = rect.width() - (2 * r);
    const qreal innerHeight = rect.height() - (2 * r);
    side(QPointF(0, rect.top()), QPointF(0, rect.top() - s),
         QRectF(rect.left() + r, rect.top() - s, innerWidth, s));
    side(QPointF(0, rect.bottom()), QPointF(0, rect.bottom() + s),
         QRectF(rect.left() + r, rect.bottom(), innerWidth, s));
    side(QPointF(rect.left(), 0), QPointF(rect.left() - s, 0),
         QRectF(rect.left() - s, rect.top() + r, s, innerHeight));
    side(QPointF(rect.right(), 0), QPointF(rect.right() + s, 0),
         QRectF(rect.right(), rect.top() + r, s, innerHeight));
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
    painter.setRenderHint(QPainter::Antialiasing);
    paintShadow(painter, content.translated(0, kShadowOffsetY), kCornerRadius, kShadowSize);
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
    QPainterPath square;
    square.addRect(content);
    QPainterPath rounded;
    rounded.addRoundedRect(content, kCornerRadius, kCornerRadius);
    const QPainterPath corners = square.subtracted(rounded);

    painter.setRenderHint(QPainter::Antialiasing);
    // Erase what the content widgets painted outside the rounded shape...
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.fillPath(corners, Qt::black);
    // ...put the shadow back there...
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.save();
    painter.setClipPath(corners);
    paintShadow(painter, content.translated(0, kShadowOffsetY), kCornerRadius, kShadowSize);
    painter.restore();
    // ...and outline the shape, just outside the content.
    painter.setPen(QPen(m_window->palette().color(QPalette::Mid), kBorderWidth));
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
    layout->setContentsMargins(kButtonSpacing, 0, 2 * kButtonSpacing, 0);
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
    m_window->setContentsMargins(frameMargins());
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
