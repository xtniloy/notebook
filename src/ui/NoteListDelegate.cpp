#include "ui/NoteListDelegate.h"

#include "models/NoteListRoles.h"
#include "ui/Icons.h"

#include <QPainter>

namespace notes::ui {

namespace {

constexpr int kHorizontalPadding = 16;
constexpr int kVerticalPadding = 13; // roomy rows, as macOS Notes
constexpr int kLineGap = 5;
constexpr int kIconSize = 14;
constexpr int kIconGap = 4;
constexpr qreal kHighlightRadius = 6.0;
constexpr qreal kThumbnailRadius = 4.0;
constexpr qreal kHeaderScale = 1.1;
// Section headers ("Pinned", "Today", ...): gentle space above, a little below.
constexpr int kHeaderTopPadding = 12;
constexpr int kHeaderBottomPadding = 4;
constexpr int kHighlightInset = 6;

QFont boldFont(const QFont& base, qreal scale = 1.0)
{
    QFont font = base;
    font.setWeight(QFont::DemiBold);
    if (scale != 1.0) {
        font.setPointSizeF(font.pointSizeF() * scale);
    }
    return font;
}

QRect iconRect(int left, const QRect& line)
{
    return {left, line.center().y() - (kIconSize / 2) + 1, kIconSize, kIconSize};
}

} // namespace

void NoteListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    painter->save();
    if (index.data(models::kIsSectionHeaderRole).toBool()) {
        paintHeader(painter, option, index);
    } else {
        paintNote(painter, option, index);
    }
    painter->restore();
}

void NoteListDelegate::paintHeader(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index)
{
    painter->setFont(boldFont(option.font, kHeaderScale));
    // Headers are disabled rows (so selection skips them); draw them in the
    // normal text color, not the disabled one.
    painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
    const QRect rect =
        option.rect.adjusted(kHorizontalPadding, 0, -kHorizontalPadding, -kHeaderBottomPadding);
    painter->drawText(
        rect, Qt::AlignLeft | Qt::AlignBottom,
        painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, rect.width()));
}

void NoteListDelegate::paintNote(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index)
{
    const QPalette& palette = option.palette;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool locked = index.data(models::kLockedRole).toBool();
    painter->setRenderHint(QPainter::Antialiasing);

    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.highlight());
        painter->drawRoundedRect(option.rect.adjusted(kHighlightInset, 1, -kHighlightInset, -1),
                                 kHighlightRadius, kHighlightRadius);
    } else {
        // Hairline between notes, but not above the next section's header.
        const QModelIndex next = index.siblingAtRow(index.row() + 1);
        if (next.isValid() && !next.data(models::kIsSectionHeaderRole).toBool()) {
            QColor line = palette.color(QPalette::Mid);
            line.setAlphaF(0.35F);
            painter->setPen(line);
            const int y = option.rect.bottom();
            painter->drawLine(option.rect.left() + kHorizontalPadding, y,
                              option.rect.right() - kHorizontalPadding, y);
        }
    }

    QRect content = option.rect.adjusted(kHorizontalPadding, kVerticalPadding, -kHorizontalPadding,
                                         -kVerticalPadding);

    // Thumbnail slot; the image itself arrives with attachments (task 3.x).
    if (index.data(models::kHasThumbnailRole).toBool()) {
        const int side = content.height();
        const QRectF slot(content.right() - side + 1, content.top(), side, side);
        painter->setPen(palette.color(QPalette::Mid));
        painter->setBrush(palette.color(QPalette::Midlight));
        painter->drawRoundedRect(slot.adjusted(0.5, 0.5, -0.5, -0.5), kThumbnailRadius,
                                 kThumbnailRadius);
        content.setRight(static_cast<int>(slot.left()) - kHorizontalPadding);
    }

    const QFont titleFont = boldFont(option.font);
    const QFontMetrics titleMetrics(titleFont);
    QRect titleLine(content.left(), content.top(), content.width(), titleMetrics.height());
    QRect detailLine(content.left(), titleLine.bottom() + 1 + kLineGap, content.width(),
                     option.fontMetrics.height());

    const QColor textColor = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor mutedColor = selected ? textColor : palette.color(QPalette::PlaceholderText);

    const QIcon::Mode iconMode = selected ? QIcon::Selected : QIcon::Normal;
    if (locked) {
        icon(Icon::Lock)
            .paint(painter, iconRect(titleLine.left(), titleLine), Qt::AlignCenter, iconMode);
        titleLine.setLeft(titleLine.left() + kIconSize + kIconGap);
    }
    if (index.data(models::kPinnedRole).toBool()) {
        icon(Icon::Pin).paint(painter, iconRect(titleLine.right() - kIconSize + 1, titleLine),
                              Qt::AlignCenter, iconMode);
        titleLine.setRight(titleLine.right() - kIconSize - kIconGap);
    }

    painter->setFont(titleFont);
    painter->setPen(textColor);
    painter->drawText(
        titleLine, Qt::AlignLeft | Qt::AlignVCenter,
        titleMetrics.elidedText(index.data().toString(), Qt::ElideRight, titleLine.width()));

    painter->setFont(option.font);
    const QString time = index.data(models::kTimeTextRole).toString();
    painter->drawText(detailLine, Qt::AlignLeft | Qt::AlignVCenter, time);
    detailLine.setLeft(detailLine.left() + option.fontMetrics.horizontalAdvance(time) +
                       (2 * kIconGap));

    const QString preview =
        locked ? NoteListDelegate::tr("Locked") : index.data(models::kPreviewRole).toString();
    painter->setPen(mutedColor);
    painter->drawText(detailLine, Qt::AlignLeft | Qt::AlignVCenter,
                      option.fontMetrics.elidedText(preview, Qt::ElideRight, detailLine.width()));
}

QSize NoteListDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // Every note row has the same height; headers are compact (UI review: full
    // note-height headers left a large gap above "Today", "Yesterday", ...).
    if (index.data(models::kIsSectionHeaderRole).toBool()) {
        return {0, QFontMetrics(boldFont(option.font, kHeaderScale)).height() + kHeaderTopPadding +
                       kHeaderBottomPadding};
    }
    const int height = QFontMetrics(boldFont(option.font)).height() + kLineGap +
                       option.fontMetrics.height() + (2 * kVerticalPadding);
    return {0, height};
}

} // namespace notes::ui
