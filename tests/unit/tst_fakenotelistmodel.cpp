// FakeNoteListModel (task 1.6): model invariants, section order and the 10,000-note
// data set used by TC-PERF-001.

#include "FakeClock.h"
#include "models/DateGrouping.h"
#include "models/FakeNoteListModel.h"
#include "models/NoteListRoles.h"

#include <QAbstractItemModelTester>
#include <QSet>
#include <QTest>

#include <limits>

using notes::models::DateGrouping;
using notes::models::FakeNoteListModel;
namespace roles = notes::models;

namespace {

const QTimeZone kUtc = QTimeZone::UTC;

bool isHeader(const QModelIndex& index)
{
    return index.data(roles::kIsSectionHeaderRole).toBool();
}

} // namespace

class tst_FakeNoteListModel : public QObject
{
    Q_OBJECT

private slots:
    void passesModelTester_data();
    void passesModelTester();
    void generatesTenThousandNotes();
    void pinnedSectionComesFirst();
    void notesSortedNewestFirstUnderMatchingHeaders();
    void headersAreNotSelectable();
    void lockedNotesHaveNoPreview();
    void sameArgumentsSameNotes();
    void emptyModel();
    void noteTextStartsWithTitleAndPreview();
    void lockedNoteHasNoText();
};

void tst_FakeNoteListModel::passesModelTester_data()
{
    QTest::addColumn<int>("count");
    QTest::newRow("small") << 50;
    QTest::newRow("10k") << 10'000;
}

void tst_FakeNoteListModel::passesModelTester()
{
    QFETCH(int, count);
    const notes::test::FakeClock clock;
    FakeNoteListModel model(count, clock.nowMs(), kUtc);
    const QAbstractItemModelTester tester(&model,
                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(model.rowCount() > count);
}

void tst_FakeNoteListModel::generatesTenThousandNotes()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(10'000, clock.nowMs(), kUtc);
    QCOMPARE(model.noteCount(), 10'000);

    QSet<qint64> ids;
    int headers = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row);
        if (isHeader(index)) {
            ++headers;
        } else {
            ids.insert(index.data(roles::kNoteIdRole).toLongLong());
        }
    }
    QCOMPARE(ids.size(), 10'000);
    QCOMPARE(model.rowCount(), 10'000 + headers);
    // Pinned, Today ... Previous 30 Days, this year's months, three past years.
    QVERIFY2(headers >= 6, qPrintable(QString::number(headers)));
}

void tst_FakeNoteListModel::pinnedSectionComesFirst()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(200, clock.nowMs(), kUtc);
    QVERIFY(isHeader(model.index(0)));
    QCOMPARE(model.index(0).data().toString(), QStringLiteral("Pinned"));
    for (int row = 1; row <= FakeNoteListModel::kPinnedCount; ++row) {
        QVERIFY(model.index(row).data(roles::kPinnedRole).toBool());
    }
    const QModelIndex afterPinned = model.index(FakeNoteListModel::kPinnedCount + 1);
    QVERIFY(isHeader(afterPinned));
    QCOMPARE(afterPinned.data().toString(), QStringLiteral("Today"));
}

void tst_FakeNoteListModel::notesSortedNewestFirstUnderMatchingHeaders()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(2'000, clock.nowMs(), kUtc);
    QString header;
    qint64 previous = std::numeric_limits<qint64>::max();
    bool previousWasHeader = false;
    for (int row = FakeNoteListModel::kPinnedCount + 1; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row);
        if (isHeader(index)) {
            QVERIFY2(!previousWasHeader, "empty section");
            header = index.data().toString();
            previousWasHeader = true;
            continue;
        }
        previousWasHeader = false;
        const qint64 modified = index.data(roles::kModifiedRole).toLongLong();
        QVERIFY(modified <= previous);
        previous = modified;
        QVERIFY(!index.data(roles::kPinnedRole).toBool());
        QCOMPARE(header,
                 DateGrouping::title(DateGrouping::sectionFor(modified, clock.nowMs(), kUtc)));
        QCOMPARE(index.data(roles::kTimeTextRole).toString(),
                 DateGrouping::rowTimeText(modified, clock.nowMs(), kUtc));
    }
    QVERIFY(!previousWasHeader);
}

