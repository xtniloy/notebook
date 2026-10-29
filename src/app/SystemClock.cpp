#include "app/SystemClock.h"

#include <QDateTime>

namespace notes::app {

qint64 SystemClock::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace notes::app
