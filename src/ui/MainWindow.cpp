#include "ui/MainWindow.h"

#include "app/AppContext.h"
#include "app/Config.h"
#include "models/FakeNoteListModel.h"
#include "models/FakeSidebarModel.h"
#include "ui/ActionRegistry.h"
#include "ui/EditorView.h"
#include "ui/Icons.h"
#include "ui/NoteListView.h"
#include "ui/Sidebar.h"
#include "ui/ThemeManager.h"
#include "ui/dialogs/AboutDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QLineEdit>
#include <QMenuBar>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QToolBar>

#include <algorithm>
#include <initializer_list>
#include <optional>

namespace notes::ui {

namespace {

constexpr QLatin1StringView kGeometryKey("mainWindow/geometry");
constexpr QLatin1StringView kPaneWidthsKey("mainWindow/paneWidths");
constexpr QLatin1StringView kSidebarVisibleKey("mainWindow/sidebarVisible");

constexpr QSize kDefaultWindowSize(1100, 720);
// Restored widths beyond this are treated as corrupt settings.
constexpr int kMaxRestoredPaneWidth = 2000;
constexpr int kSearchFieldWidth = 220;
// Enough to show that the list is virtualized (Plan §10, TC-PERF-001).
constexpr int kFakeNoteCount = 10'000;
// The style sheet draws a 1 px line in the middle; the rest is grab area.
constexpr int kSplitterHandleWidth = 5;

// Splitter positions.
constexpr int kSidebarIndex = 0;
constexpr int kNoteListIndex = 1;
constexpr int kEditorIndex = 2;

template <typename Pane = QWidget>
Pane* createPane(QLatin1StringView objectName, int minimumWidth, QWidget* parent)
{
    auto* pane = new Pane(parent);
    pane->setObjectName(objectName);
    pane->setMinimumWidth(minimumWidth);
    return pane;
}

// A stored width, or `fallback` if missing, unparsable, or out of range.
int restoredWidth(const QVariantList& widths, int index, int minimum, int fallback)
{
    if (widths.size() <= index) {
        return fallback;
    }
    bool ok = false;
    const int width = widths.at(index).toInt(&ok);
    return ok && width >= minimum && width <= kMaxRestoredPaneWidth ? width : fallback;
}

} // namespace

MainWindow::MainWindow(app::AppContext& context, QWidget* parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_themes(new ThemeManager(context.settings(), this))
    , m_actions(new ActionRegistry(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_sidebar(
          createPane<Sidebar>(QLatin1StringView("sidebarPane"), kSidebarMinimumWidth, m_splitter))
    , m_noteList(createPane<NoteListView>(QLatin1StringView("noteListPane"), kNoteListMinimumWidth,
                                          m_splitter))
    , m_editor(
          createPane<EditorView>(QLatin1StringView("editorPane"), kEditorMinimumWidth, m_splitter))
    , m_toggleSidebar(m_actions->action(ActionId::ToggleSidebar))
    , m_sidebarModel(new models::FakeSidebarModel(this))
    , m_noteListModel(new models::FakeNoteListModel(
          kFakeNoteCount, QDateTime::currentMSecsSinceEpoch(), QTimeZone::LocalTime, this))
{
    setWindowTitle(QString::fromLatin1(config::kDisplayName));

    m_splitter->setObjectName(QStringLiteral("mainSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(kSplitterHandleWidth);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_noteList);
    m_splitter->addWidget(m_editor);
    // Resizing the window only changes the editor's width, as in macOS Notes.
    m_splitter->setStretchFactor(kSidebarIndex, 0);
    m_splitter->setStretchFactor(kNoteListIndex, 0);
    m_splitter->setStretchFactor(kEditorIndex, 1);
    setCentralWidget(m_splitter);
    m_sidebar->setModel(m_sidebarModel);
    m_noteList->setModel(m_noteListModel);
    connect(m_noteList, &NoteListView::noteSelected, this, &MainWindow::openNote);
    // The list selects its first note without a signal, before anyone listens.
    if (const std::optional<qint64> current = m_noteList->currentNote()) {
        openNote(*current);
    }

    m_toggleSidebar->setChecked(true);
    connect(m_toggleSidebar, &QAction::toggled, this, &MainWindow::setSidebarVisible);
    connect(m_actions->action(ActionId::SearchAllNotes), &QAction::triggered, this,
            &MainWindow::focusSearch);

    createToolBar();
    createMenus();
    restoreLayout();
}

MainWindow::~MainWindow()
{
    saveLayout();
}

void MainWindow::createToolBar()
{
    QToolBar* toolBar = addToolBar(tr("Toolbar"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    // No "hide toolbar" context menu: there would be no way to bring it back.
    toolBar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolBar->toggleViewAction()->setVisible(false);

    // Groups as in Plan §3.1: layout | note | editing, then search on the right.
    for (QAction* action : m_actions->toolbarActions()) {
        if (action == m_actions->action(ActionId::ListView) ||
            action == m_actions->action(ActionId::DeleteNote) ||
            action == m_actions->action(ActionId::Format)) {
            toolBar->addSeparator();
        }
        toolBar->addAction(action);
    }

    auto* spacer = new QWidget(toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);

    m_searchField = new QLineEdit(toolBar);
    m_searchField->setObjectName(QStringLiteral("searchField"));
    m_searchField->setPlaceholderText(tr("Search"));
    m_searchField->setToolTip(m_actions->action(ActionId::SearchAllNotes)->toolTip());
    m_searchField->setClearButtonEnabled(true);
    m_searchField->addAction(icon(Icon::Search), QLineEdit::LeadingPosition);
    m_searchField->setFixedWidth(kSearchFieldWidth);
    toolBar->addWidget(m_searchField);
}

void MainWindow::createMenus()
{
    const auto add = [this](QMenu* menu, std::initializer_list<ActionId> ids) {
        for (const ActionId id : ids) {
            menu->addAction(m_actions->action(id));
        }
    };

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    add(fileMenu, {ActionId::NewNote});
    fileMenu->addSeparator();
    add(fileMenu, {ActionId::LockNote, ActionId::ShareExport});

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    add(editMenu, {ActionId::DeleteNote});
    editMenu->addSeparator();
    add(editMenu, {ActionId::FindInNote, ActionId::SearchAllNotes});

    add(menuBar()->addMenu(tr("F&ormat")),
        {ActionId::Format, ActionId::Checklist, ActionId::InsertTable, ActionId::Attachment});

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    add(viewMenu, {ActionId::ListView, ActionId::GalleryView});
    viewMenu->addSeparator();
    add(viewMenu, {ActionId::ToggleSidebar});
    viewMenu->addSeparator();

    // Appearance: System / Light / Dark (task 1.8), saved by the ThemeManager.
    QMenu* appearance = viewMenu->addMenu(tr("&Appearance"));
    auto* group = new QActionGroup(appearance);
    const auto addTheme = [&](const QString& text, const char* name, ThemePreference preference) {
        QAction* action = appearance->addAction(text);
        action->setObjectName(QLatin1StringView(name));
        action->setCheckable(true);
        action->setChecked(m_themes->preference() == preference);
        group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, preference] { m_themes->setPreference(preference); });
    };
    addTheme(tr("Match &System"), "themeSystemAction", ThemePreference::System);
    addTheme(tr("&Light"), "themeLightAction", ThemePreference::Light);
    addTheme(tr("&Dark"), "themeDarkAction", ThemePreference::Dark);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* about = helpMenu->addAction(tr("&About %1").arg(windowTitle()));
    about->setObjectName(QStringLiteral("aboutAction"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::focusSearch()
{
    m_searchField->setFocus(Qt::ShortcutFocusReason);
    m_searchField->selectAll();
}

void MainWindow::openNote(qint64 noteId)
{
    // Becomes NoteService::get(id) in task 2.7.
    const std::optional<models::FakeNoteListModel::FakeNote> note = m_noteListModel->note(noteId);
    if (!note) {
        m_editor->clear();
        return;
    }
    const QDateTime modified = QDateTime::fromMSecsSinceEpoch(note->modifiedMs);
    if (note->locked) {
        m_editor->showLockedNote(modified);
    } else {
        m_editor->showNote(note->text, modified);
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_shown) {
        m_shown = true;
        applyPaneWidths(m_sidebarWidth, m_initialNoteListWidth);
    }
}

void MainWindow::setSidebarVisible(bool visible)
{
    if (visible == !m_sidebar->isHidden()) {
        return;
    }
    const int noteListWidth = m_shown ? m_noteList->width() : m_initialNoteListWidth;
    if (!visible && m_shown) {
        m_sidebarWidth = m_sidebar->width();
    }
    m_sidebar->setVisible(visible);
    if (m_shown) {
        applyPaneWidths(m_sidebarWidth, noteListWidth);
    }
}

void MainWindow::applyPaneWidths(int sidebarWidth, int noteListWidth)
{
    const bool sidebarVisible = !m_sidebar->isHidden();
    const int handles = m_splitter->handleWidth() * (sidebarVisible ? 2 : 1);
    const int available = m_splitter->contentsRect().width() - handles;
    const int sidebar = sidebarVisible ? sidebarWidth : 0;
    const int editor = std::max(kEditorMinimumWidth, available - sidebar - noteListWidth);
    m_splitter->setSizes({sidebar, noteListWidth, editor});
}

void MainWindow::restoreLayout()
{
    const QSettings& settings = m_context.settings();
    if (!restoreGeometry(settings.value(kGeometryKey).toByteArray())) {
        resize(kDefaultWindowSize);
    }

    const QVariantList widths = settings.value(kPaneWidthsKey).toList();
    m_sidebarWidth =
        restoredWidth(widths, kSidebarIndex, kSidebarMinimumWidth, kSidebarDefaultWidth);
    m_initialNoteListWidth =
        restoredWidth(widths, kNoteListIndex, kNoteListMinimumWidth, kNoteListDefaultWidth);

    m_toggleSidebar->setChecked(settings.value(kSidebarVisibleKey, true).toBool());
}

void MainWindow::saveLayout()
{
    if (!m_shown) {
        return; // widths are meaningless until the window has been laid out
    }
    // isHidden(), not isVisible(): after the window closes nothing is "visible",
    // but the sidebar still holds its last width.
    if (!m_sidebar->isHidden()) {
        m_sidebarWidth = m_sidebar->width();
    }
    QSettings& settings = m_context.settings();
    settings.setValue(kGeometryKey, saveGeometry());
    settings.setValue(kPaneWidthsKey, QVariantList{m_sidebarWidth, m_noteList->width()});
    settings.setValue(kSidebarVisibleKey, !m_sidebar->isHidden());
}

void MainWindow::showAbout()
{
    auto* dialog = new AboutDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

} // namespace notes::ui
