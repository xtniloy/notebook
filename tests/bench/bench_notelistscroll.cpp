// TC-PERF-001: scrolling 10,000 notes from top to bottom paints every frame in
// under 16 ms (Plan §10). Runs headless (offscreen, raster painting) in release.
//
// Each step scrolls a quarter of the viewport and forces a full synchronous
// repaint, which is the worst case: a real scroll only repaints the strip that
// came into view.

#include "FakeClock.h"
#include "models/FakeNoteListModel.h"
#include "ui/NoteListView.h"

#include <QElapsedTimer>
#include <QScrollBar>
#include <QTest>

#include <algorithm>
#include <numeric>
#include <vector>

class bench_NoteListScroll : public QObject
{
    Q_OBJECT

private slots:
    void scrollTenThousandNotes();
};

void bench_NoteListScroll::scrollTenThousandNotes()
{
    constexpr int kNotes = 10'000;
    constexpr qint64 kFrameBudgetNs = 16'000'000;

    const notes::test::FakeClock clock;
    QElapsedTimer timer;
    timer.start();
    notes::models::FakeNoteListModel model(kNotes, clock.nowMs(), QTimeZone::UTC);
    // Counts against the 500 ms cold start (Plan §10) while the app uses fake data.
    qInfo("Generating %d notes: %.1f ms", kNotes, static_cast<double>(timer.nsecsElapsed()) / 1e6);
    notes::ui::NoteListView view;
    view.setModel(&model);
    view.resize(300, 900); // the note list pane on a 1080p screen
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QScrollBar* bar = view.verticalScrollBar();
    QVERIFY(bar->maximum() > 0);
    const int step = std::max(1, view.viewport()->height() / 4);
    view.viewport()->repaint(); // warm up font and icon caches

    std::vector<qint64> frames;
    frames.reserve(static_cast<std::size_t>(bar->maximum() / step) + 2);
    for (int y = 0;; y = std::min(y + step, bar->maximum())) {
        bar->setValue(y);
        timer.start();
        view.viewport()->repaint();
        frames.push_back(timer.nsecsElapsed());
        if (y == bar->maximum()) {
            break;
        }
    }
    QCOMPARE(bar->value(), bar->maximum());

    std::ranges::sort(frames);
    const auto ms = [](qint64 ns) { return static_cast<double>(ns) / 1e6; };
    const double mean = ms(std::accumulate(frames.begin(), frames.end(), qint64{0})) /
                        static_cast<double>(frames.size());
    const qint64 p99 = frames.at((frames.size() * 99) / 100);
    const qint64 worst = frames.back();
    qInfo("%zu frames: mean %.2f ms, p99 %.2f ms, max %.2f ms (budget 16 ms)", frames.size(), mean,
          ms(p99), ms(worst));

    QVERIFY2(worst < kFrameBudgetNs,
             qPrintable(QStringLiteral("slowest frame %1 ms").arg(ms(worst), 0, 'f', 2)));
}

QTEST_MAIN(bench_NoteListScroll)
#include "bench_notelistscroll.moc"
