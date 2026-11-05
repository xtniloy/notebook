#include "ui/MainWindow.h"

#include "app/AppContext.h"
#include "app/Config.h"
#include "app/IClock.h"
#include "models/Folder.h"
#include "models/FolderTreeModel.h"
#include "models/Note.h"
#include "models/NoteListModel.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/Autosave.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/ActionRegistry.h"
#include "ui/EditorView.h"
#include "ui/FormatMenu.h"
#include "ui/Icons.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"
#include "ui/SearchField.h"
#include "ui/Sidebar.h"
#include "ui/ThemeManager.h"
#include "ui/WindowChrome.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/AlertDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>

namespace notes::ui {

namespace {

// The Attach action's file dialog: images only until file attachments (task 7.2).
QStringList pickImages(QWidget* parent)
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        patterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
    }
    const QString filter = MainWindow::tr("Images (%1)").arg(patterns.join(QLatin1Char(' ')));
    return QFileDialog::getOpenFileNames(
        parent, MainWindow::tr("Add Image"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), filter);
}

constexpr QLatin1StringView kGeometryKey("mainWindow/geometry");
constexpr QLatin1StringView kPaneWidthsKey("mainWindow/paneWidths");
constexpr QLatin1StringView kSidebarVisibleKey("mainWindow/sidebarVisible");
constexpr QLatin1StringView kSortKey("noteList/sort");

// Settings values for the note sort; unknown values mean the default.
constexpr std::array kSortNames{
    std::pair{storage::NoteSort::DateEdited, QLatin1StringView("dateEdited")},
    std::pair{storage::NoteSort::DateCreated, QLatin1StringView("dateCreated")},
    std::pair{storage::NoteSort::Title, QLatin1StringView("title")},
};

storage::NoteSort sortFromSettings(const QString& value)
{
    for (const auto& [sort, name] : kSortNames) {
        if (value == name) {
            return sort;
        }
    }
    return storage::NoteSort::DateEdited;
}

QLatin1StringView settingsNameOf(storage::NoteSort sort)
{
    for (const auto& [candidate, name] : kSortNames) {
        if (candidate == sort) {
            return name;
        }
    }
    return kSortNames.front().second;
}

constexpr QSize kDefaultWindowSize(1100, 720);
// Restored widths beyond this are treated as corrupt settings.
constexpr int kMaxRestoredPaneWidth = 2000;
constexpr int kSearchFieldWidth = 200;
// Standard toolbar icon size; the style's default (24 px in Fusion) made the
// buttons, and the search field that matches their height, look oversized.
constexpr int kToolBarIconSize = 18;
// With the toolbar's 4 px padding and 8 px spacing: 14 px from each window edge.
constexpr int kToolBarEdgeSpacer = 2;
constexpr int kToolBarBottomInset = 10;
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

