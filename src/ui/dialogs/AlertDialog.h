#pragma once

#include <QDialog>

#include <cstdint>

class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace notes::ui {

// A short message with a choice, as macOS Notes' alerts: the app logo, a bold
// title, a line of explanation and a row of full-width buttons, on a rounded,
// shadowed panel without a title bar, centered over its parent. Used for the
// first-delete notice, deleting a note permanently, and deleting, creating and
// renaming folders (with a text field for the name).
//
// Escape rejects. Enter presses the default button: with a text field, the
// accept button (typing a name and pressing Enter creates it); otherwise
// Cancel when there is one (nothing irreversible by accident).
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

    // A one-line text field under the message, focused and fully selected
    // when shown. Add it before the buttons: while it holds only spaces, the
    // accept buttons are disabled.
    QLineEdit* addTextField(const QString& text);

    // Buttons appear left to right in the order added; style: theme.qss.
    QPushButton* addButton(const QString& text, ButtonRole role);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // The wrapped text decides the height.
    void fitHeight();

    // Enables the accept buttons when the text field has text.
    void updateAcceptButtons();

    QVBoxLayout* m_content;
    QHBoxLayout* m_buttons;
    QLineEdit* m_field = nullptr;
};

} // namespace notes::ui
