// The window on real data (task 2.11): TC-UI-021 New Note, TC-UI-022 Delete
// selects the next note, TC-UI-023 Duplicate, TC-UI-024 restart keeps
// everything; plus Pin and Move To, folder actions and the saved sort.

#include "TestHelpers.h"
#include "models/Folder.h"
#include "models/Note.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/ActionRegistry.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"
#include "ui/Sidebar.h"
#include "ui/dialogs/AlertDialog.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <chrono>
#include <functional>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Folder;
using notes::models::Note;
using notes::test::await;
using notes::ui::AlertDialog;
using notes::ui::EditorView;
using notes::ui::MainWindow;
using notes::ui::NoteListView;
using notes::ui::Sidebar;
namespace roles = notes::models;

namespace {

// Runs `handle` once a modal dialog or popup menu of type T is open. Polls,
// so handlers for a menu and the dialog it opens can be set up together.
template <typename T>
void whenOpen(const std::function<void(T*)>& handle, int attemptsLeft = 500)
{
    QTimer::singleShot(10, qApp, [handle, attemptsLeft] {
        QWidget* open = QApplication::activeModalWidget();
        if (qobject_cast<T*>(open) == nullptr) {
            open = QApplication::activePopupWidget();
        }
        if (auto* widget = qobject_cast<T*>(open)) {
            handle(widget);
        } else if (attemptsLeft > 0) {
            whenOpen<T>(handle, attemptsLeft - 1);
        } else {
            QFAIL("the expected dialog or menu never opened");
        }
    });
}

// Triggers the menu action `name` once the popup menu is open.
void chooseFromMenu(const QString& name)
{
    whenOpen<QMenu>([name](QMenu* menu) {
        auto* action = menu->findChild<QAction*>(name);
        QVERIFY2(action, qPrintable(name));
        QVERIFY2(action->isEnabled(), qPrintable(name));
        menu->close();
        action->trigger();
    });
}

// Types `text` into `widget`; QTest::keyClicks() can't type '\n'.
void typeText(QWidget* widget, const QString& text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            QTest::keyClick(widget, Qt::Key_Return);
        }
        QTest::keyClicks(widget, lines.at(i));
    }
}

// The alert's button with `role` ("accept" or "cancel").
QPushButton* alertButton(const QWidget* dialog, const QString& role)
{
    for (QPushButton* button : dialog->findChildren<QPushButton*>()) {
        if (button->property("alertRole").toString() == role) {
            return button;
        }
    }
    return nullptr;
}

// Answers the next folder name dialog: types `name`, then Enter.
void answerName(const QString& name)
{
    whenOpen<AlertDialog>([name](AlertDialog* dialog) {
        QCOMPARE(dialog->objectName(), QStringLiteral("folderNameDialog"));
        auto* field = dialog->findChild<QLineEdit*>(QStringLiteral("alertField"));
        if (field == nullptr) {
            QFAIL("name field expected");
        }
        field->selectAll();
        QTest::keyClicks(field, name);
        QTest::keyClick(field, Qt::Key_Return);
    });
}

// Confirms the next alert (Delete Folder).
void clickDestructive()
{
    whenOpen<AlertDialog>([](AlertDialog* dialog) {
        QPushButton* accept = alertButton(dialog, QStringLiteral("accept"));
        if (accept == nullptr) {
            QFAIL("accept button expected");
        }
        accept->click();
    });
}

} // namespace

class tst_Wiring : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void newNoteInSelectedFolderFocusesEditor();
    void newNoteFromAllNotesAndRecentlyDeleted();
    void deleteKeySelectsNextNote();
    void duplicateCopiesBody();
    void pinAndMoveFromMenu();
    void folderActions();
    void folderNameDialog();
    void defaultFolderCannotBeRenamedOrDeleted();
    void sortIsSavedInSettings();
    void restartKeepsEverything();

