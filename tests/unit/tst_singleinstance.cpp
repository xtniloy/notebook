// SingleInstance guard, both sides in one process. The two-process flow with the
// real binary (TC-UI-001) is in tests/integration/tst_singleinstance_process.cpp.

#include "app/SingleInstance.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <future>

using namespace std::chrono_literals;
using notes::app::SingleInstance;

namespace {

QString testKey()
{
    return QStringLiteral("io.augmex.NotesApp.test");
}

// activatePrimary() blocks, and the primary needs this thread's event loop to
// answer, so the client runs on another thread. Wait with QTRY_* macros.
std::future<bool> activateAsync(const QString& directory, const QByteArray& token)
{
    return std::async(std::launch::async, [directory, token] {
        const SingleInstance client(directory, testKey());
        return client.activatePrimary(token, 5s);
    });
}

// Sends raw bytes to the primary; true if the primary then closes the connection.
std::future<bool> sendRawAsync(const QString& socketPath, const QByteArray& bytes)
{
    return std::async(std::launch::async, [socketPath, bytes] {
        QLocalSocket socket;
        socket.connectToServer(socketPath);
        if (!socket.waitForConnected(5000)) {
            return false;
        }
        socket.write(bytes);
        socket.waitForBytesWritten(5000);
        return socket.state() == QLocalSocket::UnconnectedState || socket.waitForDisconnected(5000);
    });
}

bool isReady(const std::future<bool>& future)
{
    return future.wait_for(0s) == std::future_status::ready;
}

} // namespace

class tst_SingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void firstInstanceBecomesPrimary();
    void secondInstanceIsRefused();
    void lockIsReleasedOnDestruction();
    void socketIsPrivateToUser();
    void activationReachesPrimary_data();
    void activationReachesPrimary();
    void malformedMessageIsIgnored_data();
    void malformedMessageIsIgnored();
    void activationFailsWithoutPrimary();
    void leftoverSocketFileIsReplaced();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

void tst_SingleInstance::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void tst_SingleInstance::cleanup()
{
    m_dir.reset();
}

void tst_SingleInstance::firstInstanceBecomesPrimary()
{
    SingleInstance instance(m_dir->path(), testKey());
    QVERIFY(instance.tryBecomePrimary());
    QVERIFY(QFile::exists(instance.lockPath()));
    QVERIFY(QFile::exists(instance.socketPath()));
}

void tst_SingleInstance::secondInstanceIsRefused()
{
    SingleInstance first(m_dir->path(), testKey());
    QVERIFY(first.tryBecomePrimary());

    SingleInstance second(m_dir->path(), testKey());
    QVERIFY(!second.tryBecomePrimary());
}

void tst_SingleInstance::lockIsReleasedOnDestruction()
{
    {
        SingleInstance first(m_dir->path(), testKey());
        QVERIFY(first.tryBecomePrimary());
    }
    SingleInstance second(m_dir->path(), testKey());
    QVERIFY(second.tryBecomePrimary());
}

void tst_SingleInstance::socketIsPrivateToUser()
{
    SingleInstance instance(m_dir->path(), testKey());
    QVERIFY(instance.tryBecomePrimary());

    const QFile::Permissions others = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    QCOMPARE(QFileInfo(instance.socketPath()).permissions() & others, QFile::Permissions());
}

void tst_SingleInstance::activationReachesPrimary_data()
{
    QTest::addColumn<QByteArray>("sentToken");
    QTest::addColumn<QByteArray>("receivedToken");

    const QByteArray gnomeToken("gnome-shell/Terminal/4242-1-host_TIME123");
    QTest::newRow("no token") << QByteArray() << QByteArray();
    QTest::newRow("token") << gnomeToken << gnomeToken;
    QTest::newRow("space dropped") << QByteArray("a b") << QByteArray();
    QTest::newRow("newline dropped") << QByteArray("a\nactivate") << QByteArray();
    QTest::newRow("too long dropped") << QByteArray(300, 'x') << QByteArray();
}

void tst_SingleInstance::activationReachesPrimary()
{
    QFETCH(QByteArray, sentToken);
    QFETCH(QByteArray, receivedToken);

    SingleInstance primary(m_dir->path(), testKey());
    QVERIFY(primary.tryBecomePrimary());
    QSignalSpy activated(&primary, &SingleInstance::activationRequested);

    auto result = activateAsync(m_dir->path(), sentToken);
    QTRY_VERIFY(isReady(result));
    QVERIFY(result.get());
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toByteArray(), receivedToken);
}

void tst_SingleInstance::malformedMessageIsIgnored_data()
{
    QTest::addColumn<QByteArray>("message");

    QTest::newRow("unknown command") << QByteArray("quit\n");
    QTest::newRow("control char in token") << QByteArray("activate a\x01z\n");
    QTest::newRow("overlong line") << QByteArray(600, 'x');
}

void tst_SingleInstance::malformedMessageIsIgnored()
{
    QFETCH(QByteArray, message);

    SingleInstance primary(m_dir->path(), testKey());
    QVERIFY(primary.tryBecomePrimary());
    QSignalSpy activated(&primary, &SingleInstance::activationRequested);

    auto closedByPrimary = sendRawAsync(primary.socketPath(), message);
    QTRY_VERIFY(isReady(closedByPrimary));
    QVERIFY(closedByPrimary.get());
    QCOMPARE(activated.count(), 0);
}

void tst_SingleInstance::activationFailsWithoutPrimary()
{
    const SingleInstance client(m_dir->path(), testKey());
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!client.activatePrimary({}, 300ms));
    QVERIFY(timer.elapsed() < 2000);
}

void tst_SingleInstance::leftoverSocketFileIsReplaced()
{
    SingleInstance primary(m_dir->path(), testKey());
    {
        // What a crashed primary leaves behind.
        QFile stale(primary.socketPath());
        QVERIFY(stale.open(QIODevice::WriteOnly));
    }
    QVERIFY(primary.tryBecomePrimary());
    QSignalSpy activated(&primary, &SingleInstance::activationRequested);

    auto result = activateAsync(m_dir->path(), {});
    QTRY_VERIFY(isReady(result));
    QVERIFY(result.get());
    QCOMPARE(activated.count(), 1);
}

QTEST_GUILESS_MAIN(tst_SingleInstance)
#include "tst_singleinstance.moc"
