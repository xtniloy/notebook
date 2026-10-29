#include "models/DateGrouping.h"

#include <QDateTime>

namespace notes::models {

namespace {

constexpr qint64 kYesterday = 1;
constexpr qint64 kLastWeek = 7;
constexpr qint64 kLastMonth = 30;

QDateTime toZone(qint64 timestampMs, const QTimeZone& zone)
{
    return QDateTime::fromMSecsSinceEpoch(timestampMs, zone);
}

// Calendar days from the timestamp's date to today's date; negative for the future.
qint64 daysAgo(const QDateTime& then, const QDateTime& now)
{
    return then.date().daysTo(now.date());
}

} // namespace

DateSection DateGrouping::sectionFor(qint64 timestampMs, qint64 nowMs, const QTimeZone& zone)
{
    using Kind = DateSection::Kind;
    const QDateTime then = toZone(timestampMs, zone);
    const QDateTime now = toZone(nowMs, zone);
    const qint64 days = daysAgo(then, now);

    if (days < kYesterday) {
        return {.kind = Kind::Today};
    }
    if (days == kYesterday) {
        return {.kind = Kind::Yesterday};
    }
    if (days <= kLastWeek) {
        return {.kind = Kind::Previous7Days};
    }
    if (days <= kLastMonth) {
        return {.kind = Kind::Previous30Days};
    }
    if (then.date().year() == now.date().year()) {
        return {.kind = Kind::Month, .month = then.date().month()};
    }
    return {.kind = Kind::Year, .year = then.date().year()};
}

QString DateGrouping::title(const DateSection& section, const QLocale& locale)
{
    using Kind = DateSection::Kind;
    switch (section.kind) {
    case Kind::Pinned:
        return tr("Pinned");
    case Kind::Today:
        return tr("Today");
    case Kind::Yesterday:
        return tr("Yesterday");
    case Kind::Previous7Days:
        return tr("Previous 7 Days");
    case Kind::Previous30Days:
        return tr("Previous 30 Days");
    case Kind::Month:
        return locale.standaloneMonthName(section.month);
    case Kind::Year:
        return QString::number(section.year);
    }
    return {};
}

QString DateGrouping::rowTimeText(qint64 timestampMs, qint64 nowMs, const QTimeZone& zone,
                                  const QLocale& locale)
{
    const QDateTime then = toZone(timestampMs, zone);
    const qint64 days = daysAgo(then, toZone(nowMs, zone));

    if (days < kYesterday) {
        return locale.toString(then.time(), QLocale::ShortFormat);
    }
    if (days == kYesterday) {
        return tr("Yesterday");
    }
    // Seven days ago is the same weekday as today, so it gets a date instead.
    if (days < kLastWeek) {
        return locale.dayName(then.date().dayOfWeek());
    }
    return locale.toString(then.date(), QLocale::ShortFormat);
}

} // namespace notes::models