private:
    void openWindow();
    void closeWindow();
    Note addNote(const QString& text, qint64 folderId = roles::kAllNotesFolderId);
    [[nodiscard]] QStringList listedTitles() const;
    [[nodiscard]] QModelIndex listIndex(qint64 noteId) const;
    void openNoteMenu(qint64 noteId);
    [[nodiscard]] QString selectedFolderName() const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteListView* m_list = nullptr;
    Sidebar* m_sidebar = nullptr;
    EditorView* m_editor = nullptr;
    qint64 m_notesFolder = 0;
};

void tst_Wiring::initTestCase()
{
    // The LOCALE collation follows the default locale; pin it for stable results.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void tst_Wiring::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    m_notesFolder = await(m_context->folders().ensureDefaultFolder()).value().id;
    // Past the first-delete notice (tst_recentlydeleted covers it).
    m_context->settings().setValue(MainWindow::kTrashNoticeShownKey, true);
}

void tst_Wiring::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_Wiring::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    m_sidebar = m_window->findChild<Sidebar*>(QStringLiteral("sidebarPane"));
    m_editor = m_window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(m_list && m_sidebar && m_editor);
    QTRY_COMPARE(m_sidebar->currentFolder(), std::optional<qint64>(m_notesFolder));
}

void tst_Wiring::closeWindow()
{
    m_window = nullptr;
}

Note tst_Wiring::addNote(const QString& text, qint64 folderId)
{
    notes::test::fakeClockOf(*m_context).advance(1min);
    return await(m_context->notes().create(folderId, {}, text)).value();
}

QStringList tst_Wiring::listedTitles() const
{
    QStringList titles;
    const QAbstractItemModel* model = m_list->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        if (index.data(roles::kNoteIdRole).isValid()) {
            titles.append(index.data().toString());
        }
    }
    return titles;
}

QModelIndex tst_Wiring::listIndex(qint64 noteId) const
{
    const QAbstractItemModel* model = m_list->model();
    return model->match(model->index(0, 0), roles::kNoteIdRole, noteId, 1, Qt::MatchExactly)
        .value(0);
}

void tst_Wiring::openNoteMenu(qint64 noteId)
{
    const QModelIndex index = listIndex(noteId);
    QVERIFY(index.isValid());
    emit m_list->customContextMenuRequested(m_list->visualRect(index).center());
}

QString tst_Wiring::selectedFolderName() const
{
    const auto* tree = m_sidebar->findChild<QTreeView*>(QStringLiteral("sidebarTree"));
    return tree->currentIndex().data().toString();
}

// TC-UI-021
void tst_Wiring::newNoteInSelectedFolderFocusesEditor()
{
    const Folder work = await(m_context->folders().create(QStringLiteral("Work"))).value();
    openWindow();
    QTRY_VERIFY(m_sidebar->selectFolder(work.id));
    QTRY_COMPARE(m_list->model()->rowCount(), 0);

    QTest::keySequence(m_window.get(), QKeySequence(Qt::CTRL | Qt::Key_N));
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("New Note")});
    QTRY_VERIFY(m_editor->textEdit()->hasFocus());
    const qint64 id = m_list->currentNote().value_or(0);
    QCOMPARE(await(m_context->notes().get(id)).value().folderId, work.id);

    // Typing names it (first line), and autosave keeps it.
    typeText(m_editor->textEdit(), QStringLiteral("Groceries\nMilk"));
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("Groceries")});
    QCOMPARE(await(m_context->notes().get(id)).value().bodyText, QStringLiteral("Groceries\nMilk"));
}

void tst_Wiring::newNoteFromAllNotesAndRecentlyDeleted()
{
    openWindow();
    QVERIFY(m_sidebar->selectFolder(roles::kAllNotesFolderId));
    m_window->actions().action(notes::ui::ActionId::NewNote)->trigger();
    QTRY_VERIFY(m_list->currentNote().has_value());
    const Note first = await(m_context->notes().get(m_list->currentNote().value_or(0))).value();
    QCOMPARE(first.folderId, m_notesFolder);

    // Recently Deleted can't take notes: the new one goes to "Notes", shown.
    QVERIFY(m_sidebar->selectFolder(roles::kRecentlyDeletedFolderId));
    m_window->actions().action(notes::ui::ActionId::NewNote)->trigger();
    QTRY_COMPARE(m_sidebar->currentFolder(), std::optional<qint64>(m_notesFolder));
    QTRY_VERIFY(m_list->currentNote().has_value() && *m_list->currentNote() != first.id);
    QTRY_VERIFY(m_editor->textEdit()->hasFocus());
}