void tst_FakeNoteListModel::headersAreNotSelectable()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(300, clock.nowMs(), kUtc);
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row);
        const Qt::ItemFlags flags = index.flags();
        if (isHeader(index)) {
            QVERIFY(!(flags & Qt::ItemIsSelectable));
            QVERIFY(!index.data(roles::kNoteIdRole).isValid());
        } else {
            QVERIFY(flags & Qt::ItemIsSelectable);
            QVERIFY(index.data(roles::kNoteIdRole).toLongLong() > 0);
        }
    }
}

void tst_FakeNoteListModel::lockedNotesHaveNoPreview()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(300, clock.nowMs(), kUtc);
    int locked = 0;
    int thumbnails = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row);
        if (isHeader(index)) {
            continue;
        }
        if (index.data(roles::kLockedRole).toBool()) {
            ++locked;
            QVERIFY(index.data(roles::kPreviewRole).toString().isEmpty());
            QVERIFY(!index.data().toString().isEmpty()); // the title stays visible
        } else {
            QVERIFY(!index.data(roles::kPreviewRole).toString().isEmpty());
        }
        thumbnails += index.data(roles::kHasThumbnailRole).toBool() ? 1 : 0;
    }
    QVERIFY(locked > 0);
    QVERIFY(thumbnails > 0);
}

void tst_FakeNoteListModel::sameArgumentsSameNotes()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel a(500, clock.nowMs(), kUtc);
    const FakeNoteListModel b(500, clock.nowMs(), kUtc);
    QCOMPARE(a.rowCount(), b.rowCount());
    for (int row = 0; row < a.rowCount(); ++row) {
        QCOMPARE(a.index(row).data(), b.index(row).data());
        QCOMPARE(a.index(row).data(roles::kModifiedRole), b.index(row).data(roles::kModifiedRole));
    }
}

void tst_FakeNoteListModel::emptyModel()
{
    const FakeNoteListModel model(0, notes::test::FakeClock().nowMs(), kUtc);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.noteCount(), 0);
}

// The editor's text for a note (task 1.7) agrees with its list row.
void tst_FakeNoteListModel::noteTextStartsWithTitleAndPreview()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(300, clock.nowMs(), kUtc);
    int checked = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row);
        if (isHeader(index) || index.data(roles::kLockedRole).toBool()) {
            continue;
        }
        const qint64 id = index.data(roles::kNoteIdRole).toLongLong();
        const auto found = model.note(id);
        if (!found) {
            QFAIL("note not found");
        }
        const FakeNoteListModel::FakeNote& note = *found;
        QVERIFY(!note.locked);
        QCOMPARE(note.modifiedMs, index.data(roles::kModifiedRole).toLongLong());
        const QStringList lines = note.text.split(QLatin1Char('\n'));
        QVERIFY(lines.size() >= 4);
        QCOMPARE(lines.at(0), index.data().toString());
        QCOMPARE(lines.at(1), index.data(roles::kPreviewRole).toString());
        const auto again = model.note(id);
        QVERIFY(again && again->text == note.text); // same text every time
        ++checked;
    }
    QVERIFY(checked > 200);
    QVERIFY(!model.note(0));
    QVERIFY(!model.note(301));
}

void tst_FakeNoteListModel::lockedNoteHasNoText()
{
    const notes::test::FakeClock clock;
    const FakeNoteListModel model(300, clock.nowMs(), kUtc);
    const QModelIndex locked =
        model.match(model.index(0), roles::kLockedRole, true, 1, Qt::MatchExactly).value(0);
    QVERIFY(locked.isValid());
    const auto found = model.note(locked.data(roles::kNoteIdRole).toLongLong());
    if (!found) {
        QFAIL("note not found");
    }
    const FakeNoteListModel::FakeNote& note = *found;
    QVERIFY(note.locked);
    QVERIFY(note.text.isEmpty());
    QCOMPARE(note.modifiedMs, locked.data(roles::kModifiedRole).toLongLong());
}

QTEST_MAIN(tst_FakeNoteListModel)
#include "tst_fakenotelistmodel.moc"
