#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <chrono>
#include <memory>

class QLocalServer;
class QLocalSocket;
class QLockFile;

namespace notes::app {

// Keeps NotesApp to one process per user, so two processes never write the same
// database or run two sync engines. A second launch asks the running instance to
// show its window, then exits.
//
// A QLockFile decides who is primary; if the holder crashed, QLockFile notices the
// dead PID and reclaims the lock. The primary listens on a local socket next to the
// lock for one-line messages: "activate[ <token>]\n", answered with "ok\n".
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SingleInstance)

public:
    // `directory` must be private to the user; see defaultDirectory().
    SingleInstance(QString directory, QString key, QObject* parent = nullptr);
    ~SingleInstance() override;

    // $XDG_RUNTIME_DIR: per-user, mode 0700, cleared on logout.
    static QString defaultDirectory();

    // True if this process is now the primary instance. False if another instance
    // holds the lock; the caller should activatePrimary() and exit.
    [[nodiscard]] bool tryBecomePrimary();

    // Asks the primary instance to show itself and waits for its reply.
    // `activationToken` is the launcher's $XDG_ACTIVATION_TOKEN; Wayland only lets
    // the primary take focus with it. Invalid tokens are dropped.
    [[nodiscard]] bool
    activatePrimary(const QByteArray& activationToken,
                    std::chrono::milliseconds timeout = std::chrono::seconds(3)) const;

    [[nodiscard]] QString socketPath() const;
    [[nodiscard]] QString lockPath() const;

signals:
    // Emitted on the primary. `activationToken` is empty if the launcher gave none.
    void activationRequested(const QByteArray& activationToken);

private:
    void handleNewConnections();
    void handleMessage(QLocalSocket* socket);

    QString m_directory;
    QString m_key;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer* m_server = nullptr; // child of this
};

} // namespace notes::app
