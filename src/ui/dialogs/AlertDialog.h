#pragma once

#include <QDialog>

#include <cstdint>

class QHBoxLayout;
class QPushButton;

namespace notes::ui {

// A short message with a choice, as macOS Notes' alerts: the app logo, a bold
// title, a line of explanation and a row of full-width buttons, on a rounded,
// shadowed panel without a title bar, centered over its parent. Used for the
// first-delete notice and for deleting a note permanently.
//
// Escape rejects. Enter presses the default button: Cancel when there is one
// (nothing irreversible by accident), otherwise the accept button.
class AlertDialog : public QDialog
{
    Q_OBJECT

public:
    enum class ButtonRole : std::uint8_t {
        Accept, // accent colored, accepts
        Cancel, // neutral, rejects
    };

    static constexpr int kWidth = 300;          // the panel, without the shadow
    static constexpr int kRadius = 12;          // as the main window's corners
    static constexpr int kShadowSize = 20;      // as the main window's shadow
    static constexpr qreal kShadowAlpha = 0.18; // darker than the window's: it floats above it
    static constexpr int kIconSize = 64;

    AlertDialog(const QString& title, const QString& message, QWidget* parent = nullptr);

    // Buttons appear left to right in the order added; style: theme.qss.
    QPushButton* addButton(const QString& text, ButtonRole role);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // The wrapped text decides the height.
    void fitHeight();

    QHBoxLayout* m_buttons;
};

} // namespace notes::ui
