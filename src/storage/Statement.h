#pragma once

#include "storage/DbResult.h"

#include <QByteArray>
#include <QString>

#include <optional>
#include <type_traits>

struct sqlite3;
struct sqlite3_stmt;

namespace notes::storage {

// A prepared SQLite statement with typed parameters and columns (ADR 0002).
// Parameter and column indexes start at 1 and 0 respectively, as in SQLite.
//
// Binding: QString always binds as TEXT and QByteArray as BLOB, even when empty;
// use std::optional (or nullptr) for SQL NULL. A failed bind() is remembered and
// returned by the next step()/run(), so binds can be chained without checks.
//
// Get statements from Database::prepare(); create them directly only in the
// storage layer.
class Statement
{
public:
    Statement(sqlite3* db, sqlite3_stmt* stmt);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;

    template <typename T>
    Statement& bind(int index, const T& value)
    {
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            bindNull(index);
        } else if constexpr (IsOptional<T>::value) {
            if (value.has_value()) {
                bind(index, *value);
            } else {
                bindNull(index);
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            bindInt64(index, value ? 1 : 0);
        } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
            bindInt64(index, static_cast<qint64>(value));
        } else if constexpr (std::is_floating_point_v<T>) {
            bindDouble(index, static_cast<double>(value));
        } else if constexpr (std::is_same_v<T, QByteArray>) {
            // Before the QString branch: QByteArray converts to QString implicitly.
            bindBlob(index, value);
        } else if constexpr (std::is_convertible_v<T, QString>) {
            bindText(index, value);
        } else {
            static_assert(sizeof(T) == 0, "unsupported parameter type");
        }
        return *this;
    }

    // Named parameter, e.g. bind(":title", note.title).
    template <typename T>
    Statement& bind(const char* name, const T& value)
    {
        return bind(parameterIndex(name), value);
    }

    // Runs one step: true if a row is ready to read, false when done.
    DbResult<bool> step();
    // Steps until done, for statements without result rows (INSERT, UPDATE, ...),
    // then rewinds (keeping the bindings) so it can be bound and run again.
    DbStatus run();
    // Back to the start, with all parameters unbound.
    void reset();

    template <typename T>
    [[nodiscard]] T column(int index) const
    {
        if constexpr (IsOptional<T>::value) {
            if (isNull(index)) {
                return std::nullopt;
            }
            return column<typename T::value_type>(index);
        } else if constexpr (std::is_same_v<T, bool>) {
            return columnInt64(index) != 0;
        } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
            return static_cast<T>(columnInt64(index));
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(columnDouble(index));
        } else if constexpr (std::is_same_v<T, QString>) {
            return columnText(index);
        } else if constexpr (std::is_same_v<T, QByteArray>) {
            return columnBlob(index);
        } else {
            static_assert(sizeof(T) == 0, "unsupported column type");
        }
    }

    [[nodiscard]] bool isNull(int index) const;
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] QString sql() const;

private:
    template <typename T>
    struct IsOptional : std::false_type
    {};
    template <typename T>
    struct IsOptional<std::optional<T>> : std::true_type
    {};

    [[nodiscard]] int parameterIndex(const char* name);
    void bindNull(int index);
    void bindInt64(int index, qint64 value);
    void bindDouble(int index, double value);
    void bindText(int index, const QString& value);
    void bindBlob(int index, const QByteArray& value);
    void checkBind(int rc, int index);

    [[nodiscard]] qint64 columnInt64(int index) const;
    [[nodiscard]] double columnDouble(int index) const;
    [[nodiscard]] QString columnText(int index) const;
    [[nodiscard]] QByteArray columnBlob(int index) const;

    sqlite3* m_db;
    sqlite3_stmt* m_stmt;
    std::optional<DbError> m_bindError;
};

} // namespace notes::storage
