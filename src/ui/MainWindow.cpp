#include "ui/MainWindow.h"

#include "app/AppContext.h"
#include "app/Config.h"
#include "ui/dialogs/AboutDialog.h"

#include <QAction>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>

#include <algorithm>

namespace notes::ui {

namespace {

constexpr QLatin1StringView kGeometryKey("mainWindow/geometry");
constexpr QLatin1StringView kPaneWidthsKey("mainWindow/paneWidths");
constexpr QLatin1StringView kSidebarVisibleKey("mainWindow/sidebarVisible");

constexpr QSize kDefaultWindowSize(1100, 720);
// Restored widths beyond this are treated as corrupt settings.
constexpr int kMaxRestoredPaneWidth = 2000;

// Splitter positions.
constexpr int kSidebarIndex = 0;
constexpr int kNoteListIndex = 1;
constexpr int kEditorIndex = 2;

QWidget* createPane(QLatin1StringView objectName, int minimumWidth, QWidget* parent)
{
    auto* pane = new QWidget(parent);
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
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    // Placeholders until the Sidebar (1.5), NoteListView (1.6) and EditorView (1.7).
    , m_sidebar(createPane(QLatin1StringView("sidebarPane"), kSidebarMinimumWidth, m_splitter))
    , m_noteList(createPane(QLatin1StringView("noteListPane"), kNoteListMinimumWidth, m_splitter))
    , m_editor(createPane(QLatin1StringView("editorPane"), kEditorMinimumWidth, m_splitter))
{
    setWindowTitle(QString::fromLatin1(config::kDisplayName));

    m_splitter->setObjectName(QStringLiteral("mainSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_noteList);
    m_splitter->addWidget(m_editor);
    // Resizing the window only changes the editor's width, as in macOS Notes.
    m_splitter->setStretchFactor(kSidebarIndex, 0);
    m_splitter->setStretchFactor(kNoteListIndex, 0);
    m_splitter->setStretchFactor(kEditorIndex, 1);
    setCentralWidget(m_splitter);

    // Menus move into the ActionRegistry in task 1.4.
    m_toggleSidebar = new QAction(tr("Show &Sidebar"), this);
    m_toggleSidebar->setObjectName(QStringLiteral("toggleSidebarAction"));
    m_toggleSidebar->setCheckable(true);
    m_toggleSidebar->setChecked(true);
    m_toggleSidebar->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    connect(m_toggleSidebar, &QAction::toggled, this, &MainWindow::setSidebarVisible);
    menuBar()->addMenu(tr("&View"))->addAction(m_toggleSidebar);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* about = helpMenu->addAction(tr("&About %1").arg(windowTitle()));
    about->setObjectName(QStringLiteral("aboutAction"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    restoreLayout();
}

MainWindow::~MainWindow()
{
    saveLayout();
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
