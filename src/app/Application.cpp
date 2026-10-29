#include "app/Application.h"

#include "app/AppContext.h"
#include "app/AppIdentity.h"
#include "app/Config.h"
#include "app/SingleInstance.h"
#include "ui/MainWindow.h"

#include <QMessageBox>

#include <cstdlib>

namespace notes::app {

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    applyAppIdentity();
    setDesktopFileName(QString::fromLatin1(config::kAppId));
}

Application::~Application() = default;

int Application::run()
{
    auto* instance = new SingleInstance(SingleInstance::defaultDirectory(),
                                        QString::fromLatin1(config::kAppId), this);
    if (!instance->tryBecomePrimary()) {
        if (instance->activatePrimary(qgetenv("XDG_ACTIVATION_TOKEN"))) {
            return EXIT_SUCCESS;
        }
        QMessageBox::warning(nullptr, applicationName(),
                             tr("%1 is already running but is not responding.\n"
                                "Close it and try again.")
                                 .arg(applicationName()));
        return EXIT_FAILURE;
    }

    m_context = AppContext::createDefault();
    m_window = std::make_unique<ui::MainWindow>(*m_context);
    connect(instance, &SingleInstance::activationRequested, this, &Application::activateMainWindow);
    m_window->show();
    return exec();
}

void Application::activateMainWindow(const QByteArray& activationToken)
{
    // On Wayland a window may only take focus with a token from the launcher.
    // Qt's Wayland plugin uses (and clears) this variable on the next activateWindow().
    if (!activationToken.isEmpty()) {
        qputenv("XDG_ACTIVATION_TOKEN", activationToken);
    }
    m_window->setWindowState(m_window->windowState() & ~Qt::WindowMinimized);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

} // namespace notes::app
