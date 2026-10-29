// TC-UI-001: starting a second instance sends an "activate" message to the running
// one, and the second process exits with code 0. Runs the real notesapp binary
// with every XDG directory redirected into a temp dir.

#include "app/Config.h"
#include "app/SingleInstance.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <vector>

using namespace std::chrono_literals;
using notes::app::SingleInstance;

class tst_SingleInstanceProcess : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void secondLaunchActivatesRunningInstance();
    void secondProcessExitsAndFirstKeepsRunning();
    void crashedInstanceIsReplaced();

private:
    // Starts notesapp; the process is killed in cleanup().
    QProcess* startApp(const QByteArray& activationToken = {});
    [[nodiscard]] QString runtimeDir() const;
    [[nodiscard]] SingleInstance instance() const;
    [[nodiscard]] bool isListening() const;

    std::unique_ptr<QTemporaryDir> m_home;
    std::vector<std::unique_ptr<QProcess>> m_processes;
};

void tst_SingleInstanceProcess::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    const QDir home(m_home->path());
    // Qt requires the runtime dir to be 0700.
    QVERIFY(home.mkdir(QStringLiteral("runtime"),
                       QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    for (const char* dir : {"config", "data", "cache"}) {
        QVERIFY(home.mkdir(QString::fromLatin1(dir)));
    }
}

void tst_SingleInstanceProcess::cleanup()
{
    for (const auto& process : m_processes) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(5000);
        }
    }
    m_processes.clear();
    m_home.reset();
}

QProcess* tst_SingleInstanceProcess::startApp(const QByteArray& activationToken)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtimeDir());
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_home->filePath(QStringLiteral("config")));
    env.insert(QStringLiteral("XDG_DATA_HOME"), m_home->filePath(QStringLiteral("data")));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), m_home->filePath(QStringLiteral("cache")));
    env.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    if (!activationToken.isEmpty()) {
        env.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), QString::fromLatin1(activationToken));
    }

    auto process = std::make_unique<QProcess>();
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->start(QStringLiteral(NOTESAPP_BINARY), {});
    m_processes.push_back(std::move(process));
    return m_processes.back().get();
}

QString tst_SingleInstanceProcess::runtimeDir() const
{
    return m_home->filePath(QStringLiteral("runtime"));
}

SingleInstance tst_SingleInstanceProcess::instance() const
{
    return {runtimeDir(), QString::fromLatin1(notes::config::kAppId)};
}

bool tst_SingleInstanceProcess::isListening() const
{
    QLocalSocket probe;
    probe.connectToServer(instance().socketPath());
    const bool connected = probe.waitForConnected(100);
    probe.abort();
    return connected;
}

void tst_SingleInstanceProcess::secondLaunchActivatesRunningInstance()
{
    // This test process plays the running instance.
    SingleInstance primary = instance();
    QVERIFY(primary.tryBecomePrimary());
    QSignalSpy activated(&primary, &SingleInstance::activationRequested);

    QProcess* second = startApp("launcher-token-42");
    QVERIFY(second->waitForStarted());
    // Not waitForFinished(): it would block the event loop the primary answers on.
    QSignalSpy finished(second, &QProcess::finished);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15s);

    QCOMPARE(second->exitStatus(), QProcess::NormalExit);
    QCOMPARE(second->exitCode(), 0);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.at(0).at(0).toByteArray(), QByteArray("launcher-token-42"));
}

void tst_SingleInstanceProcess::secondProcessExitsAndFirstKeepsRunning()
{
    QProcess* first = startApp();
    QVERIFY(first->waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(isListening(), 15s);

    QProcess* second = startApp();
    QVERIFY(second->waitForFinished(15000));
    QCOMPARE(second->exitStatus(), QProcess::NormalExit);
    QCOMPARE(second->exitCode(), 0);
    QCOMPARE(first->state(), QProcess::Running);
}

void tst_SingleInstanceProcess::crashedInstanceIsReplaced()
{
    QProcess* crashed = startApp();
    QVERIFY(crashed->waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(isListening(), 15s);
    crashed->kill(); // SIGKILL: no cleanup, lock and socket files are left behind
    QVERIFY(crashed->waitForFinished());
    QVERIFY(QFile::exists(instance().lockPath()));
    QVERIFY(!isListening());

    QProcess* replacement = startApp();
    QVERIFY(replacement->waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(isListening(), 15s);
    QCOMPARE(replacement->state(), QProcess::Running);
    QVERIFY(instance().activatePrimary({}, 5s));
}

QTEST_GUILESS_MAIN(tst_SingleInstanceProcess)
#include "tst_singleinstance_process.moc"
