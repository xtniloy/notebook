// FakeSidebarModel (task 1.5): TC-UI-013 QAbstractItemModelTester, plus the
// shape of the sample tree the sidebar relies on.

#include "models/FakeSidebarModel.h"

#include <QAbstractItemModelTester>
#include <QSet>
#include <QTest>

#include <functional>

using notes::models::FakeSidebarModel;
using notes::models::SidebarItemKind;
namespace roles = notes::models;

namespace {

SidebarItemKind kindOf(const QModelIndex& index)
{
    return static_cast<SidebarItemKind>(index.data(roles::kKindRole).toInt());
}

QModelIndex find(const QAbstractItemModel& model, const QString& name)
{
    return model
        .match(model.index(0, 0), Qt::DisplayRole, name, 1, Qt::MatchExactly | Qt::MatchRecursive)
        .value(0);
}

// Every row, parents before their children.
void forEachRow(const QAbstractItemModel& model,
                const std::function<void(const QModelIndex&)>& visit)
{
    QList<QModelIndex> pending{QModelIndex()};
    while (!pending.isEmpty()) {
        const QModelIndex parent = pending.takeLast();
        for (int row = model.rowCount(parent) - 1; row >= 0; --row) {
            pending.append(model.index(row, 0, parent));
        }
        if (parent.isValid()) {
            visit(parent);
        }
    }
}

} // namespace

class tst_FakeSidebarModel : public QObject
{
    Q_OBJECT

private slots:
    void passesModelTester();
    void sectionsInPlanOrder();
    void foldersNestThreeLevels();
    void folderIdsAreUniqueAndPositive();
    void onlyNoteSetsAreSelectable();
    void allNotesCountsEveryFolder();
};

// TC-UI-013
void tst_FakeSidebarModel::passesModelTester()
{
    FakeSidebarModel model;
    const QAbstractItemModelTester tester(&model,
                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.reload();
    QVERIFY(model.rowCount() > 0);
}

// Plan §3.3: account section (All Notes, Notes, user folders), Tags, Recently Deleted.
void tst_FakeSidebarModel::sectionsInPlanOrder()
{
    const FakeSidebarModel model;
    QCOMPARE(model.rowCount(), 3);
    const QModelIndex account = model.index(0, 0);
    QCOMPARE(kindOf(account), SidebarItemKind::AccountHeader);
    QCOMPARE(account.data().toString(), QStringLiteral("On My Computer"));
    QCOMPARE(kindOf(model.index(1, 0)), SidebarItemKind::SectionHeader);
    QCOMPARE(model.index(1, 0).data().toString(), QStringLiteral("Tags"));
    QVERIFY(model.rowCount(model.index(1, 0)) > 0);
    QCOMPARE(kindOf(model.index(2, 0)), SidebarItemKind::RecentlyDeleted);
    QCOMPARE(model.index(2, 0).data(roles::kFolderIdRole).toLongLong(),
             roles::kRecentlyDeletedFolderId);

    const QModelIndex allNotes = model.index(0, 0, account);
    QCOMPARE(kindOf(allNotes), SidebarItemKind::AllNotes);
    QCOMPARE(allNotes.data(roles::kFolderIdRole).toLongLong(), roles::kAllNotesFolderId);
    const QModelIndex notes = model.index(1, 0, account);
    QCOMPARE(kindOf(notes), SidebarItemKind::Folder);
    QCOMPARE(notes.data().toString(), QStringLiteral("Notes"));
    QVERIFY(model.rowCount(account) > 3); // plus user folders
}

void tst_FakeSidebarModel::foldersNestThreeLevels()
{
    const FakeSidebarModel model;
    const QModelIndex archive = find(model, QStringLiteral("Archive"));
    QVERIFY(archive.isValid());
    QCOMPARE(archive.parent().data().toString(), QStringLiteral("Projects"));
    QCOMPARE(archive.parent().parent().data().toString(), QStringLiteral("Work"));
    QCOMPARE(kindOf(archive.parent().parent().parent()), SidebarItemKind::AccountHeader);
}

void tst_FakeSidebarModel::folderIdsAreUniqueAndPositive()
{
    const FakeSidebarModel model;
    QSet<qint64> ids;
    int folders = 0;
    forEachRow(model, [&](const QModelIndex& index) {
        if (kindOf(index) == SidebarItemKind::Folder) {
            ++folders;
            const qint64 id = index.data(roles::kFolderIdRole).toLongLong();
            QVERIFY(id > 0);
            ids.insert(id);
            QVERIFY(index.data(roles::kCountRole).isValid());
        }
    });
    QVERIFY(folders >= 5);
    QCOMPARE(ids.size(), folders);
}

void tst_FakeSidebarModel::onlyNoteSetsAreSelectable()
{
    const FakeSidebarModel model;
    forEachRow(model, [](const QModelIndex& index) {
        const bool selectable = (index.flags() & Qt::ItemIsSelectable) != 0;
        const bool hasId = index.data(roles::kFolderIdRole).isValid();
        QVERIFY2(selectable == hasId, qPrintable(index.data().toString()));
        QVERIFY(index.flags() & Qt::ItemIsEnabled);
    });
}

void tst_FakeSidebarModel::allNotesCountsEveryFolder()
{
    const FakeSidebarModel model;
    int total = 0;
    forEachRow(model, [&](const QModelIndex& index) {
        if (kindOf(index) == SidebarItemKind::Folder) {
            total += index.data(roles::kCountRole).toInt();
        }
    });
    const QModelIndex allNotes = find(model, QStringLiteral("All Notes"));
    QCOMPARE(allNotes.data(roles::kCountRole).toInt(), total);
    QVERIFY(!find(model, QStringLiteral("On My Computer")).data(roles::kCountRole).isValid());
}

QTEST_MAIN(tst_FakeSidebarModel)
#include "tst_fakesidebarmodel.moc"
