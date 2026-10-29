#include "ui/MainWindow.h"

#include "app/AppContext.h"
#include "app/Config.h"

namespace notes::ui {

MainWindow::MainWindow(app::AppContext& context, QWidget* parent)
    : QMainWindow(parent)
    , m_context(context)
{
    setWindowTitle(QString::fromLatin1(config::kAppName));
    resize(1100, 720);
}

} // namespace notes::ui
