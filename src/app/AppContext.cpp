#include "app/AppContext.h"

#include <QSettings>

#include <utility>

namespace notes::app {

AppContext::AppContext(std::unique_ptr<QSettings> settings)
    : m_settings(std::move(settings))
{
    Q_ASSERT(m_settings);
}

AppContext::~AppContext() = default;

std::unique_ptr<AppContext> AppContext::createDefault()
{
    return std::make_unique<AppContext>(std::make_unique<QSettings>());
}

} // namespace notes::app