// A button's place in a toolbar group, for the style sheet.
const char* groupPosition(qsizetype index, qsizetype count)
{
    if (count == 1) {
        return "only";
    }
    if (index == 0) {
        return "first";
    }
    return index == count - 1 ? "last" : "middle";
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
    , m_chrome(new WindowChrome(this))
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
    , m_sidebarModel(new models::FolderTreeModel(context.folders(), context.notes(), this))
    , m_noteListModel(new models::NoteListModel(context.notes(), context.clock(), this))
    , m_autosave(new services::Autosave(context.notes(), this))
    , m_sort(sortFromSettings(context.settings().value(kSortKey).toString()))
    , m_formatMenu(new FormatMenu(this))
{
    setWindowTitle(QString::fromLatin1(config::kDisplayName));
    setMinimumSize(kMinimumWindowSize);

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
    // Both models fill in asynchronously; the views pick the first folder and
    // note as rows arrive, and say so with their signals.
    connect(m_sidebar, &Sidebar::folderSelected, this,
            [this](qint64 folderId) { m_noteListModel->setFolder(folderId, m_sort); });
    connect(m_sidebar, &Sidebar::folderMenuRequested, this, &MainWindow::showFolderMenu);
    connect(m_sidebar, &Sidebar::newFolderRequested, this, [this] { newFolder(std::nullopt); });
    connect(m_sidebarModel, &models::FolderTreeModel::loaded, this,
            &MainWindow::selectPendingFolder);
    connect(m_noteListModel, &QAbstractItemModel::rowsInserted, this,
            &MainWindow::selectPendingNote);
    connect(m_actions->action(ActionId::NewNote), &QAction::triggered, this, &MainWindow::newNote);
    connect(m_noteList, &NoteListView::noteSelected, this, &MainWindow::openNote);
    connect(m_noteList, &NoteListView::currentNoteCleared, this, &MainWindow::clearEditor);
    m_noteList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_noteList, &QWidget::customContextMenuRequested, this, &MainWindow::showNoteMenu);
    connect(m_actions->action(ActionId::DeleteNote), &QAction::triggered, this,
            &MainWindow::deleteCurrentNote);
    connect(m_editor->textEdit(), &QTextEdit::textChanged, m_autosave,
            &services::Autosave::markChanged);
    connect(m_formatMenu, &FormatMenu::styleChosen, this, [this](editor::ParagraphStyle style) {
        m_editor->textEdit()->setParagraphStyle(style);
        updateFormatMenu(); // choosing the checked style again keeps it checked
    });
    connect(m_actions->action(ActionId::Checklist), &QAction::triggered, this, [this] {
        m_editor->textEdit()->toggleChecklist();
        updateFormatMenu();
    });
    connect(m_actions->action(ActionId::Attachment), &QAction::triggered, this,
            &MainWindow::addImages);
    connect(m_editor->textEdit(), &NoteTextEdit::imageRejected, this,
            &MainWindow::showImageRejected);
    m_editor->textEdit()->setMoveCheckedToBottom(
        context.settings().value(kMoveCheckedToBottomKey, false).toBool());
    connect(m_formatMenu, &FormatMenu::inlineStyleToggled, this,
            [this](editor::InlineStyle style, bool on) {
                m_editor->textEdit()->setInlineStyle(style, on);
                updateFormatMenu(); // the check mark follows what applied
            });
    connect(m_editor->textEdit(), &QTextEdit::currentCharFormatChanged, this,
            &MainWindow::updateFormatMenu);
    connect(m_editor->textEdit(), &QTextEdit::cursorPositionChanged, this,
            &MainWindow::updateFormatMenu);
    connect(m_editor->textEdit(), &QTextEdit::textChanged, this, &MainWindow::updateFormatMenu);
    // A selection's formats are all of its text's (StyleManager::selectionHas()).
    connect(m_editor->textEdit(), &QTextEdit::selectionChanged, this,
            &MainWindow::updateFormatMenu);
    connect(m_formatMenu, &QMenu::aboutToShow, this, &MainWindow::updateFormatMenu);
    m_sidebar->setModel(m_sidebarModel);
    m_noteList->setModel(m_noteListModel);
    // Selected during setModel() without a signal, if the rows were there already.
    if (const std::optional<qint64> folder = m_sidebar->currentFolder()) {
        m_noteListModel->setFolder(*folder, m_sort);
    }

    m_toggleSidebar->setChecked(true);
    connect(m_toggleSidebar, &QAction::toggled, this, &MainWindow::setSidebarVisible);
    connect(m_actions->action(ActionId::SearchAllNotes), &QAction::triggered, this,
            &MainWindow::focusSearch);

    createToolBar();
    createMenus();
    restoreLayout();
    updateFormatMenu();
}

