#pragma once

#include <QString>

#include <optional>
#include <utility>

namespace notes::storage {

// A failed database operation (ADR 0002). Values, not exceptions, so errors can
// cross from the DB thread to the UI (ADR 0003). The message comes from SQLite
// and names tables and columns, never bound values, so it is safe to log.
struct DbError
{
    int code = 0; // SQLite extended result code, e.g. SQLITE_CONSTRAINT_UNIQUE
    QString message;

    [[nodiscard]] int primaryCode() const { return code & 0xff; }
};

// Either a value or a DbError (C++20 has no std::expected).
template <typename T>
class [[nodiscard]] DbResult
{
public:
    DbResult(T value) // implicit, so a function can `return value;`
        : m_value(std::move(value))
    {}
    DbResult(DbError error) // implicit, so a function can `return error;`
        : m_error(std::move(error))
    {}

    [[nodiscard]] bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    // Calling these on an error is a programming bug: check ok() first.
    [[nodiscard]] T& value() &
    {
        if (!m_value) {
            qFatal("DbResult::value() called on an error: %s", qPrintable(m_error.message));
        }
        return *m_value;
    }
    [[nodiscard]] const T& value() const&
    {
        if (!m_value) {
            qFatal("DbResult::value() called on an error: %s", qPrintable(m_error.message));
        }
        return *m_value;
    }
    [[nodiscard]] T&& value() && { return std::move(value()); }
    // The value, or `fallback` on an error.
    [[nodiscard]] T valueOr(T fallback) const { return m_value.value_or(std::move(fallback)); }

    [[nodiscard]] const DbError& error() const { return m_error; }

private:
    std::optional<T> m_value;
    DbError m_error;
};

// Success without a value, or a DbError.
template <>
class [[nodiscard]] DbResult<void>
{
public:
    DbResult() = default;
    DbResult(DbError error) // implicit, so a function can `return error;`
        : m_error(std::move(error))
    {}

    [[nodiscard]] bool ok() const { return !m_error.has_value(); }
    explicit operator bool() const { return ok(); }
    // An empty error (code 0) when ok().
    [[nodiscard]] DbError error() const { return m_error.value_or(DbError{}); }

private:
    std::optional<DbError> m_error;
};

using DbStatus = DbResult<void>;

} // namespace notes::storage
