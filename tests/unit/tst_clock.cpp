// IClock (task 2.2): TC-STO-001 FakeClock time travel, SystemClock reads the
// real time, and AppContext hands out the right clock (ADR 0004).

#include "FakeClock.h"
#include "TestHelpers.h"
#include "app/AppContext.h"
#include "app/AppIdentity.h"
#include "app/SystemClock.h"

#include <QDateTime>
#include <QTest>

#include <chrono>

using namespace std::chrono_literals;
using notes::app::IClock;
using notes::test::FakeClock;

class tst_Clock : public QObject
{
    Q_OBJECT

private slots:
    void fakeClockAdvancesExactly();
    void fakeClockCanGoBackwards();
    void systemClockReadsRealTime();
    void defaultContextUsesSystemClock();
    void testContextUsesFakeClock();
};

// TC-STO-001
void tst_Clock::fakeClockAdvancesExactly()
{
    FakeClock fake;
    const IClock& clock = fake; // as code under test sees it
    const qint64 start = clock.nowMs();
    QCOMPARE(start, FakeClock::kDefaultStartMs);

    fake.advance(std::chrono::days(30));
    QCOMPARE(clock.nowMs() - start, qint64{30} * 24 * 60 * 60 * 1000);

    fake.advance(1ms);
    QCOMPARE(clock.nowMs() - start, (qint64{30} * 24 * 60 * 60 * 1000) + 1);
}

// A corrected wall clock can jump back (ADR 0004: relock must fail safe).
void tst_Clock::fakeClockCanGoBackwards()
{
    FakeClock fake;
    fake.advance(-2h);
    QCOMPARE(fake.nowMs(), FakeClock::kDefaultStartMs - (qint64{2} * 60 * 60 * 1000));
    fake.setNowMs(0);
    QCOMPARE(fake.nowMs(), qint64{0});
}

void tst_Clock::systemClockReadsRealTime()
{
    const notes::app::SystemClock clock;
    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    const qint64 now = clock.nowMs();
    const qint64 after = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(before <= now);
    QVERIFY(now <= after);
}

void tst_Clock::defaultContextUsesSystemClock()
{
    notes::app::applyAppIdentity();
    const auto context = notes::app::AppContext::createDefault();
    QVERIFY(dynamic_cast<const notes::app::SystemClock*>(&context->clock()) != nullptr);
}

void tst_Clock::testContextUsesFakeClock()
{
    const notes::test::TempDb dir;
    const auto context = notes::test::makeTestContext(dir.dirPath());
    QCOMPARE(context->clock().nowMs(), FakeClock::kDefaultStartMs);
}

QTEST_MAIN(tst_Clock)
#include "tst_clock.moc"