MainWindow::~MainWindow()
{
    // Before the editor goes: the autosave reads it. The save itself finishes on
    // the DB thread, which drains its queue when the AppContext closes it.
    m_autosave->untrack();
    saveLayout();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_autosave->flush();
    QMainWindow::closeEvent(event);
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
    toolBar->setIconSize(QSize(kToolBarIconSize, kToolBarIconSize));

    // macOS-style groups of joined buttons (Plan §3.1): layout, view mode and
    // delete on the left; new note beside the editing tools in the middle, apart
    // from delete so a slip can't hit the wrong one; share and search on the
    // right. Order within and across groups is Plan §3.2's.
    const auto addGroup = [&](std::initializer_list<ActionId> ids) {
        auto* group = new QFrame(toolBar);
        group->setObjectName(QStringLiteral("toolGroup"));
        auto* layout = new QHBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        qsizetype index = 0;
        for (const ActionId id : ids) {
            auto* button = new QToolButton(group);
            button->setDefaultAction(m_actions->action(id));
            button->setIconSize(toolBar->iconSize());
            // For the style sheet: rounded outer corners, dividers between buttons.
            button->setProperty("position",
                                groupPosition(index, static_cast<qsizetype>(ids.size())));
            layout->addWidget(button);
            ++index;
        }
        toolBar->addWidget(group);
    };
    // Empty toolbar space is a drag area (WindowChrome), so spacers let clicks through.
    const auto addSpacer = [&] {
        auto* spacer = new QWidget(toolBar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
        toolBar->addWidget(spacer);
    };

    // Fixed spacers at both ends: QToolBar's layout ignores the toolbar's own
    // left/right margins, so the first group sat 4 px from the edge while the
    // search box ended 20 px from it.
    const auto addEdgeSpacer = [&] {
        auto* spacer = new QWidget(toolBar);
        spacer->setFixedWidth(kToolBarEdgeSpacer);
        spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
        toolBar->addWidget(spacer);
    };

    addEdgeSpacer();
    addGroup({ActionId::ToggleSidebar});
    addGroup({ActionId::ListView, ActionId::GalleryView});
    addGroup({ActionId::DeleteNote});
    addSpacer();
    addGroup({ActionId::NewNote});
    addGroup({ActionId::Format, ActionId::Checklist, ActionId::InsertTable, ActionId::Attachment,
              ActionId::LockNote});
    addSpacer();
    addGroup({ActionId::ShareExport});

    // Aa opens the paragraph styles.
    for (auto* button : toolBar->findChildren<QToolButton*>()) {
        if (button->defaultAction() == m_actions->action(ActionId::Format)) {
            button->setMenu(m_formatMenu);
            button->setPopupMode(QToolButton::InstantPopup);
        }
    }

    m_searchField = new SearchField(toolBar);
    m_searchField->setObjectName(QStringLiteral("searchField"));
    m_searchField->setToolTip(m_actions->action(ActionId::SearchAllNotes)->toolTip());
    m_searchField->setFixedWidth(kSearchFieldWidth);
    // As tall as the button groups beside it (macOS Notes). Polished first, so
    // the group's size includes the style sheet's padding and border.
    if (auto* group = toolBar->findChild<QFrame*>(QStringLiteral("toolGroup"))) {
        group->ensurePolished();
        m_searchField->setFixedHeight(group->sizeHint().height());
    }
    toolBar->addWidget(m_searchField);
    addEdgeSpacer();

    m_chrome->addDragArea(toolBar);

    // QToolBar takes its padding and spacing from the style when constructed,
    // before the style sheet applies to it, and re-reads them only on a style
    // change (as a theme switch sends). Send one, so the first theme looks the
    // same as any later one.
    toolBar->ensurePolished();
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(toolBar, &styleChange);

    // On top of the style sheet's uniform padding (QToolBar can't take uneven
    // padding from it). The menu row above ends ~10 px below its text, so the
    // bottom gets that much extra: the gaps above and below the buttons match.
    toolBar->setContentsMargins(0, 0, 0, kToolBarBottomInset);
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
    QAction* newFolderAction =
        fileMenu->addAction(tr("New &Folder"), this, [this] { newFolder(std::nullopt); });
    newFolderAction->setObjectName(QStringLiteral("newFolderAction"));
    newFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    fileMenu->addSeparator();
    add(fileMenu, {ActionId::LockNote, ActionId::ShareExport});

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    // The editor's own (task 3.8): their shortcuts work while it has focus.
    editMenu->addAction(m_editor->textEdit()->pasteAsPlainTextAction());
    editMenu->addAction(m_editor->textEdit()->pasteAsMarkdownAction());
    editMenu->addSeparator();
    add(editMenu, {ActionId::DeleteNote});
    editMenu->addSeparator();
    add(editMenu, {ActionId::FindInNote, ActionId::SearchAllNotes});

    QMenu* formatMenu = menuBar()->addMenu(tr("F&ormat"));
    const QList<QAction*> styles = m_formatMenu->styleActions();
    const auto lists = static_cast<qsizetype>(editor::ParagraphStyle::BulletedList);
    formatMenu->addActions(styles.first(lists));
    formatMenu->addSeparator();
    formatMenu->addActions(styles.sliced(lists));
    formatMenu->addSeparator();
    formatMenu->addActions(m_formatMenu->inlineActions());
    formatMenu->addSeparator();
    add(formatMenu, {ActionId::Checklist});
    // The "Move checked items to bottom" setting (Plan §3.5), kept in QSettings.
    QAction* moveChecked = formatMenu->addAction(tr("&Move Checked Items to Bottom"));
    moveChecked->setObjectName(QStringLiteral("moveCheckedToBottomAction"));
    moveChecked->setCheckable(true);
    moveChecked->setChecked(m_editor->textEdit()->moveCheckedToBottom());
    connect(moveChecked, &QAction::toggled, this, [this](bool move) {
        m_editor->textEdit()->setMoveCheckedToBottom(move);
        m_context.settings().setValue(kMoveCheckedToBottomKey, move);
    });
    add(formatMenu, {ActionId::InsertTable, ActionId::Attachment});
    connect(formatMenu, &QMenu::aboutToShow, this, &MainWindow::updateFormatMenu);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    add(viewMenu, {ActionId::ListView, ActionId::GalleryView});
    viewMenu->addSeparator();
    add(viewMenu, {ActionId::ToggleSidebar});
    viewMenu->addSeparator();

    // Sort Notes By (task 2.11), saved in QSettings. Recently Deleted always
    // lists by deletion date.
    QMenu* sortMenu = viewMenu->addMenu(tr("&Sort Notes By"));
    sortMenu->setObjectName(QStringLiteral("sortMenu"));
    auto* sortGroup = new QActionGroup(sortMenu);
    const auto addSort = [&](const QString& text, storage::NoteSort sort) {
        QAction* action = sortMenu->addAction(text);
        action->setObjectName(QStringLiteral("sort_%1").arg(settingsNameOf(sort)));
        action->setCheckable(true);
        action->setChecked(m_sort == sort);
        sortGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, sort] { setNoteSort(sort); });
    };
    addSort(tr("Date &Edited"), storage::NoteSort::DateEdited);
    addSort(tr("Date &Created"), storage::NoteSort::DateCreated);
    addSort(tr("&Title"), storage::NoteSort::Title);
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

    // The menu row is the header (task 1.9): window buttons on the right, and
    // dragging its empty part moves the window.
    menuBar()->setObjectName(QStringLiteral("headerBar"));
    menuBar()->setCornerWidget(m_chrome->createWindowButtons(menuBar()), Qt::TopRightCorner);
    m_chrome->addDragArea(menuBar());
}

