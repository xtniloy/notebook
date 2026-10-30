// NoteListView (task 1.6): TC-UI-015 note selection signal, row heights, header
// rows that can't be selected. Runs under QT_QPA_PLATFORM=offscreen.

#include "FakeClock.h"
#include "TestHelpers.h"
#include "models/FakeNoteListModel.h"
#include "models/NoteListRoles.h"
#include "ui/MainWindow.h"
#include "ui/NoteListView.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <optional>

using notes::models::FakeNoteListModel;
using notes::ui::NoteListView;
namespace roles = notes::models;

class tst_NoteListView : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rowHeights();
    void notesIndentedFromHeaders();
    void selectsFirstNoteInitially();
    void clickingNoteEmitsNoteSelected();
    void clickingHeaderKeepsSelection();
    void keyboardSkipsHeaders();
    void selectNoteScrollsToIt();
    void selectionSurvivesModelSwap();
    void mainWindowHostsNoteList();

private:
    void clickRow(int row);
    [[nodiscard]] int rowOf(const QString& header) const;

    std::unique_ptr<FakeNoteListModel> m_model;
    std::unique_ptr<NoteListView> m_view;
};

void tst_NoteListView::init()
{
    const notes::test::FakeClock clock;
    m_model = std::make_unique<FakeNoteListModel>(500, clock.nowMs(), QTimeZone::UTC);
    m_view = std::make_unique<NoteListView>();
    m_view->setModel(m_model.get());
    m_view->resize(300, 700);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
}

void tst_NoteListView::cleanup()
{
    m_view = nullptr; // not .reset(): QAbstractItemView has its own reset()
    m_model = nullptr;
}

void tst_NoteListView::clickRow(int row)
{
    const QRect rect = m_view->visualRect(m_model->index(row));
    QVERIFY2(m_view->viewport()->rect().contains(rect.center()), "row is not visible");
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, rect.center());
}

int tst_NoteListView::rowOf(const QString& header) const
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row);
        if (index.data(roles::kIsSectionHeaderRole).toBool() && index.data() == header) {
            return row;
        }
    }
    return -1;
}

void tst_NoteListView::rowHeights()
{
    // Every note row has one height...
    const int noteHeight = m_view->visualRect(m_model->index(1)).height();
    QVERIFY(noteHeight >= 40);
    QCOMPARE(m_view->visualRect(m_model->index(2)).height(), noteHeight);
    QCOMPARE(m_view->visualRect(m_model->index(m_model->rowCount() - 1)).height(), noteHeight);
    // ...and section headers are compact (UI review: no big gap above "Today").
    const int headerHeight = m_view->visualRect(m_model->index(0)).height(); // "Pinned"
    QVERIFY(headerHeight >= m_view->fontMetrics().height());
    QVERIFY2(
        headerHeight <= noteHeight * 2 / 3,
        qPrintable(QStringLiteral("header %1 px, note %2 px").arg(headerHeight).arg(noteHeight)));
    // Rows span the full width, so the whole row is clickable.
    QCOMPARE(m_view->visualRect(m_model->index(1)).width(), m_view->viewport()->width());
}

// UI review (as macOS Notes): notes start further right than the section
// header above them, so the headers stand out; the header stays where it was.
void tst_NoteListView::notesIndentedFromHeaders()
{
    m_view->clearSelection(); // keep the highlight out of the measurement
    const QImage image = m_view->viewport()->grab().toImage();
    const QColor background = m_view->palette().color(QPalette::Base);
    // Left-most pixel in `rect` that differs clearly from the background (text or icon).
    const auto inkStart = [&](const QRect& rect) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                const QColor c = image.pixelColor(x, y);
                if (qAbs(c.lightness() - background.lightness()) > 60) {
                    return x;
                }
            }
        }
        return -1;
    };
    const int today = rowOf(QStringLiteral("Today"));
    const int header = inkStart(m_view->visualRect(m_model->index(today)));
    const int note = inkStart(m_view->visualRect(m_model->index(today + 1)));
    QVERIFY(header > 0 && note > 0);
    // Unchanged: the 16 px padding (plus up to 2 px of glyph side bearing).
    QVERIFY2(header >= 16 && header <= 18, qPrintable(QString::number(header)));
    QVERIFY2(note - header >= 12,
             qPrintable(QStringLiteral("header at %1 px, note at %2 px").arg(header).arg(note)));
}

