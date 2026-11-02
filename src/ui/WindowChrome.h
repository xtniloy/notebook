#pragma once

#include <QList>
#include <QMargins>
#include <QObject>
#include <QSet>

class QMainWindow;
class QPainter;
class QToolButton;
class QWidget;

namespace notes::ui {

// The app's own window frame (task 1.9). The system title bar is replaced by
// the app's header row (menus + window buttons): GNOME's Adwaita decoration
// ignores the app theme and draws no shadow, so the window disappeared against
// light backgrounds.
//
// The window is frameless and translucent, with rounded corners. Around the
// content it paints a soft drop shadow and a 1 px border; that margin is also
// the resize border. Shadow, border and rounding go away while the window is
// maximised or full screen. (Qt can't tell when GNOME snaps a window to half
// the screen, so snapped windows keep them.)
//
// The content widgets (menu bar, panes) are opaque rectangles, so the rounding
// is done by an overlay above them that erases the four corners outside the
// rounded shape and draws the border along it.
class WindowChrome : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WindowChrome)

public:
    static constexpr int kShadowSize = 20;
    static constexpr int kBorderWidth = 1;
    static constexpr int kCornerRadius = 12; // as GNOME (libadwaita) windows
    // Resizing, as in GNOME's own apps: a band of this width just outside the
    // visible edge (the rest of the shadow is not a handle)...
    static constexpr int kResizeBand = 10;
    // ...resizing diagonally within this distance of a corner along an edge...
    static constexpr int kResizeCorner = 20;
    // ...and a square this size just inside each rounded corner, where the
    // content underneath would otherwise take the mouse.
    static constexpr int kCornerGrip = 6;

    // Must run before the window is first shown (translucency can't be added later).
    explicit WindowChrome(QMainWindow* window);
    ~WindowChrome() override;

    // Dragging the empty parts of `area` moves the window; double-clicking them
    // maximises or restores it. For a QMenuBar, the menu titles stay menus.
    void addDragArea(QWidget* area);

    // Minimise, maximise/restore and close, for the right end of the header row.
    [[nodiscard]] QWidget* createWindowButtons(QWidget* parent);

    // The shadow and border around the content; zero when maximised.
    [[nodiscard]] QMargins frameMargins() const;
    // Where the content widgets are laid out: kOverhang pixels beyond the
    // painted content on every side when framed. The corner overlay trims the
    // overhang along the rounded edge, so at fractional display scales no row
    // of window background can show between the widgets and the edge.
    [[nodiscard]] QMargins layoutMargins() const;
    static constexpr int kOverhang = 1;

    // Paints the shadow and the rounded content background. Call from the
    // window's paintEvent(); the corner overlay paints the rest.
    void paint(QPainter& painter) const;

    void toggleMaximized();

    // Which window edges a point in a window of `size` resizes, when the
    // visible window is inset by `margin` (the shadow): within kResizeBand
    // outside the visible edge, diagonally near corners, and in the corner
    // grips. Empty elsewhere, or when `margin` is 0 (maximised).
    [[nodiscard]] static Qt::Edges edgesAt(const QPoint& pos, const QSize& size, int margin);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class CornerOverlay;

    // The window's content area: inside the shadow margin.
    [[nodiscard]] QRect contentRect() const;
    // Erases the corners outside the rounded shape, redraws the shadow there,
    // and draws the border. Called by the overlay above all content.
    void paintCorners(QPainter& painter) const;
    void placeOverlay();
    // The corner grips sit inside the visible corners; hidden when maximised.
    void placeCornerGrips();
    bool handleWindowEvent(QEvent* event);
    bool handleDragAreaEvent(QWidget* area, QEvent* event);
    void updateFrame();

    QMainWindow* m_window;
    QWidget* m_overlay = nullptr;
    QToolButton* m_maximizeButton = nullptr;
    QList<QWidget*> m_cornerGrips;
    // Only compared, never dereferenced, so stale pointers are harmless.
    QSet<const QObject*> m_dragAreas;
};

} // namespace notes::ui
