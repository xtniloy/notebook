#include "app/SingleInstance.h"

#include <QByteArrayView>
#include <QDeadlineTimer>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <thread>
#include <utility>

namespace notes::app {

namespace {

Q_LOGGING_CATEGORY(lcInstance, "notes.app.instance")

constexpr QByteArrayView kActivateCommand("activate");
constexpr QByteArrayView kAckReply("ok");
constexpr qsizetype kMaxTokenLength = 256;
constexpr qint64 kMaxLineLength = 512;
constexpr std::chrono::milliseconds kConnectRetryDelay(50);
// A client that connects but never sends a full line is dropped after this.
constexpr std::chrono::seconds kClientTimeout(5);

// Activation tokens are opaque printable ASCII without spaces.
bool isValidToken(QByteArrayView token)
{
    return token.size() <= kMaxTokenLength &&
           std::ranges::all_of(token, [](char c) { return c > ' ' && c < '\x7f'; });
}

int remainingMs(const QDeadlineTimer& deadline)
{
    return static_cast<int>(std::max<qint64>(0, deadline.remainingTime()));
}

} // namespace

SingleInstance::SingleInstance(QString directory, QString key, QObject* parent)
    : QObject(parent)
    , m_directory(std::move(directory))
    , m_key(std::move(key))
{}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
}

QString SingleInstance::socketPath() const
{
    return QDir(m_directory).filePath(m_key + QStringLiteral(".socket"));
}

QString SingleInstance::lockPath() const
{
    return QDir(m_directory).filePath(m_key + QStringLiteral(".lock"));
}

bool SingleInstance::tryBecomePrimary()
{
    Q_ASSERT(!m_lock);
    m_lock = std::make_unique<QLockFile>(lockPath());
    // Don't expire a live holder's lock by age. A lock whose process has died is
    // still reclaimed.
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        const QLockFile::LockError error = m_lock->error();
        m_lock.reset();
        if (error == QLockFile::LockFailedError) {
            return false;
        }
        // The lock file can't be created at all (e.g. read-only runtime dir). Start
        // anyway rather than locking the user out; SQLite's own file locking still
        // prevents two processes from corrupting the database.
        qCWarning(lcInstance) << "Cannot create single-instance lock; error" << error;
        return true;
    }

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // We hold the lock, so an existing socket file was left behind by a crash.
    QLocalServer::removeServer(socketPath());
    if (!m_server->listen(socketPath())) {
        // Still primary; later launches will fail to activate us and report it.
        qCWarning(lcInstance) << "Cannot listen for activation requests:"
                              << m_server->errorString();
        return true;
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::handleNewConnections);
    return true;
}

void SingleInstance::handleNewConnections()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { handleMessage(socket); });
        QTimer::singleShot(kClientTimeout, socket, &QLocalSocket::abort);
    }
}

void SingleInstance::handleMessage(QLocalSocket* socket)
{
    if (!socket->canReadLine()) {
        if (socket->bytesAvailable() > kMaxLineLength) {
            socket->abort();
        }
        return;
    }

    const QByteArray line = socket->readLine(kMaxLineLength + 1).trimmed();
    const QByteArrayView view(line);
    const qsizetype space = view.indexOf(' ');
    const QByteArrayView command = space < 0 ? view : view.first(space);
    const QByteArrayView token = space < 0 ? QByteArrayView() : view.sliced(space + 1);

    if (command != kActivateCommand || !isValidToken(token)) {
        qCWarning(lcInstance) << "Ignoring malformed activation request";
        socket->abort();
        return;
    }

    socket->write(kAckReply.toByteArray() + '\n');
    socket->disconnectFromServer(); // sends the reply first
    emit activationRequested(token.toByteArray());
}

bool SingleInstance::activatePrimary(const QByteArray& activationToken,
                                     std::chrono::milliseconds timeout) const
{
    const QDeadlineTimer deadline(timeout);
    QLocalSocket socket;

    // The primary may hold the lock but not be listening yet, so retry until the deadline.
    socket.connectToServer(socketPath());
    while (!socket.waitForConnected(remainingMs(deadline))) {
        if (deadline.hasExpired()) {
            qCWarning(lcInstance) << "Running instance did not answer:" << socket.errorString();
            return false;
        }
        std::this_thread::sleep_for(kConnectRetryDelay);
        socket.connectToServer(socketPath());
    }

    QByteArray message = kActivateCommand.toByteArray();
    if (!activationToken.isEmpty()) {
        if (isValidToken(activationToken)) {
            message += ' ' + activationToken;
        } else {
            qCWarning(lcInstance) << "Dropping malformed activation token";
        }
    }
    message += '\n';

    socket.write(message);
    if (!socket.waitForBytesWritten(remainingMs(deadline))) {
        qCWarning(lcInstance) << "Cannot send activation request:" << socket.errorString();
        return false;
    }
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(remainingMs(deadline))) {
            qCWarning(lcInstance) << "Running instance did not confirm activation";
            return false;
        }
    }
    return QByteArrayView(socket.readLine().trimmed()) == kAckReply;
}

} // namespace notes::app