// TC-UI-022
void tst_Wiring::deleteKeySelectsNextNote()
{
    const Note a = addNote(QStringLiteral("A"));
    const Note b = addNote(QStringLiteral("B"));
    const Note c = addNote(QStringLiteral("C"));
    openWindow();
    QTRY_COMPARE(listedTitles(),
                 (QStringList{QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("A")}));
    QVERIFY(m_list->selectNote(b.id));
    m_list->setFocus();

    QTest::keyClick(m_list, Qt::Key_Delete);
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("C"), QStringLiteral("A")}));
    QVERIFY(await(m_context->notes().get(b.id)).value().isTrashed());
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(a.id)); // the next one down
    QTRY_COMPARE(m_editor->textEdit()->toPlainText(), QStringLiteral("A"));

    // At the end of the list, the one above.
    QTest::keyClick(m_list, Qt::Key_Delete);
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(c.id));

    // In the editor, Delete edits text instead.
    m_editor->textEdit()->setFocus();
    m_editor->textEdit()->moveCursor(QTextCursor::Start);
    QTest::keyClick(m_editor->textEdit(), Qt::Key_Delete);
    QCOMPARE(m_editor->textEdit()->toPlainText(), QString());
    QCOMPARE(m_list->currentNote(), std::optional<qint64>(c.id));
}

// TC-UI-023
void tst_Wiring::duplicateCopiesBody()
{
    const Note plan = addNote(QStringLiteral("Plan\nStep one\nStep two"));
    openWindow();
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(plan.id));

    chooseFromMenu(QStringLiteral("duplicateNoteAction"));
    openNoteMenu(plan.id);
    QTRY_VERIFY(m_list->currentNote() != std::optional<qint64>(plan.id));
    const Note copy = await(m_context->notes().get(m_list->currentNote().value_or(0))).value();
    QCOMPARE(copy.title, QStringLiteral("Plan copy"));
    QCOMPARE(copy.bodyText, plan.bodyText);
    QCOMPARE(copy.body, plan.body);
    QCOMPARE(copy.folderId, plan.folderId);
    QVERIFY(copy.uuid != plan.uuid);
    QTRY_VERIFY(listedTitles().contains(QStringLiteral("Plan copy")));
}

void tst_Wiring::pinAndMoveFromMenu()
{
    const Folder work = await(m_context->folders().create(QStringLiteral("Work"))).value();
    const Note old = addNote(QStringLiteral("Old"));
    addNote(QStringLiteral("New"));
    openWindow();
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("New"), QStringLiteral("Old")}));

    chooseFromMenu(QStringLiteral("pinNoteAction"));
    openNoteMenu(old.id);
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("Old"), QStringLiteral("New")}));
    QVERIFY(listIndex(old.id).data(roles::kPinnedRole).toBool());

    // Move To lists every folder; the note's own is disabled.
    whenOpen<QMenu>([&](QMenu* menu) {
        QVERIFY(!menu->findChild<QAction*>(QStringLiteral("moveTo_%1").arg(m_notesFolder))
                     ->isEnabled());
        auto* target = menu->findChild<QAction*>(QStringLiteral("moveTo_%1").arg(work.id));
        QVERIFY(target && target->isEnabled());
        menu->close();
        target->trigger();
    });
    openNoteMenu(old.id);
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("New")});
    QCOMPARE(await(m_context->notes().get(old.id)).value().folderId, work.id);
}

