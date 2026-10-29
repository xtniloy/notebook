#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace notes::models {

// UUID helpers for ADR 0005: random version 4, stored and sent as 36 lowercase
// characters without braces, e.g. "3f2504e0-4f89-41d3-9a0c-0305e82c3301".

[[nodiscard]] QString newUuid();

// Parses a UUID from outside (sync, import, drag & drop) into the canonical form.
// Accepts braces and upper case; returns nothing for invalid or nil UUIDs.
[[nodiscard]] std::optional<QString> parseUuid(QStringView text);

} // namespace notes::models
