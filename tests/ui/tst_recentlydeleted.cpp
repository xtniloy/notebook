// Recently Deleted in the window (task 2.10): deleting moves a note there
// (the first time after a notice saying so), it counts down and is read-only, Recover brings it
// back, Delete Permanently asks first, and old notes are purged at startup.

#include "TestHelpers.h"
#include "models/Note.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/NoteService.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/NoteTextEdit.h"
#include "ui/Sidebar.h"
#include "ui/dialogs/AlertDialog.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QTimer>

#include <chrono>
#include <functional>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Note;
using notes::test::await;
using notes::ui::EditorView;
using notes::ui::MainWindow;
using notes::ui::NoteListView;
using notes::ui::Sidebar;
namespace roles = notes::models;

namespace {

// Runs `handle` on the next modal dialog or popup menu, once it's open.
void whenModalOpens(const std::function<void(QWidget*)>& handle)
{
    QTimer::singleShot(0, qApp, [handle] {
        QWidget* modal = QApplication::activeModalWidget();
        if (modal == nullptr) {
            modal = QApplication::activePopupWidget();
        }
        QVERIFY(modal != nullptr);
        handle(modal);
    });
}

} // namespace

class tst_RecentlyDeleted : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void deleteMovesNoteToRecentlyDeleted();
    void firstDeleteShowsNoticeOnce();
    void recoverFromMenu();
    void deletePermanentlyAsksFirst();
    void oldNotesPurgedAtStartup();

private:
    void openWindow();
    void showRecentlyDeleted();
    [[nodiscard]] QStringList listedTitles() const;

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    NoteListView* m_list = nullptr;
    Sidebar* m_sidebar = nullptr;
    EditorView* m_editor = nullptr;
    Note m_keep;
    Note m_gone;
};

void tst_RecentlyDeleted::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    m_keep = await(m_context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Keep")))
                 .value();
    notes::test::fakeClockOf(*m_context).advance(1min);
    m_gone = await(m_context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Gone")))
                 .value();
    // Past the first-delete notice, except where a test clears this.
    m_context->settings().setValue(MainWindow::kTrashNoticeShownKey, true);
}

void tst_RecentlyDeleted::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_RecentlyDeleted::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    m_sidebar = m_window->findChild<Sidebar*>(QStringLiteral("sidebarPane"));
    m_editor = m_window->findChild<EditorView*>(QStringLiteral("editorPane"));
    QVERIFY(m_list && m_sidebar && m_editor);
    QTRY_VERIFY(m_editor->hasNote());
}

void tst_RecentlyDeleted::showRecentlyDeleted()
{
    QTRY_VERIFY(m_sidebar->selectFolder(roles::kRecentlyDeletedFolderId));
}

QStringList tst_RecentlyDeleted::listedTitles() const
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

void tst_RecentlyDeleted::deleteMovesNoteToRecentlyDeleted()
{
    openWindow();
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(m_gone.id));
    m_window->findChild<QAction*>(QStringLiteral("deleteNoteAction"))->trigger();
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("Keep")});
    QVERIFY(await(m_context->notes().get(m_gone.id)).value().isTrashed());

    showRecentlyDeleted();
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("Gone")});
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(m_gone.id));
    QCOMPARE(m_list->currentIndex().data(roles::kTimeTextRole).toString(),
             QStringLiteral("30 days remaining"));
    // Shown, but read-only until recovered.
    QTRY_COMPARE(m_editor->textEdit()->toPlainText(), QStringLiteral("Gone"));
    QVERIFY(m_editor->textEdit()->isReadOnly());
}

// As in macOS Notes: the first delete says where deleted notes go.
void tst_RecentlyDeleted::firstDeleteShowsNoticeOnce()
{
    QSettings& settings = m_context->settings();
    settings.remove(MainWindow::kTrashNoticeShownKey);
    openWindow();
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(m_gone.id));
    auto* remove = m_window->findChild<QAction*>(QStringLiteral("deleteNoteAction"));

    // Escape deletes nothing, and the notice comes again.
    whenModalOpens([](QWidget* dialog) {
        QCOMPARE(dialog->objectName(), QStringLiteral("trashNoticeDialog"));
        QTest::keyClick(dialog, Qt::Key_Escape);
    });
    remove->trigger();
    QTest::qWait(50);
    QCOMPARE(listedTitles(), (QStringList{QStringLiteral("Gone"), QStringLiteral("Keep")}));
    QVERIFY(!settings.value(MainWindow::kTrashNoticeShownKey).toBool());

    // OK moves the note.
    whenModalOpens([](QWidget* dialog) {
        QCOMPARE(dialog->objectName(), QStringLiteral("trashNoticeDialog"));
        QVERIFY(qobject_cast<notes::ui::AlertDialog*>(dialog));
        const auto buttons = dialog->findChildren<QPushButton*>();
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons.first()->text(), QStringLiteral("OK"));
        buttons.first()->click();
    });
    remove->trigger();
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("Keep")});
    QVERIFY(await(m_context->notes().get(m_gone.id)).value().isTrashed());
    QVERIFY(settings.value(MainWindow::kTrashNoticeShownKey).toBool());

    // Never again: the next delete happens at once.
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(m_keep.id));
    remove->trigger();
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    QTRY_VERIFY(listedTitles().isEmpty());
}

