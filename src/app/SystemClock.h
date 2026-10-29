#pragma once

#include "app/IClock.h"

namespace notes::app {

// The real wall clock. The only place in src/ that may ask the system for the
// current time (ADR 0004).
class SystemClock final : public IClock
{
public:
    [[nodiscard]] qint64 nowMs() const override;
};

} // namespace notes::app
