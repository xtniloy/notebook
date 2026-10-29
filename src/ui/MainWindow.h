#pragma once

#include <QMainWindow>

class QAction;
class QSplitter;

namespace notes::app {
class AppContext;
}

namespace notes::ui {

// Three-pane main window (Plan §3.1): sidebar | note list | editor.
// Window size, pane widths and sidebar visibility persist in QSettings.
class MainWindow : public QMainWindow
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MainWindow)

public:
    // Pane widths in pixels. The editor takes whatever is left.
    static constexpr int kSidebarDefaultWidth = 220;
    static constexpr int kNoteListDefaultWidth = 300;
    static constexpr int kSidebarMinimumWidth = 160;
    static constexpr int kNoteListMinimumWidth = 200;
    static constexpr int kEditorMinimumWidth = 320;

    explicit MainWindow(app::AppContext& context, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void setSidebarVisible(bool visible);
    // Sets exact pane widths; the editor gets the remaining space. QSplitter's
    // own setSizes() spreads any difference proportionally across all panes.
    void applyPaneWidths(int sidebarWidth, int noteListWidth);
    void restoreLayout();
    void saveLayout();
    void showAbout();

    app::AppContext& m_context;
    // Declaration order matters: the panes are created inside m_splitter.
    QSplitter* m_splitter;
    QWidget* m_sidebar;
    QWidget* m_noteList;
    QWidget* m_editor;
    QAction* m_toggleSidebar = nullptr;

    // Kept while the sidebar is hidden, so it comes back at the same width.
    int m_sidebarWidth = kSidebarDefaultWidth;
    // Applied on first show, when the splitter knows its real width.
    int m_initialNoteListWidth = kNoteListDefaultWidth;
    bool m_shown = false;
};

} // namespace notes::ui