void tst_RecentlyDeleted::recoverFromMenu()
{
    QVERIFY(await(m_context->notes().trash(m_gone.id)));
    openWindow();
    showRecentlyDeleted();
    QTRY_COMPARE(listedTitles(), QStringList{QStringLiteral("Gone")});

    whenModalOpens([](QWidget* popup) {
        auto* menu = qobject_cast<QMenu*>(popup);
        QVERIFY(menu);
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("deletePermanentlyAction")));
        auto* recover = menu->findChild<QAction*>(QStringLiteral("recoverNoteAction"));
        QVERIFY(recover);
        menu->close();
        recover->trigger();
    });
    const QRect row = m_list->visualRect(m_list->currentIndex());
    emit m_list->customContextMenuRequested(row.center());

    QTRY_VERIFY(listedTitles().isEmpty());
    const Note restored = await(m_context->notes().get(m_gone.id)).value();
    QVERIFY(!restored.isTrashed());
    QCOMPARE(restored.folderId, m_gone.folderId);
}

void tst_RecentlyDeleted::deletePermanentlyAsksFirst()
{
    QVERIFY(await(m_context->notes().trash(m_gone.id)));
    openWindow();
    showRecentlyDeleted();
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(m_gone.id));
    auto* remove = m_window->findChild<QAction*>(QStringLiteral("deleteNoteAction"));

    // The same alert as the first-delete notice: Cancel (the default, so
    // Enter is safe), then Delete.
    const auto buttonOf = [](QWidget* dialog, const QString& role) -> QPushButton* {
        for (QPushButton* button : dialog->findChildren<QPushButton*>()) {
            if (button->property("alertRole").toString() == role) {
                return button;
            }
        }
        return nullptr;
    };

    // Cancel keeps it.
    whenModalOpens([&](QWidget* dialog) {
        QCOMPARE(dialog->objectName(), QStringLiteral("deletePermanentlyDialog"));
        QVERIFY(qobject_cast<notes::ui::AlertDialog*>(dialog));
        QPushButton* cancel = buttonOf(dialog, QStringLiteral("cancel"));
        QPushButton* accept = buttonOf(dialog, QStringLiteral("accept"));
        if (cancel == nullptr || accept == nullptr) {
            QFAIL("Cancel and Delete buttons expected");
        }
        QCOMPARE(accept->text(), QStringLiteral("Delete"));
        QVERIFY(cancel->isDefault());
        QVERIFY(cancel->x() < accept->x());
        cancel->click();
    });
    remove->trigger();
    QTest::qWait(50);
    QCOMPARE(listedTitles(), QStringList{QStringLiteral("Gone")});
    QVERIFY(!await(m_context->notes().get(m_gone.id)).value().deletedAt);

    // Delete purges it.
    whenModalOpens([&](QWidget* dialog) {
        QPushButton* accept = buttonOf(dialog, QStringLiteral("accept"));
        if (accept == nullptr) {
            QFAIL("Delete button expected");
        }
        accept->click();
    });
    remove->trigger();
    QTRY_VERIFY(listedTitles().isEmpty());
    QVERIFY(await(m_context->notes().get(m_gone.id)).value().deletedAt.has_value());
    QTRY_VERIFY(!m_editor->hasNote());
}

// TC-STO-031 end to end: purged when the app starts.
void tst_RecentlyDeleted::oldNotesPurgedAtStartup()
{
    // Trashed 31 days before the next start (whose clock is at the default).
    notes::test::fakeClockOf(*m_context)
        .setNowMs(notes::test::FakeClock::kDefaultStartMs -
                  std::chrono::milliseconds(31 * 24h).count());
    QVERIFY(await(m_context->notes().trash(m_gone.id)));
    m_context = nullptr;

    m_context = notes::test::makeTestContext(m_dir->dirPath());
    QTRY_VERIFY(await(m_context->notes().get(m_gone.id)).value().deletedAt.has_value());
    QVERIFY(!await(m_context->notes().get(m_keep.id)).value().deletedAt);
}

QTEST_MAIN(tst_RecentlyDeleted)
#include "tst_recentlydeleted.moc"
