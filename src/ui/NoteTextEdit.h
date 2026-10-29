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
};

} // namespace notes::ui
