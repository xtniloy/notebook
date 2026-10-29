#include "app/AppIdentity.h"

#include "app/Config.h"

#include <QCoreApplication>

namespace notes::app {

void applyAppIdentity()
{
    QCoreApplication::setOrganizationName(QString::fromLatin1(config::kOrgName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(config::kOrgDomain));
    QCoreApplication::setApplicationName(QString::fromLatin1(config::kAppName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(config::kAppVersion));
}

} // namespace notes::app
