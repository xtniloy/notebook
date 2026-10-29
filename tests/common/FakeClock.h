#pragma once

#include "app/IClock.h"

#include <chrono>
#include <cstdint>

namespace notes::test {

// Controllable clock for time-travel tests (30-day purge, auto-relock).
// Time is UTC milliseconds since epoch (ADR 0004).
class FakeClock final : public app::IClock
{
public:
    // 2027-01-15 08:00:00 UTC, a Friday. Fixed so tests are deterministic.
    static constexpr std::int64_t kDefaultStartMs = 1'800'000'000'000;

    explicit FakeClock(std::int64_t nowMs = kDefaultStartMs)
        : m_nowMs(nowMs)
    {}

    [[nodiscard]] qint64 nowMs() const override { return m_nowMs; }
    void setNowMs(std::int64_t nowMs) { m_nowMs = nowMs; }
    // Negative deltas move time backwards, like a wall clock being corrected.
    void advance(std::chrono::milliseconds delta) { m_nowMs += delta.count(); }

private:
    std::int64_t m_nowMs;
};

} // namespace notes::test