void MainWindow::focusSearch()
{
    m_searchField->setFocus(Qt::ShortcutFocusReason);
    m_searchField->selectAll();
}

void MainWindow::openNote(qint64 noteId)
{
    // Save the note being left now, before its replacement is even loaded. The
    // load below is queued behind the save, so reopening it shows the saved text.
    m_autosave->flush();
    m_openingNote = noteId;
    m_context.notes().open(noteId).then(
        this, [this, noteId](const storage::DbResult<services::OpenedNote>& opened) {
            if (noteId != m_openingNote) {
                return; // another note was selected meanwhile
            }
            // Anything typed into the old note while this one loaded.
            m_autosave->untrack();
            if (!opened) {
                m_editor->clear();
                updateFormatMenu();
                return;
            }
            const models::Note& note = opened.value().note;
            const QDateTime modified = QDateTime::fromMSecsSinceEpoch(note.updatedAt);
            if (note.locked) {
                m_editor->showLockedNote(modified);
                updateFormatMenu();
                return;
            }
            m_editor->showNoteBody(note.body, note.bodyText, modified, opened.value().attachments);
            if (noteId == m_focusEditorFor) {
                m_focusEditorFor = 0;
                m_editor->textEdit()->setFocus(Qt::OtherFocusReason);
            }
            if (note.isTrashed()) {
                // Read-only in Recently Deleted, as in macOS Notes: recover it to edit.
                m_editor->textEdit()->setReadOnly(true);
                updateFormatMenu();
                return;
            }
            const NoteTextEdit* edit = m_editor->textEdit();
            m_autosave->track(noteId, [edit] {
                return services::Autosave::Content{.body = edit->noteBody(),
                                                   .text = edit->notePlainText(),
                                                   .attachments = edit->attachments()};
            });
            updateFormatMenu();
        });
}

