#include "app/Config.h"
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    const QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(notes::config::kAppName));
    QApplication::setApplicationVersion(QString::fromLatin1(notes::config::kAppVersion));
    QGuiApplication::setDesktopFileName(QString::fromLatin1(notes::config::kAppId));

    notes::ui::MainWindow window;
    window.show();

    return QApplication::exec();
}
