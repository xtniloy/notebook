#pragma once

#include <QLocale>
#include <QWidget>

class QDateTime;
class QLabel;
class QStackedWidget;

namespace notes::ui {

class NoteTextEdit;

// The editor pane (Plan §3.5): the note's date and time centered in light gray,
// above the note text. Shows an empty state when no note is selected, and a
// placeholder for locked notes until the lock screen (task 6.4).
class EditorView : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(EditorView)

public:
    explicit EditorView(QWidget* parent = nullptr);
    ~EditorView() override;

    void showNote(const QString& text, const QDateTime& modified);
    // The date stays visible; the content never reaches the editor.
    void showLockedNote(const QDateTime& modified);
    // Empty state: "No note selected", read-only editor.
    void clear();

    [[nodiscard]] bool hasNote() const { return m_hasNote; }
    [[nodiscard]] NoteTextEdit* textEdit() const { return m_textEdit; }

    // "Thursday, October 28, 2026 at 9:41 PM" in English.
    [[nodiscard]] static QString headerText(const QDateTime& modified,
                                            const QLocale& locale = QLocale());

private:
    void showPlaceholder(const QString& message);

    QLabel* m_header;
    QStackedWidget* m_pages;
    NoteTextEdit* m_textEdit;
    QLabel* m_placeholder;
    bool m_hasNote = false;
};

} // namespace notes::ui
