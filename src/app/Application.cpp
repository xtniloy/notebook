#include "app/Application.h"

#include "app/AppContext.h"
#include "app/AppIdentity.h"
#include "app/Config.h"
#include "app/SingleInstance.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"

#include <QLoggingCategory>
#include <QMessageBox>

#include <cstdlib>

namespace notes::app {

namespace {

Q_LOGGING_CATEGORY(lcApp, "notes.app")

void loadFonts()
{
    const QStringList failed = ui::loadBundledFonts();
    if (!failed.isEmpty()) {
        qCWarning(lcApp) << "Falling back to system fonts; could not load" << failed;
    }
    // Keep the system's size, swap the family (Plan §3.6).
    QFont uiFont = QApplication::font();
    uiFont.setFamilies({QString::fromLatin1(ui::kUiFontFamily)});
    QApplication::setFont(uiFont);
}

} // namespace

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    applyAppIdentity();
    setApplicationDisplayName(QString::fromLatin1(config::kDisplayName));
    setDesktopFileName(QString::fromLatin1(config::kAppId));
    // Window icon. GNOME's dock takes its icon from the installed .desktop file
    // (kAppId) instead, which packaging adds in Phase 11.
    setWindowIcon(ui::appIcon());
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
        QMessageBox::warning(nullptr, applicationDisplayName(),
                             tr("%1 is already running but is not responding.\n"
                                "Close it and try again.")
                                 .arg(applicationDisplayName()));
        return EXIT_FAILURE;
    }

    // After the single-instance check, so a second launch doesn't pay for it.
    loadFonts();
    m_context = AppContext::createDefault();
    // Waits for the migration job only: at startup, before any window, and
    // needed to decide whether the window may open at all.
    QFuture<storage::DbResult<storage::Migrator::Result>> ready =
        m_context->openDatabase(AppContext::defaultDatabasePath());
    ready.waitForFinished();
    if (const std::optional<QString> failure = AppContext::openFailureMessage(ready.result())) {
        QMessageBox::critical(nullptr, applicationDisplayName(), *failure);
        return EXIT_FAILURE;
    }
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