void MainWindow::deleteCurrentNote()
{
    const std::optional<qint64> note = m_noteList->currentNote();
    if (!note) {
        return;
    }
    if (m_noteListModel->folderId() == models::kRecentlyDeletedFolderId) {
        deletePermanently(*note);
        return;
    }
    // The first time, say where deleted notes go (macOS Notes). Escape there
    // deletes nothing, and the notice comes again next time.
    QSettings& settings = m_context.settings();
    if (!settings.value(kTrashNoticeShownKey, false).toBool()) {
        AlertDialog notice(tr("Deleted notes are moved to the Recently Deleted folder."),
                           tr("You can recover or remove notes from Recently Deleted within "
                              "30 days."),
                           this);
        notice.setObjectName(QStringLiteral("trashNoticeDialog"));
        notice.addButton(tr("OK"), AlertDialog::ButtonRole::Accept);
        if (notice.exec() != QDialog::Accepted) {
            return;
        }
        settings.setValue(kTrashNoticeShownKey, true);
    }
    m_autosave->flush(); // the last keystrokes go with it
    (void)m_context.notes().trash(*note);
}

void MainWindow::recoverNote(qint64 noteId)
{
    (void)m_context.notes().restore(noteId);
}

void MainWindow::deletePermanently(qint64 noteId)
{
    AlertDialog confirm(tr("Delete this note permanently?"), tr("You can't undo this action."),
                        this);
    confirm.setObjectName(QStringLiteral("deletePermanentlyDialog"));
    confirm.addButton(tr("Cancel"), AlertDialog::ButtonRole::Cancel); // the default
    confirm.addButton(tr("Delete"), AlertDialog::ButtonRole::Accept);
    if (confirm.exec() == QDialog::Accepted) {
        (void)m_context.notes().purge(noteId);
    }
}

void MainWindow::showNoteMenu(const QPoint& position)
{
    const QModelIndex index = m_noteList->indexAt(position);
    const QVariant id = index.data(models::kNoteIdRole);
    if (!id.isValid()) {
        return; // a header or empty space
    }
    const qint64 noteId = id.toLongLong();
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("noteMenu"));
    if (m_noteListModel->folderId() == models::kRecentlyDeletedFolderId) {
        menu.addAction(tr("&Recover"), this, [this, noteId] { recoverNote(noteId); })
            ->setObjectName(QStringLiteral("recoverNoteAction"));
        menu.addAction(tr("&Delete Permanently…"), this,
                       [this, noteId] { deletePermanently(noteId); })
            ->setObjectName(QStringLiteral("deletePermanentlyAction"));
    } else {
        const bool pinned = index.data(models::kPinnedRole).toBool();
        menu.addAction(
                pinned ? tr("Un&pin Note") : tr("&Pin Note"), this,
                [this, noteId, pinned] { (void)m_context.notes().setPinned(noteId, !pinned); })
            ->setObjectName(QStringLiteral("pinNoteAction"));

        // Move To: every folder, indented under its parent; not its own.
        QMenu* moveMenu = menu.addMenu(tr("&Move To"));
        moveMenu->setObjectName(QStringLiteral("moveNoteMenu"));
        const qint64 from = index.data(models::kNoteFolderIdRole).toLongLong();
        QList<std::pair<QModelIndex, int>> pending{{QModelIndex(), -1}};
        while (!pending.isEmpty()) {
            const auto [parent, depth] = pending.takeFirst();
            QList<std::pair<QModelIndex, int>> children;
            for (int row = 0; row < m_sidebarModel->rowCount(parent); ++row) {
                const QModelIndex child = m_sidebarModel->index(row, 0, parent);
                const bool isFolder = child.data(models::kKindRole).toInt() ==
                                      static_cast<int>(models::SidebarItemKind::Folder);
                if (isFolder) {
                    const qint64 folder = child.data(models::kFolderIdRole).toLongLong();
                    QAction* target = moveMenu->addAction(
                        QString(qsizetype{depth} * 4, QLatin1Char(' ')) + child.data().toString(),
                        this, [this, noteId, folder] {
                            (void)m_context.notes().moveToFolder(noteId, folder);
                        });
                    target->setObjectName(QStringLiteral("moveTo_%1").arg(folder));
                    target->setEnabled(folder != from);
                }
                // Folders' subfolders come right after them.
                children.append({child, isFolder ? depth + 1 : depth});
            }
            for (qsizetype i = 0; i < children.size(); ++i) {
                pending.insert(i, children.at(i));
            }
        }

        menu.addAction(tr("D&uplicate"), this, [this, noteId] { duplicateNote(noteId); })
            ->setObjectName(QStringLiteral("duplicateNoteAction"));
        menu.addSeparator();
        menu.addAction(m_actions->action(ActionId::DeleteNote));
    }
    menu.exec(m_noteList->viewport()->mapToGlobal(position));
}

