#include "ui/MainWindow.h"

#include "app/AppContext.h"
#include "app/Config.h"
#include "ui/dialogs/AboutDialog.h"

#include <QMenuBar>

namespace notes::ui {

MainWindow::MainWindow(app::AppContext& context, QWidget* parent)
    : QMainWindow(parent)
    , m_context(context)
{
    setWindowTitle(QString::fromLatin1(config::kAppName));
    resize(1100, 720);

    // Moves into the ActionRegistry in task 1.4.
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* about = helpMenu->addAction(tr("&About %1").arg(windowTitle()));
    about->setObjectName(QStringLiteral("aboutAction"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    auto* dialog = new AboutDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

} // namespace notes::ui
