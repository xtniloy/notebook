#pragma once

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
// The window is frameless and translucent. Around the content it paints a soft
// drop shadow and a 1 px border; that margin is also the resize border. Both
// go away while the window is maximised or full screen. (Qt can't tell when
// GNOME snaps a window to half the screen, so snapped windows keep the margin.)
class WindowChrome : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WindowChrome)

public:
    static constexpr int kShadowSize = 16;
    static constexpr int kBorderWidth = 1;

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

    // Paints the shadow, the border and the content background. Call from the
    // window's paintEvent().
    void paint(QPainter& painter) const;

    void toggleMaximized();

    // Which window edges a point in a window of `size` resizes, given a border
    // of `margin` pixels. Empty for the interior or when `margin` is 0.
    [[nodiscard]] static Qt::Edges edgesAt(const QPoint& pos, const QSize& size, int margin);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleWindowEvent(QEvent* event);
    bool handleDragAreaEvent(QWidget* area, QEvent* event);
    void updateFrame();

    QMainWindow* m_window;
    QToolButton* m_maximizeButton = nullptr;
    // Only compared, never dereferenced, so stale pointers are harmless.
    QSet<const QObject*> m_dragAreas;
};

} // namespace notes::ui
