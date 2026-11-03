#include "ui/dialogs/TrashNoticeDialog.h"

#include "ui/Icons.h"
#include "ui/Shadow.h"

#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace notes::ui {

namespace {

constexpr int kPadding = 24;       // panel edge to its content
constexpr int kIconGap = 16;       // icon to title
constexpr int kTextGap = 10;       // title to message
constexpr int kButtonGap = 22;     // message to button
constexpr int kBorderAlpha = 28;   // as the main window's edge (WindowChrome)
constexpr qreal kTitleScale = 1.2; // title font, relative to the body text

} // namespace

TrashNoticeDialog::TrashNoticeDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("trashNoticeDialog"));
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setWindowTitle(tr("Recently Deleted"));

    auto* icon = new QLabel(this);
    icon->setPixmap(appIcon().pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
    icon->setAlignment(Qt::AlignHCenter);

    auto* title = new QLabel(tr("Deleted notes are moved to the Recently Deleted folder."), this);
    title->setObjectName(QStringLiteral("trashNoticeTitle"));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * kTitleScale);
    title->setFont(titleFont);

    auto* message = new QLabel(
        tr("You can recover or remove notes from Recently Deleted within 30 days."), this);
    message->setObjectName(QStringLiteral("trashNoticeMessage"));
    for (QLabel* label : {title, message}) {
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignHCenter);
    }

    auto* ok = new QPushButton(tr("OK"), this);
    ok->setObjectName(QStringLiteral("trashNoticeOk")); // accent colors: theme.qss
    ok->setDefault(true);
    ok->setCursor(Qt::PointingHandCursor);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    constexpr int kOuter = kShadowSize + kPadding;
    layout->setContentsMargins(kOuter, kOuter, kOuter, kOuter);
    layout->setSpacing(0);
    layout->addWidget(icon);
    layout->addSpacing(kIconGap);
    layout->addWidget(title);
    layout->addSpacing(kTextGap);
    layout->addWidget(message);
    layout->addSpacing(kButtonGap);
    layout->addWidget(ok);

    // The wrapped text decides the height.
    const int fullWidth = kWidth + (2 * kShadowSize);
    setFixedSize(fullWidth, layout->hasHeightForWidth() ? layout->heightForWidth(fullWidth)
                                                        : layout->sizeHint().height());
}

void TrashNoticeDialog::paintEvent(QPaintEvent* /*event*/)
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

void TrashNoticeDialog::showEvent(QShowEvent* event)
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
