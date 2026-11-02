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

    // Empty space between the text and the widget's right edge. The vertical
    // scroll bar sits at that edge, inside this space, and always has its room
    // reserved, so the text doesn't re-wrap when a note becomes long enough to
    // scroll (UI review: the scroll bar sat right against the text).
    void setRightPadding(int padding);
    [[nodiscard]] int rightPadding() const { return m_rightPadding; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateViewportMargins();

    int m_rightPadding = 0;
};

} // namespace notes::ui