void MainWindow::newNote()
{
    m_autosave->flush();
    const qint64 folder = m_noteListModel->folderId();
    // Recently Deleted (or nothing yet) can't take notes: they go to "Notes",
    // which the sidebar then shows.
    const bool elsewhere = folder == models::kRecentlyDeletedFolderId || folder == 0;
    m_context.notes()
        .create(elsewhere ? models::kAllNotesFolderId : folder, {}, {})
        .then(this, [this, elsewhere](const storage::DbResult<models::Note>& note) {
            if (!note) {
                showError(tr("The note could not be created."), note.error().message);
                return;
            }
            m_pendingNote = note.value().id;
            m_focusEditorFor = note.value().id;
            if (elsewhere) {
                m_sidebar->selectFolder(note.value().folderId);
            }
            selectPendingNote();
        });
}

void MainWindow::duplicateNote(qint64 noteId)
{
    m_autosave->flush(); // the copy has the latest text
    m_context.notes().duplicate(noteId).then(
        this, [this](const storage::DbResult<models::Note>& copy) {
            if (!copy) {
                showError(tr("The note could not be duplicated."), copy.error().message);
                return;
            }
            m_pendingNote = copy.value().id;
            selectPendingNote();
        });
}

void MainWindow::selectPendingNote()
{
    if (m_pendingNote && m_noteList->selectNote(*m_pendingNote)) {
        m_pendingNote.reset();
    }
}

void MainWindow::selectPendingFolder()
{
    if (m_pendingFolder && m_sidebar->selectFolder(*m_pendingFolder)) {
        m_pendingFolder.reset();
    }
}

void MainWindow::setNoteSort(storage::NoteSort sort)
{
    if (sort == m_sort) {
        return;
    }
    m_sort = sort;
    m_context.settings().setValue(kSortKey, QString(settingsNameOf(sort)));
    if (const std::optional<qint64> folder = m_sidebar->currentFolder()) {
        m_autosave->flush();
        const std::optional<qint64> note = m_noteList->currentNote();
        m_noteListModel->setFolder(*folder, sort);
        m_pendingNote = note; // keep it selected in the new order
    }
}

void MainWindow::showFolderMenu(qint64 folderId, const QPoint& globalPosition)
{
    const QModelIndex index = m_sidebarModel
                                  ->match(m_sidebarModel->index(0, 0), models::kFolderIdRole,
                                          folderId, 1, Qt::MatchExactly | Qt::MatchRecursive)
                                  .value(0);
    // "Notes" can't be renamed or deleted (FolderRepository refuses too).
    const bool isDefault = index.data(models::kIsDefaultFolderRole).toBool();
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("folderMenu"));
    menu.addAction(tr("New &Subfolder…"), this, [this, folderId] { newFolder(folderId); })
        ->setObjectName(QStringLiteral("newSubfolderAction"));
    QAction* rename =
        menu.addAction(tr("&Rename Folder…"), this, [this, folderId] { renameFolder(folderId); });
    rename->setObjectName(QStringLiteral("renameFolderAction"));
    rename->setEnabled(!isDefault);
    menu.addSeparator();
    QAction* remove =
        menu.addAction(tr("&Delete Folder…"), this, [this, folderId] { deleteFolder(folderId); });
    remove->setObjectName(QStringLiteral("deleteFolderAction"));
    remove->setEnabled(!isDefault);
    menu.exec(globalPosition);
}

std::optional<QString> MainWindow::askFolderName(const QString& title, const QString& initial,
                                                 const QString& acceptText)
{
    AlertDialog dialog(title, tr("Enter a name for this folder."), this);
    dialog.setObjectName(QStringLiteral("folderNameDialog"));
    const QLineEdit* field = dialog.addTextField(initial);
    dialog.addButton(tr("Cancel"), AlertDialog::ButtonRole::Cancel);
    dialog.addButton(acceptText, AlertDialog::ButtonRole::Accept); // the default
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const QString name = field->text().trimmed();
    return name.isEmpty() ? std::nullopt : std::optional<QString>(name);
}

