#pragma once

#include <QTextEdit>

namespace notes::ui {

// The note body editor. Plain text for now; paragraph styles, lists, tables and
// images arrive in Phase 3 (Plan §3.5).
class NoteTextEdit : public QTextEdit
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NoteTextEdit)

public:
    explicit NoteTextEdit(QWidget* parent = nullptr);
    ~NoteTextEdit() override;

    // Space around the text (UI review: the margins should look even). `right`
    // is between the text and the vertical scroll bar, which sits at the
    // widget's right edge and always has its room reserved, so the text doesn't
    // re-wrap when a note becomes long enough to scroll. `bottom` is below the
    // last line, reached by scrolling to the end.
    void setTextPadding(int right, int bottom);
    // From the text to the widget's right edge: the padding plus the scroll bar.
    [[nodiscard]] int rightInset() const;

    // setPlainText(), keeping the bottom padding (a new text resets it).
    void setNoteText(const QString& text);

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateViewportMargins();
    void applyBottomPadding();

    int m_rightPadding = 0;
    int m_bottomPadding = 0;
};

} // namespace notes::ui
