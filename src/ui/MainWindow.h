#pragma once

#include "storage/NoteRepository.h"

#include <QMainWindow>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

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
class FormatMenu;
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
    // The smallest window: wide enough for the whole toolbar (no "»" overflow
    // menu) and the three panes at their minimum widths, tall enough for a
    // usable note list and editor.
    static constexpr QSize kMinimumWindowSize{760, 480};
    // Set once the first-delete notice has been accepted (AlertDialog).
    static constexpr QLatin1StringView kTrashNoticeShownKey{"notes/trashNoticeShown"};
    // Checking a checklist item moves it to the bottom (task 3.6); off by default.
    static constexpr QLatin1StringView kMoveCheckedToBottomKey{"editor/moveCheckedToBottom"};

    explicit MainWindow(app::AppContext& context, QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] ActionRegistry& actions() const { return *m_actions; }
    [[nodiscard]] ThemeManager& themes() const { return *m_themes; }

    // Asks for image files to add to the note (the Attach action); returns
    // their paths, none if cancelled. A file dialog unless replaced (tests).
    using ImagePicker = std::function<QStringList(QWidget* parent)>;
    void setImagePicker(ImagePicker picker) { m_imagePicker = std::move(picker); }

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
    // The first time, a notice says where deleted notes go.
    void deleteCurrentNote();
    void recoverNote(qint64 noteId);
    // After a confirmation; cannot be undone.
    void deletePermanently(qint64 noteId);
    void showNoteMenu(const QPoint& position);

    // Task 2.11.
    void newNote();
    void duplicateNote(qint64 noteId);
    void showFolderMenu(qint64 folderId, const QPoint& globalPosition);
    void newFolder(std::optional<qint64> parentId);
    void renameFolder(qint64 folderId);
    void deleteFolder(qint64 folderId);
    // Asks for a folder name; nullopt if cancelled or blank. `acceptText`
    // names the confirming button ("Create", "Rename").
    [[nodiscard]] std::optional<QString> askFolderName(const QString& title, const QString& initial,
                                                       const QString& acceptText);
    void showError(const QString& text, const QString& details);
    // Task 3.7: images from the picker into the note at the cursor.
    void addImages();
    void showImageRejected(const QString& message);
    // Selects m_pendingNote / m_pendingFolder once their rows have arrived.
    void selectPendingNote();
    void selectPendingFolder();
    void setNoteSort(storage::NoteSort sort);
    void setSidebarVisible(bool visible);
    // Sets exact pane widths; the editor gets the remaining space. QSplitter's
    // own setSizes() spreads any difference proportionally across all panes.
    void applyPaneWidths(int sidebarWidth, int noteListWidth);
    void restoreLayout();
    void saveLayout();
    void showAbout();
    // The Format menu follows the editor: the current paragraph's style and
    // the inline formats at the cursor are checked, and both can be chosen
    // only while a note can be edited.
    void updateFormatMenu();

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
    // Made by the user and to be selected once listed (new, duplicated).
    std::optional<qint64> m_pendingNote;
    std::optional<qint64> m_pendingFolder;
    // A new note: the editor takes focus once it is shown (TC-UI-021).
    qint64 m_focusEditorFor = 0;
    storage::NoteSort m_sort = storage::NoteSort::DateEdited;
    SearchField* m_searchField = nullptr;
    // Paragraph styles (task 3.2): the Aa button's popup; its actions also
    // head the menu bar's Format menu.
    FormatMenu* m_formatMenu;

    // Kept while the sidebar is hidden, so it comes back at the same width.
    int m_sidebarWidth = kSidebarDefaultWidth;
    // Applied on first show, when the splitter knows its real width.
    int m_initialNoteListWidth = kNoteListDefaultWidth;
    bool m_shown = false;
    ImagePicker m_imagePicker;
};

} // namespace notes::ui
