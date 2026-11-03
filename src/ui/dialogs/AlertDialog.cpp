#include "ui/dialogs/AlertDialog.h"

#include "ui/Icons.h"
#include "ui/Shadow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace notes::ui {

namespace {

constexpr int kPadding = 24;       // panel edge to its content
constexpr int kIconGap = 16;       // icon to title
constexpr int kTextGap = 10;       // title to message
constexpr int kButtonGap = 22;     // message (or text field) to buttons
constexpr int kFieldGap = 16;      // message to text field
constexpr int kButtonSpacing = 8;  // between buttons
constexpr int kBorderAlpha = 28;   // as the main window's edge (WindowChrome)
constexpr qreal kTitleScale = 1.2; // title font, relative to the body text

} // namespace

AlertDialog::AlertDialog(const QString& title, const QString& message, QWidget* parent)
    : QDialog(parent)
    , m_content(new QVBoxLayout(this))
    , m_buttons(new QHBoxLayout)
{
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setWindowTitle(title);

    auto* icon = new QLabel(this);
    icon->setPixmap(appIcon().pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
    icon->setAlignment(Qt::AlignHCenter);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("alertTitle"));
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * kTitleScale);
    titleLabel->setFont(titleFont);

    auto* messageLabel = new QLabel(message, this);
    messageLabel->setObjectName(QStringLiteral("alertMessage"));
    for (QLabel* label : {titleLabel, messageLabel}) {
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignHCenter);
    }

    m_buttons->setSpacing(kButtonSpacing);

    constexpr int kOuter = kShadowSize + kPadding;
    m_content->setContentsMargins(kOuter, kOuter, kOuter, kOuter);
    m_content->setSpacing(0);
    m_content->addWidget(icon);
    m_content->addSpacing(kIconGap);
    m_content->addWidget(titleLabel);
    m_content->addSpacing(kTextGap);
    m_content->addWidget(messageLabel);
    m_content->addSpacing(kButtonGap);
    m_content->addLayout(m_buttons);

    fitHeight();
}

QLineEdit* AlertDialog::addTextField(const QString& text)
{
    if (m_field != nullptr) { // one field per alert
        m_field->setText(text);
        return m_field;
    }
    auto* field = new QLineEdit(text, this);            // owned by the dialog
    field->setObjectName(QStringLiteral("alertField")); // style: theme.qss
    // Between the message and the buttons: before the gap above the buttons.
    const int buttonGap = m_content->indexOf(m_buttons) - 1;
    m_content->insertSpacing(buttonGap, kFieldGap);
    m_content->insertWidget(buttonGap + 1, field);
    field->selectAll();
    field->setFocus();
    connect(field, &QLineEdit::textChanged, this, &AlertDialog::updateAcceptButtons);
    m_field = field;
    fitHeight();
    return field;
}

void AlertDialog::updateAcceptButtons()
{
    const bool hasText = m_field == nullptr || !m_field->text().trimmed().isEmpty();
    for (QPushButton* button : findChildren<QPushButton*>()) {
        if (button->property("alertRole").toString() == u"accept") {
            button->setEnabled(hasText);
        }
    }
}

QPushButton* AlertDialog::addButton(const QString& text, ButtonRole role)
{
    auto* button = new QPushButton(text, this);
    const bool cancel = role == ButtonRole::Cancel;
    button->setProperty("alertRole", cancel ? QStringLiteral("cancel") : QStringLiteral("accept"));
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(button, &QPushButton::clicked, this, cancel ? &QDialog::reject : &QDialog::accept);

    // The default: with a text field the first accept button, otherwise
    // Cancel if there is one, else the first accept button.
    const bool preferCancel = m_field == nullptr;
    QPushButton* current = nullptr;
    for (QPushButton* other : findChildren<QPushButton*>()) {
        if (other != button && other->isDefault()) {
            current = other;
        }
    }
    const bool currentIsCancel =
        current != nullptr && current->property("alertRole").toString() == u"cancel";
    if (current == nullptr || (cancel == preferCancel && currentIsCancel != preferCancel)) {
        if (current != nullptr) {
            current->setDefault(false);
        }
        button->setDefault(true);
    }

    m_buttons->addWidget(button);
    updateAcceptButtons();
    fitHeight();
    return button;
}

void AlertDialog::fitHeight()
{
    const int fullWidth = kWidth + (2 * kShadowSize);
    const QLayout* content = layout();
    setFixedSize(fullWidth, content->hasHeightForWidth() ? content->heightForWidth(fullWidth)
                                                         : content->sizeHint().height());
}

void AlertDialog::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel =
        QRectF(rect()).adjusted(kShadowSize, kShadowSize, -kShadowSize, -kShadowSize);
    paintSoftShadow(painter, panel.translated(0, 2), kRadius, kShadowSize, kShadowAlpha);

    const QColor background = palette().color(QPalette::Window);
    const bool dark = background.lightness() < 128;
    painter.setPen(
        QPen(dark ? QColor(255, 255, 255, kBorderAlpha) : QColor(0, 0, 0, kBorderAlpha), 1));
    painter.setBrush(background);
    painter.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void AlertDialog::showEvent(QShowEvent* event)
{
    // Over the middle of the window it belongs to (where the platform lets
    // windows place themselves; on Wayland the compositor centers modals).
    if (const QWidget* owner = parentWidget()) {
        const QWidget* window = owner->window();
        move(window->mapToGlobal(window->rect().center()) - rect().center());
    }
    QDialog::showEvent(event);
    if (m_field != nullptr) {
        m_field->setFocus();
    }
}

} // namespace notes::ui
