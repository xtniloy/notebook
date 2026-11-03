#pragma once

#include <QDialog>

namespace notes::ui {

// Shown the first time a note is deleted, as in macOS Notes: deleted notes go
// to Recently Deleted and can be recovered for 30 days. OK (or Enter) accepts,
// and the note moves; Escape rejects, and nothing is deleted. A rounded,
// shadowed panel without a title bar, centered over its parent.
class TrashNoticeDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int kWidth = 300;          // the panel, without the shadow
    static constexpr int kRadius = 12;          // as the main window's corners
    static constexpr int kShadowSize = 20;      // as the main window's shadow
    static constexpr qreal kShadowAlpha = 0.18; // darker than the window's: it floats above it
    static constexpr int kIconSize = 64;

    explicit TrashNoticeDialog(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
};

} // namespace notes::ui
