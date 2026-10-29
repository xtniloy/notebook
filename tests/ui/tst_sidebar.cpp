// Sidebar (task 1.5): TC-UI-011 folder selection signal, TC-UI-012 expand/collapse
// state kept for the session. Runs under QT_QPA_PLATFORM=offscreen.

#include "TestHelpers.h"
#include "models/FakeSidebarModel.h"
#include "ui/MainWindow.h"
#include "ui/Sidebar.h"

#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QTreeView>

#include <memory>
#include <optional>

using notes::models::FakeSidebarModel;
using notes::ui::Sidebar;
namespace roles = notes::models;

class tst_Sidebar : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void selectsNotesFolderInitially();
    void clickingRowEmitsFolderSelected_data();
    void clickingRowEmitsFolderSelected();
    void clickingHeaderKeepsSelection();
    void selectFolderByIdEmitsOnce();
    void nestedFoldersExpandAndCollapse();
    void stateSurvivesModelReset();
    void stateSurvivesModelSwap();
    void newFolderButtonAtBottom();
    void mainWindowHostsSidebar();

private:
    [[nodiscard]] QModelIndex find(const QString& name) const;
    void clickRow(const QString& name);
    [[nodiscard]] bool isExpanded(const QString& name) const;

    std::unique_ptr<FakeSidebarModel> m_model;
    std::unique_ptr<Sidebar> m_sidebar;
    QTreeView* m_tree = nullptr;
};

void tst_Sidebar::init()
{
    m_model = std::make_unique<FakeSidebarModel>();
    m_sidebar = std::make_unique<Sidebar>();
    m_sidebar->setModel(m_model.get());
    m_sidebar->resize(220, 600);
    m_sidebar->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_sidebar.get()));
    m_tree = m_sidebar->findChild<QTreeView*>(QStringLiteral("sidebarTree"));
    QVERIFY(m_tree);
}

void tst_Sidebar::cleanup()
{
    m_tree = nullptr;
    m_sidebar.reset();
    m_model.reset();
}

QModelIndex tst_Sidebar::find(const QString& name) const
{
    const QAbstractItemModel* model = m_tree->model();
    return model
        ->match(model->index(0, 0), Qt::DisplayRole, name, 1, Qt::MatchExactly | Qt::MatchRecursive)
        .value(0);
}

void tst_Sidebar::clickRow(const QString& name)
{
    const QModelIndex index = find(name);
    QVERIFY2(index.isValid(), qPrintable(name));
    const QRect rect = m_tree->visualRect(index);
    QVERIFY2(rect.isValid(), qPrintable(name + QStringLiteral(" is not visible")));
    QTest::mouseClick(m_tree->viewport(), Qt::LeftButton, {}, rect.center());
}

bool tst_Sidebar::isExpanded(const QString& name) const
{
    return m_tree->isExpanded(find(name));
}

void tst_Sidebar::selectsNotesFolderInitially()
{
    QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(1));
    QCOMPARE(m_tree->currentIndex().data().toString(), QStringLiteral("Notes"));
    QVERIFY(m_tree->selectionModel()->isSelected(m_tree->currentIndex()));
    // Section headers start open; nested folders start closed.
    QVERIFY(isExpanded(QStringLiteral("On My Computer")));
    QVERIFY(isExpanded(QStringLiteral("Tags")));
    QVERIFY(!isExpanded(QStringLiteral("Work")));
}

// TC-UI-011
void tst_Sidebar::clickingRowEmitsFolderSelected_data()
{
    QTest::addColumn<QString>("row");
    QTest::addColumn<qint64>("folderId");

    QTest::newRow("user folder") << QStringLiteral("Work") << qint64(2);
    QTest::newRow("All Notes") << QStringLiteral("All Notes") << roles::kAllNotesFolderId;
    QTest::newRow("Recently Deleted")
        << QStringLiteral("Recently Deleted") << roles::kRecentlyDeletedFolderId;
}

void tst_Sidebar::clickingRowEmitsFolderSelected()
{
    QFETCH(QString, row);
    QFETCH(qint64, folderId);

    QSignalSpy spy(m_sidebar.get(), &Sidebar::folderSelected);
    clickRow(row);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toLongLong(), folderId);
    QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(folderId));

    // Clicking the selected row again is not a new selection.
    clickRow(row);
    QCOMPARE(spy.count(), 0);
}

void tst_Sidebar::clickingHeaderKeepsSelection()
{
    const QSignalSpy spy(m_sidebar.get(), &Sidebar::folderSelected);
    for (const QString& row :
         {QStringLiteral("On My Computer"), QStringLiteral("Tags"), QStringLiteral("#work")}) {
        clickRow(row);
        QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(1));
        QCOMPARE(m_tree->selectionModel()->selectedIndexes(),
                 QModelIndexList{find(QStringLiteral("Notes"))});
    }
    QCOMPARE(spy.count(), 0);
}

