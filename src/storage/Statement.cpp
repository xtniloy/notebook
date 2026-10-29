#include "storage/Statement.h"

#include "storage/SqliteError.h"

#include <sqlite3.h>

namespace notes::storage {

Statement::Statement(sqlite3* db, sqlite3_stmt* stmt)
    : m_db(db)
    , m_stmt(stmt)
{
    Q_ASSERT(m_db && m_stmt);
}

Statement::~Statement()
{
    sqlite3_finalize(m_stmt);
}

int Statement::parameterIndex(const char* name)
{
    const int index = sqlite3_bind_parameter_index(m_stmt, name);
    if (index == 0 && !m_bindError) {
        m_bindError = DbError{
            .code = SQLITE_RANGE,
            .message = QStringLiteral("No parameter named %1").arg(QString::fromUtf8(name))};
    }
    return index;
}

void Statement::checkBind(int rc, int index)
{
    if (rc != SQLITE_OK && !m_bindError) {
        m_bindError = sqliteError(m_db, rc);
        m_bindError->message += QStringLiteral(" (parameter %1)").arg(index);
    }
}

void Statement::bindNull(int index)
{
    checkBind(sqlite3_bind_null(m_stmt, index), index);
}

void Statement::bindInt64(int index, qint64 value)
{
    checkBind(sqlite3_bind_int64(m_stmt, index, value), index);
}

void Statement::bindDouble(int index, double value)
{
    checkBind(sqlite3_bind_double(m_stmt, index, value), index);
}

void Statement::bindText(int index, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    // SQLITE_TRANSIENT: SQLite copies the bytes, since `utf8` dies here. An
    // empty (even null) QString binds as '' (non-NULL); see the class comment.
    checkBind(sqlite3_bind_text64(m_stmt, index, utf8.constData(),
                                  static_cast<sqlite3_uint64>(utf8.size()), SQLITE_TRANSIENT,
                                  SQLITE_UTF8),
              index);
}

void Statement::bindBlob(int index, const QByteArray& value)
{
    if (value.isEmpty()) {
        // sqlite3_bind_blob() with a null pointer would bind NULL instead.
        checkBind(sqlite3_bind_zeroblob(m_stmt, index, 0), index);
        return;
    }
    checkBind(sqlite3_bind_blob64(m_stmt, index, value.constData(),
                                  static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT),
              index);
}

DbResult<bool> Statement::step()
{
    if (m_bindError) {
        return *m_bindError;
    }
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    return sqliteError(m_db, rc);
}

DbStatus Statement::run()
{
    DbStatus result;
    while (true) {
        DbResult<bool> row = step();
        if (!row) {
            result = row.error();
            break;
        }
        if (!row.value()) {
            break;
        }
    }
    // A stepped statement can't be re-bound until it is reset.
    sqlite3_reset(m_stmt);
    m_bindError.reset();
    return result;
}

void Statement::reset()
{
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
    m_bindError.reset();
}

bool Statement::isNull(int index) const
{
    return sqlite3_column_type(m_stmt, index) == SQLITE_NULL;
}

int Statement::columnCount() const
{
    return sqlite3_column_count(m_stmt);
}

QString Statement::sql() const
{
    return QString::fromUtf8(sqlite3_sql(m_stmt));
}

qint64 Statement::columnInt64(int index) const
{
    return sqlite3_column_int64(m_stmt, index);
}

double Statement::columnDouble(int index) const
{
    return sqlite3_column_double(m_stmt, index);
}

QString Statement::columnText(int index) const
{
    // Text first, then bytes: sqlite3_column_bytes() after the conversion.
    const auto* text = sqlite3_column_text(m_stmt, index);
    if (text == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(m_stmt, index);
    return QString::fromUtf8(reinterpret_cast<const char*>(text), size);
}

QByteArray Statement::columnBlob(int index) const
{
    const void* blob = sqlite3_column_blob(m_stmt, index);
    const int size = sqlite3_column_bytes(m_stmt, index);
    if (blob == nullptr || size == 0) {
        // NULL stays a null QByteArray; a zero-length blob is an empty, non-null one.
        return isNull(index) ? QByteArray() : QByteArray("", 0);
    }
    return {static_cast<const char*>(blob), size};
}

} // namespace notes::storage
