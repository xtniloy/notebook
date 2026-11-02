// Drag & drop (task 2.12): TC-UI-025 two notes dropped on a folder move there
// and the counts follow, TC-UI-026 a folder can't go into its own subfolder;
// plus folder nesting and reordering, notes dropped on Recently Deleted, and
// the drop highlight. Real database in a temp dir.

#include "TestHelpers.h"
#include "models/DragMime.h"
#include "models/Folder.h"
#include "models/Note.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"
#include "ui/Sidebar.h"
#include "ui/SidebarDelegate.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QTest>
#include <QTreeView>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Folder;
using notes::models::Note;
using notes::test::await;
using notes::ui::MainWindow;
using notes::ui::NoteListView;
using notes::ui::Sidebar;
namespace roles = notes::models;

class tst_DragDrop : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void noteListDragsUuids();
    void droppingNotesOnFolderMovesThem();
    void droppingNotesOnRecentlyDeletedDeletesThem();
    void notesOnlyDropOntoFoldersAndTrash();
    void folderCannotGoIntoItsOwnSubfolder();
    void foldersNestAndUnnest();
    void foldersReorderBetweenRows();
    void notesFolderOnlyReorders();
    void dropTargetIsHighlighted();

private:
    void openWindow();
    // Waits until every reload posted so far has been applied.
    void settle();
    [[nodiscard]] QModelIndex folderIndex(qint64 folderId) const;
    [[nodiscard]] QModelIndex accountIndex() const;
    [[nodiscard]] int countOf(qint64 folderId) const;
    [[nodiscard]] QStringList topLevelFolders();
    [[nodiscard]] static std::unique_ptr<QMimeData> notesMime(const QList<Note>& notes);
    [[nodiscard]] static std::unique_ptr<QMimeData> folderMime(const Folder& folder);

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<notes::app::AppContext> m_context;
    std::unique_ptr<MainWindow> m_window;
    QAbstractItemModel* m_tree = nullptr;
    QTreeView* m_treeView = nullptr;
    NoteListView* m_list = nullptr;
    qint64 m_notesFolder = 0;
    Folder m_a;
    Folder m_b;
};

void tst_DragDrop::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_context = notes::test::makeTestContext(m_dir->dirPath());
    m_notesFolder = await(m_context->folders().ensureDefaultFolder()).value().id;
    m_a = await(m_context->folders().create(QStringLiteral("A"))).value();
    m_b = await(m_context->folders().create(QStringLiteral("B"))).value();
}

void tst_DragDrop::cleanup()
{
    m_window = nullptr;
    m_context = nullptr;
    m_dir = nullptr;
}

void tst_DragDrop::openWindow()
{
    m_window = std::make_unique<MainWindow>(*m_context);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto* sidebar = m_window->findChild<Sidebar*>(QStringLiteral("sidebarPane"));
    m_treeView = sidebar->findChild<QTreeView*>(QStringLiteral("sidebarTree"));
    m_list = m_window->findChild<NoteListView*>(QStringLiteral("noteListPane"));
    QVERIFY(m_treeView && m_list);
    m_tree = m_treeView->model();
    QTRY_VERIFY(folderIndex(m_b.id).isValid());
    settle();
}

void tst_DragDrop::settle()
{
    QVERIFY(await(m_context->notes().counts()));
    QCoreApplication::processEvents();
}

QModelIndex tst_DragDrop::folderIndex(qint64 folderId) const
{
    return m_tree
        ->match(m_tree->index(0, 0), roles::kFolderIdRole, folderId, 1,
                Qt::MatchExactly | Qt::MatchRecursive)
        .value(0);
}

QModelIndex tst_DragDrop::accountIndex() const
{
    return m_tree->index(0, 0);
}

int tst_DragDrop::countOf(qint64 folderId) const
{
    return folderIndex(folderId).data(roles::kCountRole).toInt();
}

QStringList tst_DragDrop::topLevelFolders()
{
    QStringList names;
    const auto tree = await(m_context->folders().listTree());
    for (const auto& entry : tree.value()) {
        names.append(QString(entry.depth, QLatin1Char('-')) + entry.folder.name);
    }
    return names;
}

std::unique_ptr<QMimeData> tst_DragDrop::notesMime(const QList<Note>& notes)
{
    QStringList uuids;
    for (const Note& note : notes) {
        uuids.append(note.uuid);
    }
    auto data = std::make_unique<QMimeData>();
    data->setData(QString(roles::kNoteUuidsMime), roles::encodeUuids(uuids));
    return data;
}

std::unique_ptr<QMimeData> tst_DragDrop::folderMime(const Folder& folder)
{
    auto data = std::make_unique<QMimeData>();
    data->setData(QString(roles::kFolderUuidsMime), roles::encodeUuids({folder.uuid}));
    return data;
}

