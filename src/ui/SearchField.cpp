#include "ui/SearchField.h"

#include "ui/Icons.h"

#include <QAction>
#include <QPainter>

namespace notes::ui {

namespace {
constexpr int kIconSize = 16;
constexpr int kIconGap = 6;
} // namespace

SearchField::SearchField(QWidget* parent)
    : QLineEdit(parent)
    , m_searchIcon(addAction(icon(Icon::Search), QLineEdit::LeadingPosition))
    , m_placeholder(tr("Search"))
{
    setClearButtonEnabled(true);
    connect(this, &QLineEdit::textChanged, this, &SearchField::updateMode);
    updateMode();
}

SearchField::~SearchField() = default;

void SearchField::updateMode()
{
    m_centred = text().isEmpty() && !hasFocus();
    // Centred: paintEvent() draws icon and text itself, so hide Qt's versions.
    m_searchIcon->setVisible(!m_centred);
    setPlaceholderText(m_centred ? QString() : m_placeholder);
    update();
}

void SearchField::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    updateMode();
}

void SearchField::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    updateMode();
}

void SearchField::paintEvent(QPaintEvent* event)
{
    QLineEdit::paintEvent(event);
    if (!m_centred) {
        return;
    }
    QPainter painter(this);
    const QFontMetrics metrics = fontMetrics();
    const int textWidth = metrics.horizontalAdvance(m_placeholder);
    const int total = kIconSize + kIconGap + textWidth;
    const int left = (width() - total) / 2;
    const QRect iconRect(left, (height() - kIconSize) / 2, kIconSize, kIconSize);
    icon(Icon::Search).paint(&painter, iconRect, Qt::AlignCenter, QIcon::Disabled);
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(QRect(iconRect.right() + kIconGap + 1, 0, textWidth + 1, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, m_placeholder);
}

} // namespace notes::ui
