#pragma once

#include <QMainWindow>

class QAction;
class QSplitter;

namespace notes::app {
class AppContext;
}

namespace notes::services {
class Autosave;
}

namespace notes::models {
class FolderTreeModel;
class NoteListModel;
} // namespace notes::models

namespace notes::ui {

class ActionRegistry;
class SearchField;
class WindowChrome;
class EditorView;
class NoteListView;
class Sidebar;
class ThemeManager;

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

    [[nodiscard]] ActionRegistry& actions() const { return *m_actions; }
    [[nodiscard]] ThemeManager& themes() const { return *m_themes; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void createToolBar();
    void createMenus();
    void focusSearch();
    void openNote(qint64 noteId);
    // Saves what's pending, then shows the empty state.
    void clearEditor();
    // Delete action: to Recently Deleted, or, there, permanently (task 2.10).
    void deleteCurrentNote();
    void recoverNote(qint64 noteId);
    // After a confirmation; cannot be undone.
    void deletePermanently(qint64 noteId);
    void showNoteMenu(const QPoint& position);
    void setSidebarVisible(bool visible);
    // Sets exact pane widths; the editor gets the remaining space. QSplitter's
    // own setSizes() spreads any difference proportionally across all panes.
    void applyPaneWidths(int sidebarWidth, int noteListWidth);
    void restoreLayout();
    void saveLayout();
    void showAbout();

    app::AppContext& m_context;
    // Before any child widget, so it sees them being added (task 1.9).
    WindowChrome* m_chrome;
    // Before the widgets too, so they are created with the theme's palette.
    ThemeManager* m_themes;
    ActionRegistry* m_actions;
    // Declaration order matters: the panes are created inside m_splitter.
    QSplitter* m_splitter;
    Sidebar* m_sidebar;
    NoteListView* m_noteList;
    EditorView* m_editor;
    QAction* m_toggleSidebar;
    models::FolderTreeModel* m_sidebarModel;
    models::NoteListModel* m_noteListModel;
    // Saves the note in the editor (task 2.9).
    services::Autosave* m_autosave;
    // The note openNote() last asked for; older answers are dropped.
    qint64 m_openingNote = 0;
    SearchField* m_searchField = nullptr;

    // Kept while the sidebar is hidden, so it comes back at the same width.
    int m_sidebarWidth = kSidebarDefaultWidth;
    // Applied on first show, when the splitter knows its real width.
    int m_initialNoteListWidth = kNoteListDefaultWidth;
    bool m_shown = false;
};

} // namespace notes::ui
