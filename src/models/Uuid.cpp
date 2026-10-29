#include "models/Uuid.h"

#include <QUuid>

namespace notes::models {

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

std::optional<QString> parseUuid(QStringView text)
{
    const QUuid uuid = QUuid::fromString(text);
    if (uuid.isNull()) { // also what fromString() returns for invalid input
        return std::nullopt;
    }
    return uuid.toString(QUuid::WithoutBraces);
}

} // namespace notes::models
