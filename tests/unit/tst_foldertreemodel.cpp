// FolderTreeModel (task 2.8): TC-STO-022 model contract while folders and
// notes change, the sidebar layout, and count badges. Runs against a real
// DbThread and a migrated database in a temp dir.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/FolderTreeModel.h"
#include "models/SidebarRoles.h"
#include "services/FolderService.h"
#include "services/NoteService.h"
#include "storage/DbThread.h"
#include "storage/Migrator.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using notes::models::Folder;
using notes::models::FolderTreeModel;
using notes::models::Note;
using notes::models::SidebarItemKind;
using notes::services::FolderService;
using notes::services::NoteService;
using notes::storage::Database;
using notes::storage::DbStatus;
using notes::test::await;
namespace roles = notes::models;

namespace {

// "On My Computer [All Notes 3, Notes 1, Work 2 [Projects 0]], Recently Deleted 1":
// names, count badges, and children in brackets.
// NOLINTNEXTLINE(misc-no-recursion): a test helper on a tiny tree
QString outline(const QAbstractItemModel& model, const QModelIndex& parent = {})
{
    QStringList parts;
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        QString part = index.data().toString();
        if (const QVariant count = index.data(roles::kCountRole); count.isValid()) {
            part += QLatin1Char(' ') + QString::number(count.toInt());
        }
        if (model.rowCount(index) > 0) {
            part += QStringLiteral(" [") + outline(model, index) + QLatin1Char(']');
        }
        parts.append(part);
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

class tst_FolderTreeModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsFoldersUnderAccount();
    void rolesAndFlags();
    void modelContractWhileThingsChange();
    void noteChangesOnlyUpdateCounts();

private:
    // Waits until every reload posted so far has been applied.
    void settle();

    std::unique_ptr<notes::test::TempDb> m_dir;
    notes::test::FakeClock m_clock;
    std::unique_ptr<notes::storage::DbThread> m_db;
    std::unique_ptr<NoteService> m_notes;
    std::unique_ptr<FolderService> m_folders;
    qint64 m_default = 0;
};

void tst_FolderTreeModel::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    m_db = std::make_unique<notes::storage::DbThread>(m_dir->path());
    QVERIFY(await(m_db->post([](Database& db) { return notes::storage::Migrator().migrate(db); })));
    m_notes = std::make_unique<NoteService>(*m_db, m_clock);
    m_folders = std::make_unique<FolderService>(*m_db, m_clock);
    m_default = await(m_folders->ensureDefaultFolder()).value().id;
}

void tst_FolderTreeModel::cleanup()
{
    m_notes = nullptr;
    m_folders = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

void tst_FolderTreeModel::settle()
{
    // Jobs finish in order: once a job posted now is back, every reload posted
    // earlier is done, and its result waits in the event queue.
    QVERIFY(await(m_db->post([](Database&) { return DbStatus(); })));
    QCoreApplication::processEvents();
}

void tst_FolderTreeModel::showsFoldersUnderAccount()
{
    const qint64 work = await(m_folders->create(QStringLiteral("Work"))).value().id;
    QVERIFY(await(m_folders->create(QStringLiteral("Projects"), work)));
    QVERIFY(await(m_notes->create(work, {}, QStringLiteral("Plan"))));
    QVERIFY(await(m_notes->create(m_default, {}, QStringLiteral("Idea"))));
    const Note gone = await(m_notes->create(work, {}, QStringLiteral("Old"))).value();
    QVERIFY(await(m_notes->trash(gone.id)));

    FolderTreeModel model(*m_folders, *m_notes);
    // Before the first answer: the fixed rows, so the sidebar is never empty.
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 0], Recently Deleted 0"));
    settle();
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 2, Notes 1, Work 1 "
                                            "[Projects 0]], Recently Deleted 1"));
}

