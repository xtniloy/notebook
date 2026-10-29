#pragma once

#include <chrono>
#include <cstdint>

namespace notes::test {

// Controllable clock for time-travel tests (30-day purge, auto-relock).
// Time is UTC milliseconds since epoch. Becomes an IClock implementation in task 2.2.
class FakeClock
{
public:
    // 2027-01-15 08:00:00 UTC. Fixed so tests are deterministic.
    static constexpr std::int64_t kDefaultStartMs = 1'800'000'000'000;

    explicit FakeClock(std::int64_t nowMs = kDefaultStartMs)
        : m_nowMs(nowMs)
    {}

    [[nodiscard]] std::int64_t nowMs() const { return m_nowMs; }
    void setNowMs(std::int64_t nowMs) { m_nowMs = nowMs; }
    void advance(std::chrono::milliseconds delta) { m_nowMs += delta.count(); }

private:
    std::int64_t m_nowMs;
};

} // namespace notes::test
