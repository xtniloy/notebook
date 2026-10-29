// DateGrouping (task 1.6): TC-UI-014 section boundaries (midnight, exactly 7 days
// ago, year change), plus titles and row time text.

#include "FakeClock.h"
#include "models/DateGrouping.h"

#include <QDateTime>
#include <QTest>

#include <chrono>

using notes::models::DateGrouping;
using notes::models::DateSection;
using notes::test::FakeClock;
using Kind = DateSection::Kind;

Q_DECLARE_METATYPE(DateSection)

namespace {

const QTimeZone kUtc = QTimeZone::UTC;

qint64 at(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int msec = 0,
          const QTimeZone& zone = kUtc)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second, msec), zone)
        .toMSecsSinceEpoch();
}

const char* kindName(Kind kind)
{
    switch (kind) {
    case Kind::Pinned:
        return "Pinned";
    case Kind::Today:
        return "Today";
    case Kind::Yesterday:
        return "Yesterday";
    case Kind::Previous7Days:
        return "Previous7Days";
    case Kind::Previous30Days:
        return "Previous30Days";
    case Kind::Month:
        return "Month";
    case Kind::Year:
        return "Year";
    }
    return "?";
}

} // namespace

namespace QTest {
// Readable QCOMPARE failures.
template <>
char* toString(const DateSection& t) // `t`: the name in Qt's primary template
{
    return qstrdup(QByteArray(kindName(t.kind)) + " year=" + QByteArray::number(t.year) +
                   " month=" + QByteArray::number(t.month));
}
} // namespace QTest

class tst_DateGrouping : public QObject
{
    Q_OBJECT

private slots:
    void fakeClockStartsMidJanuary();
    void sectionFor_data();
    void sectionFor();
    void monthsOfThisYear_data();
    void monthsOfThisYear();
    void usesCalendarDaysInTheGivenZone();
    void daylightSavingDayIsStillOneDay();
    void titles();
    void rowTimeText();
};

// The boundaries below assume FakeClock's default start: Friday 2027-01-15 08:00 UTC.
void tst_DateGrouping::fakeClockStartsMidJanuary()
{
    const FakeClock clock;
    QCOMPARE(clock.nowMs(), at(2027, 1, 15, 8));
}

// TC-UI-014
void tst_DateGrouping::sectionFor_data()
{
    QTest::addColumn<qint64>("timestamp");
    QTest::addColumn<DateSection>("expected");

    const auto row = [](const char* name, qint64 timestamp, DateSection expected) {
        QTest::newRow(name) << timestamp << expected;
    };
    row("now", at(2027, 1, 15, 8), {.kind = Kind::Today});
    row("midnight today", at(2027, 1, 15), {.kind = Kind::Today});
    row("last ms of yesterday", at(2027, 1, 14, 23, 59, 59, 999), {.kind = Kind::Yesterday});
    row("midnight yesterday", at(2027, 1, 14), {.kind = Kind::Yesterday});
    row("last ms of 2 days ago", at(2027, 1, 13, 23, 59, 59, 999), {.kind = Kind::Previous7Days});
    row("exactly 7 days ago", at(2027, 1, 8, 8), {.kind = Kind::Previous7Days});
    row("start of 7th day ago", at(2027, 1, 8), {.kind = Kind::Previous7Days});
    row("last ms of 8 days ago", at(2027, 1, 7, 23, 59, 59, 999), {.kind = Kind::Previous30Days});
    row("exactly 30 days ago", at(2026, 12, 16, 8), {.kind = Kind::Previous30Days});
    row("start of 30th day ago", at(2026, 12, 16), {.kind = Kind::Previous30Days});
    // Year change: December 2026 is last year, so it is "2026", not "December".
    row("last ms of 31 days ago", at(2026, 12, 15, 23, 59, 59, 999),
        {.kind = Kind::Year, .year = 2026});
    row("new year's eve last year", at(2025, 12, 31, 23, 59, 59, 999),
        {.kind = Kind::Year, .year = 2025});
    row("future: later today", at(2027, 1, 15, 22), {.kind = Kind::Today});
    row("future: next week", at(2027, 1, 22), {.kind = Kind::Today});
}

void tst_DateGrouping::sectionFor()
{
    QFETCH(qint64, timestamp);
    QFETCH(DateSection, expected);
    const FakeClock clock;
    QCOMPARE(DateGrouping::sectionFor(timestamp, clock.nowMs(), kUtc), expected);
}

