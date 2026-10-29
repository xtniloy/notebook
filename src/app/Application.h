#pragma once

#include <QApplication>

#include <memory>

namespace notes::ui {
class MainWindow;
}

namespace notes::app {

class AppContext;

// Process bootstrap: app identity, single-instance guard, services, main window.
class Application : public QApplication
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Application)

public:
    Application(int& argc, char** argv);
    ~Application() override;

    // Runs the app and returns the process exit code. If another instance is
    // already running, asks it to show its window and returns 0 without opening one.
    int run();

private:
    void activateMainWindow(const QByteArray& activationToken);

    // Declaration order matters: the window is destroyed before the context it uses.
    std::unique_ptr<AppContext> m_context;
    std::unique_ptr<ui::MainWindow> m_window;
};

} // namespace notes::app
