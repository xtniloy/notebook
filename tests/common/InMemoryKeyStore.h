#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>

namespace notes::test {

// Stand-in for GNOME Keyring in tests. Becomes an IKeyStore implementation
// (async API, SecureBuffer values) in task 4.2.
class InMemoryKeyStore
{
public:
    [[nodiscard]] std::optional<QByteArray> read(const QString& name) const
    {
        const auto it = m_secrets.constFind(name);
        if (it == m_secrets.constEnd()) {
            return std::nullopt;
        }
        return *it;
    }

    void write(const QString& name, const QByteArray& secret) { m_secrets.insert(name, secret); }
    void remove(const QString& name) { m_secrets.remove(name); }

private:
    QHash<QString, QByteArray> m_secrets;
};

} // namespace notes::test