void MainWindow::newFolder(std::optional<qint64> parentId)
{
    const std::optional<QString> name = askFolderName(
        parentId ? tr("New Subfolder") : tr("New Folder"), tr("New Folder"), tr("Create"));
    if (!name) {
        return;
    }
    m_context.folders()
        .create(*name, parentId)
        .then(this, [this](const storage::DbResult<models::Folder>& folder) {
            if (!folder) {
                showError(tr("The folder could not be created."), folder.error().message);
                return;
            }
            m_pendingFolder = folder.value().id; // selected once the tree reloads
        });
}

void MainWindow::renameFolder(qint64 folderId)
{
    const QModelIndex index = m_sidebarModel
                                  ->match(m_sidebarModel->index(0, 0), models::kFolderIdRole,
                                          folderId, 1, Qt::MatchExactly | Qt::MatchRecursive)
                                  .value(0);
    const std::optional<QString> name =
        askFolderName(tr("Rename Folder"), index.data().toString(), tr("Rename"));
    if (!name) {
        return;
    }
    m_context.folders()
        .rename(folderId, *name)
        .then(this, [this](const storage::DbResult<models::Folder>& folder) {
            if (!folder) {
                showError(tr("The folder could not be renamed."), folder.error().message);
            }
        });
}

void MainWindow::deleteFolder(qint64 folderId)
{
    const QModelIndex index = m_sidebarModel
                                  ->match(m_sidebarModel->index(0, 0), models::kFolderIdRole,
                                          folderId, 1, Qt::MatchExactly | Qt::MatchRecursive)
                                  .value(0);
    AlertDialog confirm(
        tr("Delete the folder \u201c%1\u201d?").arg(index.data().toString()),
        tr("Its subfolders are deleted too, and all their notes move to Recently Deleted."), this);
    confirm.setObjectName(QStringLiteral("deleteFolderDialog"));
    confirm.addButton(tr("Cancel"), AlertDialog::ButtonRole::Cancel); // the default
    confirm.addButton(tr("Delete"), AlertDialog::ButtonRole::Accept);
    if (confirm.exec() != QDialog::Accepted) {
        return;
    }
    m_autosave->flush();
    m_context.folders().remove(folderId).then(this, [this](const storage::DbStatus& status) {
        if (!status) {
            showError(tr("The folder could not be deleted."), status.error().message);
        }
    });
}

void MainWindow::addImages()
{
    NoteTextEdit* edit = m_editor->textEdit();
    if (!m_editor->hasNote() || edit->isReadOnly()) {
        return;
    }
    const QStringList paths = m_imagePicker ? m_imagePicker(this) : pickImages(this);
    if (!paths.isEmpty()) {
        edit->insertImageFiles(paths);
        edit->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::showImageRejected(const QString& message)
{
    AlertDialog alert(tr("The image couldn't be added."), message, this);
    alert.setObjectName(QStringLiteral("imageRejectedDialog"));
    alert.addButton(tr("OK"), AlertDialog::ButtonRole::Accept);
    alert.exec();
}

void MainWindow::showError(const QString& text, const QString& details)
{
    QMessageBox box(QMessageBox::Warning, windowTitle(), text, QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("errorDialog"));
    box.setInformativeText(details);
    box.exec();
}

void MainWindow::clearEditor()
{
    m_openingNote = 0;
    m_autosave->untrack();
    m_editor->clear();
    updateFormatMenu();
}

void MainWindow::updateFormatMenu()
{
    const NoteTextEdit* edit = m_editor->textEdit();
    const bool editable = m_editor->hasNote() && !edit->isReadOnly();
    m_actions->action(ActionId::Format)->setEnabled(editable);
    m_actions->action(ActionId::Checklist)->setEnabled(editable);
    m_actions->action(ActionId::Attachment)->setEnabled(editable);
    edit->pasteAsPlainTextAction()->setEnabled(editable);
    edit->pasteAsMarkdownAction()->setEnabled(editable);
    m_formatMenu->setStylesEnabled(editable);
    m_formatMenu->setCurrentStyle(edit->paragraphStyle());
    using editor::InlineStyle;
    for (const InlineStyle style : {InlineStyle::Bold, InlineStyle::Italic, InlineStyle::Underline,
                                    InlineStyle::Strikethrough}) {
        QAction* action = m_formatMenu->inlineAction(style);
        action->setChecked(edit->inlineStyle(style));
        action->setEnabled(editable && edit->canSetInlineStyle(style));
    }
}

void MainWindow::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    m_chrome->paint(painter);
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