void tst_Sidebar::selectFolderByIdEmitsOnce()
{
    QSignalSpy spy(m_sidebar.get(), &Sidebar::folderSelected);
    // A nested folder inside a collapsed parent can still be selected.
    QVERIFY(m_sidebar->selectFolder(4)); // Archive
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toLongLong(), qint64(4));
    QVERIFY(m_sidebar->selectFolder(4));
    QCOMPARE(spy.count(), 0);
    QVERIFY(!m_sidebar->selectFolder(999));
    QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(4));
}

// TC-UI-012
void tst_Sidebar::nestedFoldersExpandAndCollapse()
{
    clickRow(QStringLiteral("Work"));
    QVERIFY(!m_tree->visualRect(find(QStringLiteral("Projects"))).isValid());

    QTest::keyClick(m_tree, Qt::Key_Right);
    QVERIFY(isExpanded(QStringLiteral("Work")));
    QVERIFY(m_tree->visualRect(find(QStringLiteral("Projects"))).isValid());

    clickRow(QStringLiteral("Projects"));
    QTest::keyClick(m_tree, Qt::Key_Right);
    QVERIFY(isExpanded(QStringLiteral("Projects")));
    QVERIFY(m_tree->visualRect(find(QStringLiteral("Archive"))).isValid());

    clickRow(QStringLiteral("Work"));
    QTest::keyClick(m_tree, Qt::Key_Left);
    QVERIFY(!isExpanded(QStringLiteral("Work")));
    QVERIFY(!m_tree->visualRect(find(QStringLiteral("Projects"))).isValid());
}

// TC-UI-012: "remembered for the session" means across model reloads.
void tst_Sidebar::stateSurvivesModelReset()
{
    m_tree->expand(find(QStringLiteral("Work")));
    m_tree->expand(find(QStringLiteral("Projects")));
    m_tree->collapse(find(QStringLiteral("Work")));
    m_tree->expand(find(QStringLiteral("Personal")));
    QVERIFY(m_sidebar->selectFolder(7)); // Recipes

    const QSignalSpy spy(m_sidebar.get(), &Sidebar::folderSelected);
    m_model->reload();

    QVERIFY(!isExpanded(QStringLiteral("Work")));
    QVERIFY(isExpanded(QStringLiteral("Projects")));
    QVERIFY(isExpanded(QStringLiteral("Personal")));
    QVERIFY(isExpanded(QStringLiteral("On My Computer")));
    QCOMPARE(m_tree->currentIndex(), find(QStringLiteral("Recipes")));
    QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(7));
    QCOMPARE(spy.count(), 0); // same folder, so no new selection
}

void tst_Sidebar::stateSurvivesModelSwap()
{
    m_tree->expand(find(QStringLiteral("Personal")));
    QVERIFY(m_sidebar->selectFolder(7)); // Recipes

    FakeSidebarModel other;
    m_sidebar->setModel(&other);
    QCOMPARE(m_tree->model(), &other);
    QVERIFY(isExpanded(QStringLiteral("Personal")));
    QCOMPARE(m_sidebar->currentFolder(), std::optional<qint64>(7));
    m_sidebar->setModel(m_model.get());
}

void tst_Sidebar::newFolderButtonAtBottom()
{
    const auto* button = m_sidebar->findChild<QToolButton*>(QStringLiteral("newFolderButton"));
    QVERIFY(button);
    QVERIFY(button->isVisible());
    QVERIFY(!button->isEnabled()); // enabled by the FolderService in task 2.7
    QVERIFY(button->geometry().top() >= m_tree->geometry().bottom());
    QVERIFY(!button->icon().isNull());
}

void tst_Sidebar::mainWindowHostsSidebar()
{
    const notes::test::TempDb dir;
    const auto context = notes::test::makeTestContext(dir.dirPath());
    notes::ui::MainWindow window(*context);
    auto* sidebar = window.findChild<Sidebar*>(QStringLiteral("sidebarPane"));
    QVERIFY(sidebar);
    QCOMPARE(sidebar->currentFolder(), std::optional<qint64>(1));
    const auto* tree = sidebar->findChild<QTreeView*>(QStringLiteral("sidebarTree"));
    QVERIFY(tree && tree->model());
    QCOMPARE(tree->model()->rowCount(), 3);
}

QTEST_MAIN(tst_Sidebar)
#include "tst_sidebar.moc"
