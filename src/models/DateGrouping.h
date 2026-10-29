#pragma once

#include <QCoreApplication>
#include <QLocale>
#include <QString>
#include <QTimeZone>

#include <cstdint>

namespace notes::models {

// A note list section header (Plan §3.4).
struct DateSection
{
    enum class Kind : std::uint8_t {
        Pinned,
        Today,
        Yesterday,
        Previous7Days,  // 2 to 7 calendar days ago
        Previous30Days, // 8 to 30 calendar days ago
        Month,          // earlier this year: `month` is set
        Year,           // before this year: `year` is set
    };

    Kind kind = Kind::Today;
    int year = 0;
    int month = 0; // 1-12

    bool operator==(const DateSection&) const = default;
};

// Groups note timestamps the way macOS Notes does. Days are calendar days in
// `zone` (the user's local time by default), so "Yesterday" starts at midnight,
// not 24 hours ago. Timestamps are UTC milliseconds since the epoch.
class DateGrouping
{
    Q_DECLARE_TR_FUNCTIONS(DateGrouping)

public:
    // Timestamps in the future (clock skew, another device) count as today.
    [[nodiscard]] static DateSection sectionFor(qint64 timestampMs, qint64 nowMs,
                                                const QTimeZone& zone = QTimeZone::LocalTime);

    // "Today", "Previous 7 Days", "March", "2025", ...
    [[nodiscard]] static QString title(const DateSection& section,
                                       const QLocale& locale = QLocale());

    // The time shown in a note row: the time of day for today, "Yesterday",
    // the weekday for the rest of the past week, otherwise the short date.
    [[nodiscard]] static QString rowTimeText(qint64 timestampMs, qint64 nowMs,
                                             const QTimeZone& zone = QTimeZone::LocalTime,
                                             const QLocale& locale = QLocale());
};

} // namespace notes::models