void tst_Wiring::folderActions()
{
    openWindow();
    auto* button = m_sidebar->findChild<QToolButton*>(QStringLiteral("newFolderButton"));
    answerName(QStringLiteral("Projects"));
    button->click();
    QTRY_COMPARE(selectedFolderName(), QStringLiteral("Projects"));
    const qint64 projects = m_sidebar->currentFolder().value_or(0);

    // A subfolder: selected, with its parent expanded to show it.
    answerName(QStringLiteral("2026"));
    chooseFromMenu(QStringLiteral("newSubfolderAction"));
    emit m_sidebar->folderMenuRequested(projects, m_sidebar->mapToGlobal(QPoint(10, 10)));
    QTRY_COMPARE(selectedFolderName(), QStringLiteral("2026"));
    const qint64 year = m_sidebar->currentFolder().value_or(0);
    QCOMPARE(await(m_context->folders().listTree()).value().at(2).folder.parentId,
             std::optional<qint64>(projects));

    answerName(QStringLiteral("Archive"));
    chooseFromMenu(QStringLiteral("renameFolderAction"));
    emit m_sidebar->folderMenuRequested(projects, m_sidebar->mapToGlobal(QPoint(10, 10)));
    QTRY_COMPARE(await(m_context->folders().listTree()).value().at(1).folder.name,
                 QStringLiteral("Archive"));
    QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(year)); // still selected

    // Deleting it (after confirming) takes the subfolder too; "Notes" is shown.
    const Note inside = addNote(QStringLiteral("Inside"), year);
    clickDestructive();
    chooseFromMenu(QStringLiteral("deleteFolderAction"));
    emit m_sidebar->folderMenuRequested(projects, m_sidebar->mapToGlobal(QPoint(10, 10)));
    QTRY_COMPARE(m_sidebar->currentFolder(), std::optional<qint64>(m_notesFolder));
    QCOMPARE(await(m_context->folders().listTree()).value().size(), 1);
    QVERIFY(await(m_context->notes().get(inside.id)).value().isTrashed());
}

// New Folder asks in the alert style: "New Folder" preselected, Create the
// default (Enter), disabled while the name is blank; Escape creates nothing.
void tst_Wiring::folderNameDialog()
{
    openWindow();
    whenOpen<AlertDialog>([](AlertDialog* dialog) {
        QCOMPARE(dialog->objectName(), QStringLiteral("folderNameDialog"));
        auto* field = dialog->findChild<QLineEdit*>(QStringLiteral("alertField"));
        QPushButton* create = alertButton(dialog, QStringLiteral("accept"));
        QPushButton* cancel = alertButton(dialog, QStringLiteral("cancel"));
        if (field == nullptr || create == nullptr || cancel == nullptr) {
            QFAIL("name field, Create and Cancel expected");
        }
        QCOMPARE(field->text(), QStringLiteral("New Folder"));
        QCOMPARE(field->selectedText(), QStringLiteral("New Folder"));
        QCOMPARE(create->text(), QStringLiteral("Create"));
        QVERIFY(create->isDefault());
        QVERIFY(cancel->x() < create->x());

        field->setText(QStringLiteral("   "));
        QVERIFY(!create->isEnabled());
        field->setText(QStringLiteral("Ideas"));
        QVERIFY(create->isEnabled());
        QTest::keyClick(dialog, Qt::Key_Escape);
    });
    m_sidebar->findChild<QToolButton*>(QStringLiteral("newFolderButton"))->click();
    QTest::qWait(50);
    QCOMPARE(await(m_context->folders().listTree()).value().size(), 1); // only "Notes"
}

void tst_Wiring::defaultFolderCannotBeRenamedOrDeleted()
{
    openWindow();
    whenOpen<QMenu>([](QMenu* menu) {
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("newSubfolderAction"))->isEnabled());
        QVERIFY(!menu->findChild<QAction*>(QStringLiteral("renameFolderAction"))->isEnabled());
        QVERIFY(!menu->findChild<QAction*>(QStringLiteral("deleteFolderAction"))->isEnabled());
        menu->close();
    });
    emit m_sidebar->folderMenuRequested(m_notesFolder, m_sidebar->mapToGlobal(QPoint(10, 10)));
}