void tst_FolderTreeModel::rolesAndFlags()
{
    FolderTreeModel model(*m_folders, *m_notes);
    settle();
    const QModelIndex account = model.index(0, 0);
    QCOMPARE(account.data(roles::kKindRole).toInt(),
             static_cast<int>(SidebarItemKind::AccountHeader));
    QVERIFY(!account.data(roles::kFolderIdRole).isValid());
    QVERIFY(!account.data(roles::kCountRole).isValid());
    QVERIFY(!(model.flags(account) & Qt::ItemIsSelectable));

    const QModelIndex allNotes = model.index(0, 0, account);
    QCOMPARE(allNotes.data(roles::kKindRole).toInt(), static_cast<int>(SidebarItemKind::AllNotes));
    QCOMPARE(allNotes.data(roles::kFolderIdRole).toLongLong(), roles::kAllNotesFolderId);

    const QModelIndex notes = model.index(1, 0, account);
    QCOMPARE(notes.data(roles::kKindRole).toInt(), static_cast<int>(SidebarItemKind::Folder));
    QCOMPARE(notes.data(roles::kFolderIdRole).toLongLong(), m_default);
    QVERIFY(model.flags(notes) & Qt::ItemIsSelectable);

    const QModelIndex trash = model.index(1, 0);
    QCOMPARE(trash.data(roles::kKindRole).toInt(),
             static_cast<int>(SidebarItemKind::RecentlyDeleted));
    QCOMPARE(trash.data(roles::kFolderIdRole).toLongLong(), roles::kRecentlyDeletedFolderId);
    QCOMPARE(model.parent(notes), account);
    QCOMPARE(model.parent(account), QModelIndex());
}

// TC-STO-022 (sidebar half; tst_notelistmodel has the note list's).
void tst_FolderTreeModel::modelContractWhileThingsChange()
{
    FolderTreeModel model(*m_folders, *m_notes);
    const QAbstractItemModelTester tester(&model,
                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
    settle();

    const Folder work = await(m_folders->create(QStringLiteral("Work"))).value();
    const Folder projects = await(m_folders->create(QStringLiteral("Projects"), work.id)).value();
    const Folder home = await(m_folders->create(QStringLiteral("Home"))).value();
    const Note note = await(m_notes->create(projects.id, {}, QStringLiteral("Spec"))).value();
    settle();
    // New folders go last among their siblings.
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 1, Notes 0, Work 0 "
                                            "[Projects 1], Home 0], Recently Deleted 0"));

    QVERIFY(await(m_notes->moveToFolder(note.id, home.id)));
    QVERIFY(await(m_folders->rename(home.id, QStringLiteral("Personal"))));
    QVERIFY(await(m_folders->move(projects.id, home.id)));
    QVERIFY(await(m_folders->reorder(std::nullopt, {work.id, m_default, home.id})));
    settle();
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 1, Work 0, Notes 0, "
                                            "Personal 1 [Projects 0]], Recently Deleted 0"));

    // Deleting a folder takes its subfolders, and its notes to Recently Deleted.
    QVERIFY(await(m_folders->remove(home.id)));
    settle();
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 0, Work 0, Notes 0], "
                                            "Recently Deleted 1"));

    QVERIFY(await(m_notes->purge(note.id)));
    settle();
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 0, Work 0, Notes 0], "
                                            "Recently Deleted 0"));
}

void tst_FolderTreeModel::noteChangesOnlyUpdateCounts()
{
    FolderTreeModel model(*m_folders, *m_notes);
    settle();
    const QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    const Note note = await(m_notes->create(m_default, {}, QStringLiteral("Note"))).value();
    QVERIFY(await(m_notes->trash(note.id)));
    settle();
    QCOMPARE(reset.count(), 0);
    QVERIFY(!changed.isEmpty());
    for (const QList<QVariant>& args : std::as_const(changed)) {
        QCOMPARE(args.at(2).value<QList<int>>(), QList<int>{roles::kCountRole});
    }
    QCOMPARE(outline(model), QStringLiteral("On My Computer [All Notes 0, Notes 0], "
                                            "Recently Deleted 1"));
}

QTEST_MAIN(tst_FolderTreeModel)
#include "tst_foldertreemodel.moc"
