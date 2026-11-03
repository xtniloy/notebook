// NoteListView (task 1.6): TC-UI-015 note selection signal, row heights, header
// rows that can't be selected. Runs under QT_QPA_PLATFORM=offscreen.

#include "FakeClock.h"
#include "FakeNoteListModel.h"
#include "TestHelpers.h"
#include "models/NoteListModel.h"
#include "models/NoteListRoles.h"
#include "models/SidebarRoles.h"
#include "services/NoteService.h"
#include "ui/MainWindow.h"
#include "ui/NoteListDelegate.h"
#include "ui/NoteListView.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
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
    void titleAndDetailFonts();
    void separatorLines();
    void baseColorUnderScrollBar();
    void spaceUnderLastNote();
    void notesIndentedFromHeaders();
    void selectsFirstNoteInitially();
    void clickingNoteEmitsNoteSelected();
    void clickingHeaderKeepsSelection();
    void keyboardSkipsHeaders();
    void selectNoteScrollsToIt();
    void selectionSurvivesModelSwap();
    void notesArrivingLaterAreSelected();
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

// UI review: titles are Bold (700); the time and preview are 0.15 pt smaller
// (10.85 pt under the usual 11 pt system font), same family, regular weight.
void tst_NoteListView::titleAndDetailFonts()
{
    QFont base(QStringLiteral("Inter"));
    base.setPointSizeF(11.0);

    const QFont title = notes::ui::NoteListDelegate::titleFont(base);
    QCOMPARE(title.weight(), QFont::Bold);
    QCOMPARE(title.pointSizeF(), 11.0);

    const QFont detail = notes::ui::NoteListDelegate::detailFont(base);
    QCOMPARE(detail.pointSizeF(), 10.85);
    QCOMPARE(detail.families(), base.families());
    QCOMPARE(detail.weight(), base.weight());
}

// UI review: a visible but light line, the theme's separator color at half
// strength, between notes and under each section header; none above a header
// (its gap is enough) or around the selected note.
void tst_NoteListView::separatorLines()
{
    const QImage image = m_view->viewport()->grab().toImage();
    // The separator color blended over the list's background.
    const QColor mid = m_view->palette().color(QPalette::Mid);
    const QColor base = m_view->palette().color(QPalette::Base);
    const qreal alpha = notes::ui::NoteListDelegate::kSeparatorOpacity;
    const QColor line =
        QColor::fromRgbF(static_cast<float>((alpha * mid.redF()) + ((1 - alpha) * base.redF())),
                         static_cast<float>((alpha * mid.greenF()) + ((1 - alpha) * base.greenF())),
                         static_cast<float>((alpha * mid.blueF()) + ((1 - alpha) * base.blueF())));
    const qreal ratio = image.devicePixelRatio();
    const auto pixel = [&](int x, int y) {
        return image.pixelColor(static_cast<int>(x * ratio), static_cast<int>(y * ratio));
    };
    // 8 bits per channel in the grabbed image: allow rounding.
    const auto hasLine = [&](const QRect& rect) {
        const QColor seen = pixel(rect.center().x(), rect.bottom());
        return qAbs(seen.red() - line.red()) <= 2 && qAbs(seen.green() - line.green()) <= 2 &&
               qAbs(seen.blue() - line.blue()) <= 2;
    };

    QCOMPARE(m_model->index(0).data().toString(), QStringLiteral("Pinned"));
    QVERIFY2(hasLine(m_view->visualRect(m_model->index(0))), "under the header");

    // Two unselected notes in a row: a line between them, full strength.
    int row = 1;
    while (row + 1 < m_model->rowCount() &&
           (m_model->index(row + 1).data(roles::kIsSectionHeaderRole).toBool() ||
            m_view->currentIndex().row() == row)) {
        ++row;
    }
    QVERIFY2(hasLine(m_view->visualRect(m_model->index(row))), "between notes");

    // The last note of a section: no line above the next header.
    const int header = rowOf(QStringLiteral("Today"));
    QVERIFY(header > 1);
    QVERIFY(!hasLine(m_view->visualRect(m_model->index(header - 1))));
}

