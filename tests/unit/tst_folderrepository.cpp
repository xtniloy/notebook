// FolderRepository (task 2.5): TC-STO-007..012, plus moves, reordering and the
// "every change bumps version / updated_at / sync_state" rule.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/Uuid.h"
#include "storage/Database.h"
#include "storage/FolderRepository.h"
#include "storage/Migrator.h"

#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using notes::models::Folder;
using notes::models::SyncState;
using notes::storage::Database;
using notes::storage::DbError;
using notes::storage::DbResult;
using notes::storage::DbStatus;
using notes::storage::FolderNode;
using notes::storage::FolderRepository;
using Kind = DbError::Kind;

namespace {

// "Work [Projects [Archive]], Personal" style outline of a tree, for comparisons.
// Recursive for readability; test trees are at most 3 levels deep.
QString outline(const std::vector<FolderNode>& nodes) // NOLINT(misc-no-recursion)
{
    QStringList parts;
    for (const FolderNode& node : nodes) {
        parts.append(node.children.empty()
                         ? node.folder.name
                         : QStringLiteral("%1 [%2]").arg(node.folder.name, outline(node.children)));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

class tst_FolderRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void nestedFoldersFormATree();
    void cannotMoveIntoOwnSubtree();
    void validMoves();
    void renameBumpsVersion();
    void everyChangeMarksDirty();
    void deletingFolderTrashesItsNotes();
    void defaultFolderExistsAndIsProtected();
    void names_data();
    void names();
    void missingOrDeletedFoldersAreNotFound();
    void reorderSetsSiblingOrder();
    void reorderNeedsEverySiblingOnce();

private:
    Folder create(const QString& name, std::optional<qint64> parent = {});
    qint64 insertNote(qint64 folderId, const QString& title);
    void markSynced();
    QString tree();

    std::unique_ptr<notes::test::TempDb> m_dir;
    std::unique_ptr<Database> m_db;
    notes::test::FakeClock m_clock;
    std::unique_ptr<FolderRepository> m_repo;
};

void tst_FolderRepository::init()
{
    m_dir = std::make_unique<notes::test::TempDb>();
    DbResult<std::unique_ptr<Database>> db = Database::open(m_dir->path());
    QVERIFY(db);
    m_db = std::move(db).value();
    QVERIFY(notes::storage::Migrator().migrate(*m_db));
    m_clock.setNowMs(notes::test::FakeClock::kDefaultStartMs);
    m_repo = std::make_unique<FolderRepository>(*m_db, m_clock);
}

void tst_FolderRepository::cleanup()
{
    m_repo = nullptr;
    m_db = nullptr;
    m_dir = nullptr;
}

Folder tst_FolderRepository::create(const QString& name, std::optional<qint64> parent)
{
    DbResult<Folder> folder = m_repo->create(name, parent);
    if (!folder) {
        qWarning() << "create failed:" << folder.error().message;
        return {};
    }
    return folder.value();
}

qint64 tst_FolderRepository::insertNote(qint64 folderId, const QString& title)
{
    DbResult<notes::storage::ScopedStatement> insert = m_db->prepare(QStringLiteral(
        "INSERT INTO notes (uuid, folder_id, title, body_text, created_at, updated_at)"
        " VALUES (?, ?, ?, ?, 1, 1)"));
    if (!insert || !insert.value()
                        ->bind(1, notes::models::newUuid())
                        .bind(2, folderId)
                        .bind(3, title)
                        .bind(4, QString::fromLatin1(notes::test::kSecretMarker))
                        .run()) {
        qWarning("insertNote failed");
        return 0;
    }
    return m_db->lastInsertRowId();
}

void tst_FolderRepository::markSynced()
{
    QVERIFY(m_db->exec(QStringLiteral("UPDATE folders SET sync_state = 1;"
                                      "UPDATE notes SET sync_state = 1;")));
}

QString tst_FolderRepository::tree()
{
    DbResult<std::vector<FolderNode>> nodes = m_repo->listTree();
    return nodes ? outline(nodes.value()) : QStringLiteral("ERROR: ") + nodes.error().message;
}

// TC-STO-007
void tst_FolderRepository::nestedFoldersFormATree()
{
    const Folder work = create(QStringLiteral("Work"));
    const Folder projects = create(QStringLiteral("Projects"), work.id);
    const Folder archive = create(QStringLiteral("Archive"), projects.id);
    create(QStringLiteral("Meetings"), work.id);
    create(QStringLiteral("Personal"));

    QCOMPARE(tree(), QStringLiteral("Work [Projects [Archive], Meetings], Personal"));

    const std::vector<FolderNode> nodes = m_repo->listTree().value();
    QCOMPARE(nodes.at(0).children.at(0).children.at(0).folder.id, archive.id);
    QCOMPARE(nodes.at(0).children.at(0).children.at(0).folder.parentId, projects.id);
    QVERIFY(!nodes.at(0).folder.parentId.has_value());

    // New folders: fresh UUID, the clock's time, version 1, dirty.
    QVERIFY(notes::models::parseUuid(archive.uuid).has_value());
    QVERIFY(archive.uuid != projects.uuid);
    QCOMPARE(archive.createdAt, m_clock.nowMs());
    QCOMPARE(archive.updatedAt, m_clock.nowMs());
    QCOMPARE(archive.version, 1);
    QCOMPARE(archive.syncState, SyncState::Dirty);
}

// TC-STO-008
void tst_FolderRepository::cannotMoveIntoOwnSubtree()
{
    const Folder a = create(QStringLiteral("A"));
    const Folder b = create(QStringLiteral("B"), a.id);
    const Folder c = create(QStringLiteral("C"), b.id);
    const QString before = tree();

    for (const qint64 target : {b.id, c.id, a.id}) {
        const DbResult<Folder> moved = m_repo->move(a.id, target);
        QVERIFY(!moved);
        QCOMPARE(moved.error().kind, Kind::Cycle);
    }
    QCOMPARE(tree(), before);
    QCOMPARE(m_repo->get(a.id).value().version, 1);
}

void tst_FolderRepository::validMoves()
{
    const Folder a = create(QStringLiteral("A"));
    const Folder b = create(QStringLiteral("B"), a.id);
    const Folder c = create(QStringLiteral("C"));

    QVERIFY(m_repo->move(b.id, c.id)); // into a sibling's subtree
    QCOMPARE(tree(), QStringLiteral("A, C [B]"));
    QVERIFY(m_repo->move(b.id, std::nullopt)); // to the top level, at the end
    QCOMPARE(tree(), QStringLiteral("A, C, B"));
    QVERIFY(m_repo->move(a.id, c.id)); // a whole subtree
    QCOMPARE(tree(), QStringLiteral("C [A], B"));

    // Moving to where it already is changes nothing.
    const int version = m_repo->get(a.id).value().version;
    QVERIFY(m_repo->move(a.id, c.id));
    QCOMPARE(m_repo->get(a.id).value().version, version);
}

// TC-STO-009
void tst_FolderRepository::renameBumpsVersion()
{
    const Folder work = create(QStringLiteral("Work"));
    markSynced();
    m_clock.advance(5min);

    const DbResult<Folder> renamed = m_repo->rename(work.id, QStringLiteral("Job"));
    QVERIFY(renamed);
    QCOMPARE(renamed.value().name, QStringLiteral("Job"));
    QCOMPARE(renamed.value().version, work.version + 1);
    QCOMPARE(renamed.value().syncState, SyncState::Dirty);
    QCOMPARE(renamed.value().updatedAt, m_clock.nowMs());
    QCOMPARE(renamed.value().createdAt, work.createdAt);
    QCOMPARE(renamed.value().uuid, work.uuid);
}

void tst_FolderRepository::everyChangeMarksDirty()
{
    const Folder a = create(QStringLiteral("A"));
    const Folder b = create(QStringLiteral("B"));
    markSynced();
    m_clock.advance(1min);

    QVERIFY(m_repo->move(b.id, a.id));
    const Folder moved = m_repo->get(b.id).value();
    QCOMPARE(moved.version, 2);
    QCOMPARE(moved.syncState, SyncState::Dirty);
    QCOMPARE(moved.updatedAt, m_clock.nowMs());

    markSynced();
    m_clock.advance(1min);
    QVERIFY(m_repo->softDelete(b.id));
    const Folder deleted = m_repo->get(b.id).value();
    QCOMPARE(deleted.version, 3);
    QCOMPARE(deleted.syncState, SyncState::Dirty);
    QCOMPARE(deleted.deletedAt, std::optional<qint64>(m_clock.nowMs()));
}

// TC-STO-010
void tst_FolderRepository::deletingFolderTrashesItsNotes()
{
    const Folder work = create(QStringLiteral("Work"));
    const Folder projects = create(QStringLiteral("Projects"), work.id);
    const Folder personal = create(QStringLiteral("Personal"));
    const qint64 plan = insertNote(work.id, QStringLiteral("Plan"));
    const qint64 nested = insertNote(projects.id, QStringLiteral("Nested"));
    const qint64 other = insertNote(personal.id, QStringLiteral("Other"));
    QVERIFY(
        m_db->exec(QStringLiteral("UPDATE notes SET trashed_at = 7 WHERE id = %1").arg(nested)));
    m_clock.advance(1h);

    QVERIFY(m_repo->softDelete(work.id));
    QCOMPARE(tree(), QStringLiteral("Personal"));
    QVERIFY(m_repo->get(projects.id).value().deletedAt.has_value()); // subfolders too

    // Not lost: the notes are in Recently Deleted, with their folder and content.
    const auto noteState = [&](qint64 id) {
        DbResult<notes::storage::ScopedStatement> select = m_db->prepare(QStringLiteral(
            "SELECT coalesce(trashed_at, -1) || '|' || folder_id || '|' || body_text || '|' ||"
            " sync_state || '|' || coalesce(deleted_at, 'live') FROM notes WHERE id = ?"));
        if (!select || !select.value()->bind(1, id).step().valueOr(false)) {
            return QStringLiteral("missing");
        }
        return select.value()->column<QString>(0);
    };
    const QString now = QString::number(m_clock.nowMs());
    const QString marker = QString::fromLatin1(notes::test::kSecretMarker);
    QCOMPARE(noteState(plan), QStringLiteral("%1|%2|%3|0|live").arg(now).arg(work.id).arg(marker));
    // Already in Recently Deleted: keeps its original trash time.
    QVERIFY(noteState(nested).startsWith(QStringLiteral("7|")));
    // Notes in other folders are untouched.
    QVERIFY(noteState(other).startsWith(QStringLiteral("-1|")));
}

// TC-STO-011
void tst_FolderRepository::defaultFolderExistsAndIsProtected()
{
    const DbResult<Folder> created = m_repo->ensureDefaultFolder();
    QVERIFY(created);
    const Folder& notesFolder = created.value();
    QCOMPARE(notesFolder.uuid, QString::fromLatin1(FolderRepository::kDefaultFolderUuid));
    QCOMPARE(notesFolder.name, QStringLiteral("Notes"));
    QVERIFY(!notesFolder.parentId.has_value());
    QVERIFY(notes::models::parseUuid(notesFolder.uuid).has_value());

    // Every later start finds the same folder.
    QCOMPARE(m_repo->ensureDefaultFolder().value(), notesFolder);
    QCOMPARE(tree(), QStringLiteral("Notes"));

    const Folder work = create(QStringLiteral("Work"));
    const DbStatus deleted = m_repo->softDelete(notesFolder.id);
    QVERIFY(!deleted);
    QCOMPARE(deleted.error().kind, Kind::Protected);
    QCOMPARE(m_repo->move(notesFolder.id, work.id).error().kind, Kind::Protected);
    QCOMPARE(m_repo->rename(notesFolder.id, QStringLiteral("Mine")).error().kind, Kind::Protected);
    QCOMPARE(m_repo->get(notesFolder.id).value(), notesFolder);
    QCOMPARE(tree(), QStringLiteral("Notes, Work"));
}

// TC-STO-012
void tst_FolderRepository::names_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("stored"); // empty: rejected

    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("spaces") << QStringLiteral("   ") << QString();
    QTest::newRow("tabs and newlines") << QStringLiteral("\t\n ") << QString();
    QTest::newRow("unicode") << QStringLiteral("Ünïcödé 日本語")
                             << QStringLiteral("Ünïcödé 日本語");
    QTest::newRow("emoji") << QStringLiteral("📁 Ideas ✨") << QStringLiteral("📁 Ideas ✨");
    QTest::newRow("trimmed") << QStringLiteral("  Padded  ") << QStringLiteral("Padded");
    QTest::newRow("quotes") << QStringLiteral("Bob's \"stuff\"")
                            << QStringLiteral("Bob's \"stuff\"");
}

void tst_FolderRepository::names()
{
    QFETCH(QString, name);
    QFETCH(QString, stored);

    const DbResult<Folder> created = m_repo->create(name);
    const Folder other = create(QStringLiteral("Other"));
    const DbResult<Folder> renamed = m_repo->rename(other.id, name);
    if (stored.isEmpty()) {
        QVERIFY(!created);
        QCOMPARE(created.error().kind, Kind::InvalidArgument);
        QVERIFY(!renamed);
        QCOMPARE(renamed.error().kind, Kind::InvalidArgument);
        QCOMPARE(m_repo->get(other.id).value().name, QStringLiteral("Other"));
    } else {
        QVERIFY(created);
        QCOMPARE(created.value().name, stored);
        QCOMPARE(m_repo->get(created.value().id).value().name, stored);
        QCOMPARE(renamed.value().name, stored);
    }
}

void tst_FolderRepository::missingOrDeletedFoldersAreNotFound()
{
    const Folder gone = create(QStringLiteral("Gone"));
    const Folder kept = create(QStringLiteral("Kept"));
    QVERIFY(m_repo->softDelete(gone.id));

    QCOMPARE(m_repo->create(QStringLiteral("Child"), gone.id).error().kind, Kind::NotFound);
    QCOMPARE(m_repo->create(QStringLiteral("Child"), 999).error().kind, Kind::NotFound);
    QCOMPARE(m_repo->rename(gone.id, QStringLiteral("Back")).error().kind, Kind::NotFound);
    QCOMPARE(m_repo->move(kept.id, gone.id).error().kind, Kind::NotFound);
    QCOMPARE(m_repo->softDelete(gone.id).error().kind, Kind::NotFound);
    QCOMPARE(m_repo->get(999).error().kind, Kind::NotFound);
    QVERIFY(m_repo->get(gone.id)); // tombstones stay readable for sync
}

void tst_FolderRepository::reorderSetsSiblingOrder()
{
    const Folder parent = create(QStringLiteral("Parent"));
    const Folder a = create(QStringLiteral("A"), parent.id);
    const Folder b = create(QStringLiteral("B"), parent.id);
    const Folder c = create(QStringLiteral("C"), parent.id);
    markSynced();

    QVERIFY(m_repo->reorder(parent.id, {c.id, a.id, b.id}));
    QCOMPARE(tree(), QStringLiteral("Parent [C, A, B]"));
    // Every sibling changed position, so every one is dirty.
    for (const qint64 id : {a.id, b.id, c.id}) {
        QCOMPARE(m_repo->get(id).value().syncState, SyncState::Dirty);
    }

    markSynced();
    QVERIFY(m_repo->reorder(parent.id, {c.id, b.id, a.id}));
    QCOMPARE(tree(), QStringLiteral("Parent [C, B, A]"));
    // C kept position 0: unchanged rows aren't touched.
    QCOMPARE(m_repo->get(c.id).value().syncState, SyncState::Synced);
    QCOMPARE(m_repo->get(a.id).value().syncState, SyncState::Dirty);

    // Top level too.
    const Folder other = create(QStringLiteral("Other"));
    QVERIFY(m_repo->reorder(std::nullopt, {other.id, parent.id}));
    QCOMPARE(tree(), QStringLiteral("Other, Parent [C, B, A]"));
}

void tst_FolderRepository::reorderNeedsEverySiblingOnce()
{
    const Folder a = create(QStringLiteral("A"));
    const Folder b = create(QStringLiteral("B"));
    const Folder child = create(QStringLiteral("Child"), a.id);
    const QString before = tree();

    for (const QList<qint64>& ids : {QList<qint64>{a.id}, QList<qint64>{a.id, b.id, child.id},
                                     QList<qint64>{a.id, a.id, b.id}, QList<qint64>{b.id, 999}}) {
        const DbStatus status = m_repo->reorder(std::nullopt, ids);
        QVERIFY(!status);
        QCOMPARE(status.error().kind, Kind::InvalidArgument);
    }
    QCOMPARE(tree(), before);
}

QTEST_GUILESS_MAIN(tst_FolderRepository)
#include "tst_folderrepository.moc"
