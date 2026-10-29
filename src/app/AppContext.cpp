#include "app/AppContext.h"

#include "app/IClock.h"
#include "app/SystemClock.h"

#include <QSettings>

#include <utility>

namespace notes::app {

AppContext::AppContext(std::unique_ptr<QSettings> settings, std::unique_ptr<IClock> clock)
    : m_settings(std::move(settings))
    , m_clock(std::move(clock))
{
    Q_ASSERT(m_settings);
    Q_ASSERT(m_clock);
}

AppContext::~AppContext() = default;

std::unique_ptr<AppContext> AppContext::createDefault()
{
    return std::make_unique<AppContext>(std::make_unique<QSettings>(),
                                        std::make_unique<SystemClock>());
}

} // namespace notes::app