void tst_NoteListView::selectsFirstNoteInitially()
{
    const QModelIndex first = m_model->index(1); // row 0 is the "Pinned" header
    QCOMPARE(m_view->currentIndex(), first);
    QVERIFY(m_view->selectionModel()->isSelected(first));
    QCOMPARE(m_view->currentNote(), first.data(roles::kNoteIdRole).toLongLong());
}

// TC-UI-015
void tst_NoteListView::clickingNoteEmitsNoteSelected()
{
    QSignalSpy spy(m_view.get(), &NoteListView::noteSelected);
    const int row = rowOf(QStringLiteral("Today")) + 1;
    const qint64 id = m_model->index(row).data(roles::kNoteIdRole).toLongLong();

    clickRow(row);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toLongLong(), id);
    QCOMPARE(m_view->currentNote(), std::optional<qint64>(id));

    clickRow(row); // already selected
    QCOMPARE(spy.count(), 0);
}

void tst_NoteListView::clickingHeaderKeepsSelection()
{
    const QSignalSpy spy(m_view.get(), &NoteListView::noteSelected);
    const std::optional<qint64> before = m_view->currentNote();
    clickRow(0); // "Pinned"
    clickRow(rowOf(QStringLiteral("Today")));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(m_view->currentNote(), before);
    QCOMPARE(m_view->selectionModel()->selectedIndexes(), QModelIndexList{m_model->index(1)});
}

void tst_NoteListView::keyboardSkipsHeaders()
{
    const int lastPinned = FakeNoteListModel::kPinnedCount; // rows 1..3
    QVERIFY(m_view->selectNote(m_model->index(lastPinned).data(roles::kNoteIdRole).toLongLong()));
    m_view->setFocus();

    QSignalSpy spy(m_view.get(), &NoteListView::noteSelected);
    QTest::keyClick(m_view.get(), Qt::Key_Down); // over the "Today" header
    const QModelIndex firstToday = m_model->index(lastPinned + 2);
    QCOMPARE(m_view->currentIndex(), firstToday);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toLongLong(), firstToday.data(roles::kNoteIdRole).toLongLong());

    QTest::keyClick(m_view.get(), Qt::Key_Up);
    QCOMPARE(m_view->currentIndex(), m_model->index(lastPinned));
}

void tst_NoteListView::selectNoteScrollsToIt()
{
    const QModelIndex last = m_model->index(m_model->rowCount() - 1);
    const qint64 id = last.data(roles::kNoteIdRole).toLongLong();
    QSignalSpy spy(m_view.get(), &NoteListView::noteSelected);

    QVERIFY(m_view->selectNote(id));
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_view->verticalScrollBar()->value() > 0);
    QVERIFY(m_view->viewport()->rect().contains(m_view->visualRect(last)));
    QVERIFY(!m_view->selectNote(999'999));
    QCOMPARE(m_view->currentNote(), std::optional<qint64>(id));
}

void tst_NoteListView::selectionSurvivesModelSwap()
{
    const int row = rowOf(QStringLiteral("Previous 30 Days")) + 1;
    const qint64 id = m_model->index(row).data(roles::kNoteIdRole).toLongLong();
    QVERIFY(m_view->selectNote(id));

    const notes::test::FakeClock clock;
    FakeNoteListModel same(500, clock.nowMs(), QTimeZone::UTC);
    const QSignalSpy spy(m_view.get(), &NoteListView::noteSelected);
    m_view->setModel(&same);
    QCOMPARE(m_view->currentIndex().data(roles::kNoteIdRole).toLongLong(), id);
    QCOMPARE(spy.count(), 0);
    m_view->setModel(m_model.get());
}

void tst_NoteListView::mainWindowHostsNoteList()
{
    const notes::test::TempDb dir;
    const auto context = notes::test::makeTestContext(dir.dirPath());
    const notes::ui::MainWindow window(*context);
    const auto* list = window.findChild<NoteListView*>(QStringLiteral("noteListPane"));
    QVERIFY(list);
    QVERIFY(list->model());
    QCOMPARE(qobject_cast<const FakeNoteListModel*>(list->model())->noteCount(), 10'000);
    // Dated from the context's clock (a FakeClock here), not the real time (ADR 0004).
    const qint64 newest = list->model()->index(1, 0).data(roles::kModifiedRole).toLongLong();
    QVERIFY(newest <= notes::test::FakeClock::kDefaultStartMs);
    QVERIFY(newest > notes::test::FakeClock::kDefaultStartMs - (qint64{6} * 60 * 60 * 1000));
    QVERIFY(list->currentNote().has_value());
}

QTEST_MAIN(tst_NoteListView)
#include "tst_notelistview.moc"
