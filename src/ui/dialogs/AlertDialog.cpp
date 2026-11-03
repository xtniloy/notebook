#include "ui/dialogs/AlertDialog.h"

#include "ui/Icons.h"
#include "ui/Shadow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace notes::ui {

namespace {

constexpr int kPadding = 24;       // panel edge to its content
constexpr int kIconGap = 16;       // icon to title
constexpr int kTextGap = 10;       // title to message
constexpr int kButtonGap = 22;     // message to buttons
constexpr int kButtonSpacing = 8;  // between buttons
constexpr int kBorderAlpha = 28;   // as the main window's edge (WindowChrome)
constexpr qreal kTitleScale = 1.2; // title font, relative to the body text

} // namespace

AlertDialog::AlertDialog(const QString& title, const QString& message, QWidget* parent)
    : QDialog(parent)
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

    auto* layout = new QVBoxLayout(this);
    constexpr int kOuter = kShadowSize + kPadding;
    layout->setContentsMargins(kOuter, kOuter, kOuter, kOuter);
    layout->setSpacing(0);
    layout->addWidget(icon);
    layout->addSpacing(kIconGap);
    layout->addWidget(titleLabel);
    layout->addSpacing(kTextGap);
    layout->addWidget(messageLabel);
    layout->addSpacing(kButtonGap);
    layout->addLayout(m_buttons);

    fitHeight();
}

QPushButton* AlertDialog::addButton(const QString& text, ButtonRole role)
{
    auto* button = new QPushButton(text, this);
    const bool cancel = role == ButtonRole::Cancel;
    button->setProperty("alertRole", cancel ? QStringLiteral("cancel") : QStringLiteral("accept"));
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(button, &QPushButton::clicked, this, cancel ? &QDialog::reject : &QDialog::accept);

    // The default: Cancel if there is one, else the first accept button.
    QPushButton* current = nullptr;
    for (QPushButton* other : findChildren<QPushButton*>()) {
        if (other != button && other->isDefault()) {
            current = other;
        }
    }
    if (current == nullptr || (cancel && current->property("alertRole").toString() != u"cancel")) {
        if (current != nullptr) {
            current->setDefault(false);
        }
        button->setDefault(true);
    }

    m_buttons->addWidget(button);
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
}

} // namespace notes::ui