void tst_DateGrouping::monthsOfThisYear_data()
{
    QTest::addColumn<qint64>("timestamp");
    QTest::addColumn<DateSection>("expected");

    // Now: 2027-03-01 12:00 UTC.
    QTest::newRow("31 days ago") << at(2027, 1, 29) << DateSection{.kind = Kind::Month, .month = 1};
    QTest::newRow("30 days ago") << at(2027, 1, 30) << DateSection{.kind = Kind::Previous30Days};
    QTest::newRow("first ms of the year")
        << at(2027, 1, 1) << DateSection{.kind = Kind::Month, .month = 1};
    QTest::newRow("last ms of last year")
        << at(2026, 12, 31, 23, 59, 59, 999) << DateSection{.kind = Kind::Year, .year = 2026};
}

void tst_DateGrouping::monthsOfThisYear()
{
    QFETCH(qint64, timestamp);
    QFETCH(DateSection, expected);
    FakeClock clock;
    clock.setNowMs(at(2027, 3, 1, 12));
    QCOMPARE(DateGrouping::sectionFor(timestamp, clock.nowMs(), kUtc), expected);
}

void tst_DateGrouping::usesCalendarDaysInTheGivenZone()
{
    const FakeClock clock;                          // 08:00 UTC = 14:00 in UTC+6
    const qint64 lateEvening = at(2027, 1, 14, 20); // 02:00 on the 15th in UTC+6
    QCOMPARE(DateGrouping::sectionFor(lateEvening, clock.nowMs(), kUtc).kind, Kind::Yesterday);
    const QTimeZone plusSix = QTimeZone::fromSecondsAheadOfUtc(6 * 60 * 60);
    QCOMPARE(DateGrouping::sectionFor(lateEvening, clock.nowMs(), plusSix).kind, Kind::Today);
}

void tst_DateGrouping::daylightSavingDayIsStillOneDay()
{
    const QTimeZone berlin("Europe/Berlin");
    if (!berlin.isValid()) {
        QSKIP("No time zone database (tzdata) available");
    }
    // Clocks go forward on 2027-03-28, so that day has 23 hours.
    FakeClock clock(at(2027, 3, 29, 0, 30, 0, 0, berlin));
    QCOMPARE(
        DateGrouping::sectionFor(at(2027, 3, 28, 0, 30, 0, 0, berlin), clock.nowMs(), berlin).kind,
        Kind::Yesterday);
    clock.advance(std::chrono::hours(-1)); // 23:30 on the 28th
    QCOMPARE(
        DateGrouping::sectionFor(at(2027, 3, 28, 0, 30, 0, 0, berlin), clock.nowMs(), berlin).kind,
        Kind::Today);
}

void tst_DateGrouping::titles()
{
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    QCOMPARE(DateGrouping::title({.kind = Kind::Pinned}, english), QStringLiteral("Pinned"));
    QCOMPARE(DateGrouping::title({.kind = Kind::Today}, english), QStringLiteral("Today"));
    QCOMPARE(DateGrouping::title({.kind = Kind::Yesterday}, english), QStringLiteral("Yesterday"));
    QCOMPARE(DateGrouping::title({.kind = Kind::Previous7Days}, english),
             QStringLiteral("Previous 7 Days"));
    QCOMPARE(DateGrouping::title({.kind = Kind::Previous30Days}, english),
             QStringLiteral("Previous 30 Days"));
    QCOMPARE(DateGrouping::title({.kind = Kind::Month, .month = 3}, english),
             QStringLiteral("March"));
    QCOMPARE(DateGrouping::title({.kind = Kind::Year, .year = 2025}, english),
             QStringLiteral("2025"));

    const QLocale german(QLocale::German, QLocale::Germany);
    QCOMPARE(DateGrouping::title({.kind = Kind::Month, .month = 3}, german),
             QStringLiteral("März"));
}

void tst_DateGrouping::rowTimeText()
{
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const FakeClock clock; // Friday 2027-01-15 08:00 UTC
    const auto text = [&](qint64 timestamp) {
        return DateGrouping::rowTimeText(timestamp, clock.nowMs(), kUtc, english);
    };

    QCOMPARE(text(at(2027, 1, 15, 7, 30)), english.toString(QTime(7, 30), QLocale::ShortFormat));
    QCOMPARE(text(at(2027, 1, 14, 23)), QStringLiteral("Yesterday"));
    QCOMPARE(text(at(2027, 1, 12, 9)), QStringLiteral("Tuesday"));
    QCOMPARE(text(at(2027, 1, 9, 9)), QStringLiteral("Saturday")); // 6 days ago
    // 7 days ago is also a Friday, so it shows the date instead.
    QCOMPARE(text(at(2027, 1, 8, 9)), english.toString(QDate(2027, 1, 8), QLocale::ShortFormat));
    QCOMPARE(text(at(2025, 6, 1)), english.toString(QDate(2025, 6, 1), QLocale::ShortFormat));
}

QTEST_APPLESS_MAIN(tst_DateGrouping)
#include "tst_dategrouping.moc"
