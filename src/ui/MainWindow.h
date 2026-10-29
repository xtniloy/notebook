#pragma once

#include <QMainWindow>

namespace notes::ui {

// Three-pane main window (Plan §3.1). Empty until task 1.3.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace notes::ui
