#pragma once

#include <QString>

namespace notes::crypto {

// Initializes libsodium. Safe to call more than once and from any thread.
// Returns false if the library is unusable; the app must not continue.
[[nodiscard]] bool initialize();

// Version of the linked libsodium, e.g. "1.0.18".
QString sodiumVersion();

} // namespace notes::crypto