void tst_DragDrop::noteListDragsUuids()
{
    const Note note = await(m_context->notes().create(m_a.id, {}, QStringLiteral("x"))).value();
    openWindow();
    const QAbstractItemModel* model = m_list->model();
    QVERIFY(model->mimeTypes().contains(QString(roles::kNoteUuidsMime)));
    QVERIFY(m_list->dragEnabled());

    QVERIFY(m_window->findChild<Sidebar*>()->selectFolder(m_a.id));
    QTRY_COMPARE(m_list->currentNote(), std::optional<qint64>(note.id));
    const QModelIndex row = m_list->currentIndex();
    QVERIFY(model->flags(row) & Qt::ItemIsDragEnabled);
    QVERIFY(!(model->flags(model->index(0, 0)) & Qt::ItemIsDragEnabled)); // the header
    const std::unique_ptr<QMimeData> data(model->mimeData({row}));
    QVERIFY(data);
    QCOMPARE(roles::decodeUuids(data->data(QString(roles::kNoteUuidsMime))),
             QStringList{note.uuid});
}

// TC-UI-025
void tst_DragDrop::droppingNotesOnFolderMovesThem()
{
    const Note one = await(m_context->notes().create(m_a.id, {}, QStringLiteral("One"))).value();
    const Note two = await(m_context->notes().create(m_a.id, {}, QStringLiteral("Two"))).value();
    openWindow();
    QCOMPARE(countOf(m_a.id), 2);
    QCOMPARE(countOf(m_b.id), 0);

    const auto data = notesMime({one, two});
    QVERIFY(m_tree->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, folderIndex(m_b.id)));
    QVERIFY(m_tree->dropMimeData(data.get(), Qt::MoveAction, -1, -1, folderIndex(m_b.id)));

    QTRY_COMPARE(countOf(m_b.id), 2);
    QCOMPARE(countOf(m_a.id), 0);
    QCOMPARE(await(m_context->notes().get(one.id)).value().folderId, m_b.id);
    QCOMPARE(await(m_context->notes().get(two.id)).value().folderId, m_b.id);
}

void tst_DragDrop::droppingNotesOnRecentlyDeletedDeletesThem()
{
    const Note note = await(m_context->notes().create(m_a.id, {}, QStringLiteral("x"))).value();
    openWindow();
    const QModelIndex trash = folderIndex(roles::kRecentlyDeletedFolderId);
    const auto data = notesMime({note});
    QVERIFY(m_tree->dropMimeData(data.get(), Qt::MoveAction, -1, -1, trash));
    QTRY_COMPARE(trash.data(roles::kCountRole).toInt(), 1);
    QVERIFY(await(m_context->notes().get(note.id)).value().isTrashed());
}

void tst_DragDrop::notesOnlyDropOntoFoldersAndTrash()
{
    const Note note = await(m_context->notes().create(m_a.id, {}, QStringLiteral("x"))).value();
    openWindow();
    const auto data = notesMime({note});
    QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::MoveAction, -1, -1,
                                     folderIndex(roles::kAllNotesFolderId)));
    QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, accountIndex()));
    // Between rows means reordering, which notes can't do.
    QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::MoveAction, 1, 0, accountIndex()));
    QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::CopyAction, -1, -1, folderIndex(m_b.id)));
}

// TC-UI-026
void tst_DragDrop::folderCannotGoIntoItsOwnSubfolder()
{
    const Folder child =
        await(m_context->folders().create(QStringLiteral("Child"), m_a.id)).value();
    const Folder grandchild =
        await(m_context->folders().create(QStringLiteral("Grandchild"), child.id)).value();
    openWindow();
    const QStringList before = topLevelFolders();

    const auto data = folderMime(m_a);
    for (const qint64 target : {m_a.id, child.id, grandchild.id}) {
        QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, folderIndex(target)));
        QVERIFY(!m_tree->dropMimeData(data.get(), Qt::MoveAction, -1, -1, folderIndex(target)));
        QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::MoveAction, 0, 0, folderIndex(target)));
    }
    settle();
    QCOMPARE(topLevelFolders(), before);
}

