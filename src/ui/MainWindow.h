#pragma once

#include <QMainWindow>

namespace notes::app {
class AppContext;
}

namespace notes::ui {

// Three-pane main window (Plan §3.1). Empty until task 1.3.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(app::AppContext& context, QWidget* parent = nullptr);

private:
    app::AppContext& m_context;
};

} // namespace notes::ui
