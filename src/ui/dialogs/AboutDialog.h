#pragma once

#include <QDialog>

namespace notes::ui {

// Help → About: version, Qt notice (LGPLv3), and bundled third-party licenses.
// A stub until the app has its own name and branding.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace notes::ui
