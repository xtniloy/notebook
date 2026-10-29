#pragma once

#include <QString>
#include <QTemporaryDir>

namespace notes::test {

// Put this in note bodies so later tests can prove it never appears on disk,
// in logs, or on the network.
inline constexpr char kSecretMarker[] = "SECRET-MARKER-7f3a";

// A fresh temp directory with a database path inside it, removed on destruction.
// Tests must never touch ~/.local/share or the real keyring.
class TempDb
{
public:
    [[nodiscard]] bool isValid() const { return m_dir.isValid(); }
    [[nodiscard]] QString dirPath() const { return m_dir.path(); }
    [[nodiscard]] QString path() const { return m_dir.filePath(QStringLiteral("notes.db")); }

private:
    QTemporaryDir m_dir;
};

} // namespace notes::test