void tst_Wiring::sortIsSavedInSettings()
{
    addNote(QStringLiteral("banana"));
    addNote(QStringLiteral("Apple"));
    addNote(QStringLiteral("cherry"));
    openWindow();
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("cherry"), QStringLiteral("Apple"),
                                              QStringLiteral("banana")}));

    m_window->findChild<QAction*>(QStringLiteral("sort_title"))->trigger();
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("Apple"), QStringLiteral("banana"),
                                              QStringLiteral("cherry")}));
    QCOMPARE(m_context->settings().value(QStringLiteral("noteList/sort")).toString(),
             QStringLiteral("title"));

    closeWindow();
    openWindow();
    QVERIFY(m_window->findChild<QAction*>(QStringLiteral("sort_title"))->isChecked());
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("Apple"), QStringLiteral("banana"),
                                              QStringLiteral("cherry")}));
}

// TC-UI-024
void tst_Wiring::restartKeepsEverything()
{
    openWindow();
    answerName(QStringLiteral("Trips"));
    m_window->findChild<QAction*>(QStringLiteral("newFolderAction"))->trigger();
    QTRY_COMPARE(selectedFolderName(), QStringLiteral("Trips"));
    const qint64 trips = m_sidebar->currentFolder().value_or(0);
    // A desktop reactivates the window when the dialog closes; offscreen doesn't.
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));

    for (const QString& text : {QStringLiteral("Paris\nLouvre"), QStringLiteral("Rome\nForum"),
                                QStringLiteral("Oslo\nFjords")}) {
        notes::test::fakeClockOf(*m_context).advance(1min);
        const int before = static_cast<int>(listedTitles().size());
        QTest::keySequence(m_window.get(), QKeySequence(Qt::CTRL | Qt::Key_N));
        QTRY_COMPARE(listedTitles().size(), before + 1);
        QTRY_VERIFY(m_editor->textEdit()->hasFocus());
        typeText(m_editor->textEdit(), text);
    }
    // The last note is still unsaved (autosave waits 500 ms): pinning another
    // switches notes, which saves it.
    QTRY_COMPARE(listedTitles().size(), 3);
    const qint64 rome = m_list->model()
                            ->match(m_list->model()->index(0, 0), Qt::DisplayRole,
                                    QStringLiteral("Rome"), 1, Qt::MatchExactly)
                            .value(0)
                            .data(roles::kNoteIdRole)
                            .toLongLong();
    QVERIFY(m_list->selectNote(rome));
    chooseFromMenu(QStringLiteral("pinNoteAction"));
    openNoteMenu(rome);
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("Rome"), QStringLiteral("Oslo"),
                                              QStringLiteral("Paris")}));

    // Quit as Application does: window, then the context (which drains saves).
    closeWindow();
    m_context = nullptr;
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    openWindow();
    QTRY_VERIFY(m_sidebar->selectFolder(trips));
    QTRY_COMPARE(listedTitles(), (QStringList{QStringLiteral("Rome"), QStringLiteral("Oslo"),
                                              QStringLiteral("Paris")}));
    QVERIFY(listIndex(rome).data(roles::kPinnedRole).toBool());
    QCOMPARE(m_list->model()->index(0, 0).data().toString(), QStringLiteral("Pinned"));
    const QStringList bodies = [&] {
        QStringList result;
        const auto listed = await(m_context->notes().list(trips, notes::storage::NoteSort::Title));
        for (const auto& note : listed.value()) {
            result.append(await(m_context->notes().get(note.id)).value().bodyText);
        }
        return result;
    }();
    // Title order, pinned first.
    QCOMPARE(bodies, (QStringList{QStringLiteral("Rome\nForum"), QStringLiteral("Oslo\nFjords"),
                                  QStringLiteral("Paris\nLouvre")}));
}

QTEST_MAIN(tst_Wiring)
#include "tst_wiring.moc"