// UI review: the scroll bar is drawn transparent (AppStyle), so the list fills
// its whole area with the base color: no gray strip along its right edge.
void tst_NoteListView::baseColorUnderScrollBar()
{
    QVERIFY(m_view->autoFillBackground());
    QCOMPARE(m_view->backgroundRole(), QPalette::Base);
}

// UI review: scrolled to the end, the last note doesn't touch the bottom edge.
void tst_NoteListView::spaceUnderLastNote()
{
    m_view->scrollToBottom();
    QCoreApplication::processEvents();
    const QModelIndex last = m_model->index(m_model->rowCount() - 1);
    const int lastBottom =
        m_view->viewport()->mapTo(m_view.get(), m_view->visualRect(last).bottomLeft()).y();
    QCOMPARE(m_view->height() - 1 - lastBottom, notes::ui::NoteListView::kBottomPadding);
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
    // The bottom pixel row holds the separator line (separatorLines()), which
    // starts at the padding: leave it out, measure the text.
    const auto textArea = [&](int row) {
        return m_view->visualRect(m_model->index(row)).adjusted(0, 0, 0, -1);
    };
    const int today = rowOf(QStringLiteral("Today"));
    const int header = inkStart(textArea(today));
    const int note = inkStart(textArea(today + 1));
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

// Task 2.8: the real model loads asynchronously and changes row by row.
void tst_NoteListView::notesArrivingLaterAreSelected()
{
    const auto noteRow = [](qint64 id) {
        auto* item = new QStandardItem(QStringLiteral("Note %1").arg(id));
        item->setData(id, roles::kNoteIdRole);
        return item;
    };
    QStandardItemModel model;
    NoteListView view;
    view.setModel(&model);
    QSignalSpy selected(&view, &NoteListView::noteSelected);
    QSignalSpy cleared(&view, &NoteListView::currentNoteCleared);
    QVERIFY(!view.currentNote());

    // The first page arrives: its first note is selected and announced.
    model.appendRow(noteRow(1));
    model.appendRow(noteRow(2));
    QTRY_COMPARE(selected.count(), 1);
    QCOMPARE(selected.takeFirst().at(0).toLongLong(), qint64(1));
    QCOMPARE(view.currentNote(), std::optional<qint64>(1));

    // An edited note moving to the top is removed and reinserted at once: it
    // stays selected, and the editor isn't told to switch.
    QVERIFY(view.selectNote(2));
    selected.clear();
    model.removeRow(1);
    model.insertRow(0, noteRow(2));
    QCoreApplication::processEvents();
    QCOMPARE(selected.count(), 0);
    QCOMPARE(view.currentNote(), std::optional<qint64>(2));
    QCOMPARE(view.currentIndex().row(), 0);

    // Its note gone for good: the first remaining one; then nothing.
    model.removeRow(0);
    QTRY_COMPARE(selected.count(), 1);
    QCOMPARE(view.currentNote(), std::optional<qint64>(1));
    model.removeRow(0);
    QTRY_COMPARE(cleared.count(), 1);
    QVERIFY(!view.currentNote());
}

void tst_NoteListView::mainWindowHostsNoteList()
{
    const notes::test::TempDb dir;
    const auto context = notes::test::makeTestContext(dir.dirPath());
    const auto note = notes::test::await(
        context->notes().create(roles::kAllNotesFolderId, {}, QStringLiteral("Hello")));
    QVERIFY(note);
    const notes::ui::MainWindow window(*context);
    const auto* list = window.findChild<NoteListView*>(QStringLiteral("noteListPane"));
    QVERIFY(list);
    QVERIFY(qobject_cast<const notes::models::NoteListModel*>(list->model()));
    // Loaded from the database asynchronously, then selected.
    QTRY_COMPARE(list->currentNote(), std::optional<qint64>(note.value().id));
    // Dated from the context's clock (a FakeClock here), not the real time (ADR 0004).
    QCOMPARE(list->currentIndex().data(roles::kModifiedRole).toLongLong(),
             notes::test::FakeClock::kDefaultStartMs);
}

QTEST_MAIN(tst_NoteListView)
#include "tst_notelistview.moc"
