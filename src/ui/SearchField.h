#pragma once

#include <QLineEdit>

class QAction;

namespace notes::ui {

// The toolbar's search box (task 1.9). Like macOS Notes, the magnifier and
// "Search" sit in the middle while the box is empty and unfocused; on focus or
// typing they move to the usual left-aligned place.
class SearchField : public QLineEdit
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SearchField)

public:
    explicit SearchField(QWidget* parent = nullptr);
    ~SearchField() override;

    // True while the centred placeholder is drawn (empty and unfocused).
    [[nodiscard]] bool isPlaceholderCentred() const { return m_centred; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void updateMode();

    QAction* m_searchIcon;
    QString m_placeholder;
    bool m_centred = false;
};

} // namespace notes::ui