void tst_DragDrop::foldersNestAndUnnest()
{
    openWindow();
    // B onto A: B becomes A's subfolder.
    const auto b = folderMime(m_b);
    QVERIFY(m_tree->dropMimeData(b.get(), Qt::MoveAction, -1, -1, folderIndex(m_a.id)));
    QTRY_COMPARE(topLevelFolders(),
                 (QStringList{QStringLiteral("Notes"), QStringLiteral("A"), QStringLiteral("-B")}));
    QTRY_VERIFY(folderIndex(m_b.id).parent() == folderIndex(m_a.id));

    // Onto "On My Computer": top level again.
    QVERIFY(m_tree->dropMimeData(b.get(), Qt::MoveAction, -1, -1, accountIndex()));
    QTRY_COMPARE(topLevelFolders(),
                 (QStringList{QStringLiteral("Notes"), QStringLiteral("A"), QStringLiteral("B")}));
}

void tst_DragDrop::foldersReorderBetweenRows()
{
    openWindow();
    // On My Computer: All Notes (row 0), Notes (1), A (2), B (3). B before Notes:
    const auto b = folderMime(m_b);
    QVERIFY(m_tree->canDropMimeData(b.get(), Qt::MoveAction, 1, 0, accountIndex()));
    QVERIFY(m_tree->dropMimeData(b.get(), Qt::MoveAction, 1, 0, accountIndex()));
    QTRY_COMPARE(topLevelFolders(),
                 (QStringList{QStringLiteral("B"), QStringLiteral("Notes"), QStringLiteral("A")}));

    // B to the end (after A, the last row): its old place closes up.
    QTRY_VERIFY(folderIndex(m_b.id).row() == 1);
    QVERIFY(m_tree->dropMimeData(b.get(), Qt::MoveAction, 4, 0, accountIndex()));
    QTRY_COMPARE(topLevelFolders(),
                 (QStringList{QStringLiteral("Notes"), QStringLiteral("A"), QStringLiteral("B")}));

    // Into A at a position: nested and ordered in one drop.
    const Folder c = await(m_context->folders().create(QStringLiteral("C"), m_a.id)).value();
    QTRY_VERIFY(folderIndex(c.id).isValid());
    QVERIFY(m_tree->dropMimeData(b.get(), Qt::MoveAction, 0, 0, folderIndex(m_a.id)));
    QTRY_COMPARE(topLevelFolders(), (QStringList{QStringLiteral("Notes"), QStringLiteral("A"),
                                                 QStringLiteral("-B"), QStringLiteral("-C")}));
}

void tst_DragDrop::notesFolderOnlyReorders()
{
    openWindow();
    const Folder notes = await(m_context->folders().ensureDefaultFolder()).value();
    const auto data = folderMime(notes);
    QVERIFY(m_tree->flags(folderIndex(m_notesFolder)) & Qt::ItemIsDragEnabled);
    QVERIFY(!m_tree->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, folderIndex(m_a.id)));
    QVERIFY(m_tree->dropMimeData(data.get(), Qt::MoveAction, 4, 0, accountIndex()));
    QTRY_COMPARE(topLevelFolders(),
                 (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("Notes")}));
}

void tst_DragDrop::dropTargetIsHighlighted()
{
    const Note note = await(m_context->notes().create(m_a.id, {}, QStringLiteral("x"))).value();
    openWindow();
    const auto data = notesMime({note});
    const QRect target = m_treeView->visualRect(folderIndex(m_b.id));
    QVERIFY(target.isValid());
    const auto dropTarget = [&] {
        return m_treeView->property(notes::ui::SidebarDelegate::kDropTargetProperty)
            .value<QPersistentModelIndex>();
    };

    QDragEnterEvent enter(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, {});
    QCoreApplication::sendEvent(m_treeView->viewport(), &enter);
    QDragMoveEvent move(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, {});
    QCoreApplication::sendEvent(m_treeView->viewport(), &move);
    QVERIFY(move.isAccepted());
    QCOMPARE(QModelIndex(dropTarget()), folderIndex(m_b.id));

    // Over All Notes, which takes no notes: no highlight.
    const QRect allNotes = m_treeView->visualRect(folderIndex(roles::kAllNotesFolderId));
    QDragMoveEvent refused(allNotes.center(), Qt::MoveAction, data.get(), Qt::LeftButton, {});
    QCoreApplication::sendEvent(m_treeView->viewport(), &refused);
    QVERIFY(!dropTarget().isValid());

    // The drop itself, through the view, then the highlight goes.
    QDragMoveEvent back(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, {});
    QCoreApplication::sendEvent(m_treeView->viewport(), &back);
    QDropEvent drop(target.center(), Qt::MoveAction, data.get(), Qt::LeftButton, {});
    QCoreApplication::sendEvent(m_treeView->viewport(), &drop);
    QVERIFY(!dropTarget().isValid());
    QTRY_COMPARE(countOf(m_b.id), 1);
    QCOMPARE(await(m_context->notes().get(note.id)).value().folderId, m_b.id);
}

QTEST_MAIN(tst_DragDrop)
#include "tst_dragdrop.moc"
