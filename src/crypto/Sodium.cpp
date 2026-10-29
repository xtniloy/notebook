#include "crypto/Sodium.h"

#include <sodium.h>

namespace notes::crypto {

bool initialize()
{
    // 0 = initialized now, 1 = already initialized, -1 = failure.
    return sodium_init() >= 0;
}

QString sodiumVersion()
{
    return QString::fromLatin1(sodium_version_string());
}

} // namespace notes::crypto
