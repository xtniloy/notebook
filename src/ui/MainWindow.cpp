#include "ui/MainWindow.h"

#include "app/Config.h"

namespace notes::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromLatin1(config::kAppName));
    resize(1100, 720);
}

} // namespace notes::ui
