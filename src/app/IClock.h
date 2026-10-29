#pragma once

#include <QtTypes>

namespace notes::app {

// The only way code reads the current time (ADR 0004). The app uses SystemClock;
// tests use FakeClock (tests/common/), which can jump forward.
class IClock
{
public:
    IClock() = default;
    virtual ~IClock() = default;
    IClock(const IClock&) = delete;
    IClock& operator=(const IClock&) = delete;
    IClock(IClock&&) = delete;
    IClock& operator=(IClock&&) = delete;

    // UTC milliseconds since 1970-01-01.
    [[nodiscard]] virtual qint64 nowMs() const = 0;
};

} // namespace notes::app
