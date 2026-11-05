#include "ui/NoteListDelegate.h"

#include "models/NoteListRoles.h"
#include "ui/Icons.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>

namespace notes::ui {

namespace {

constexpr int kHorizontalPadding = 16;
// Notes start further right than their section header, as in macOS Notes, so
// headers stand out. Headers and the hairlines between notes stay at
// kHorizontalPadding.
constexpr int kNoteIndent = 14;
constexpr int kVerticalPadding = 13; // roomy rows, as macOS Notes
constexpr int kLineGap = 5;
constexpr int kIconSize = 14;
constexpr int kIconGap = 4;
constexpr int kTimeGap = 8; // between the time and the preview
constexpr qreal kHighlightRadius = 6.0;
constexpr qreal kThumbnailRadius = 4.0;
constexpr qreal kThumbnailEdgeAlpha = 0.12;
constexpr qreal kHeaderScale = 1.1;
// Section headers ("Pinned", "Today", ...): gentle space above, a little below.
constexpr int kHeaderTopPadding = 12;
constexpr int kHeaderBottomPadding = 4;
constexpr int kHighlightInset = 6;
// The time and preview line is this much smaller than the title (detailFont()).
constexpr qreal kDetailSizeReduction = 0.15;

QFont boldFont(const QFont& base, qreal scale = 1.0, QFont::Weight weight = QFont::DemiBold)
{
    QFont font = base;
    font.setWeight(weight);
    if (scale != 1.0) {
        font.setPointSizeF(font.pointSizeF() * scale);
    }
    return font;
}

// A crisp 1 px line in the theme's separator color at half strength along the
// bottom of `rect`, inset like the row's text (UI review: full strength was too
// heavy, 35% smeared over two pixel rows barely visible). Filled, not stroked:
// an antialiased 1 px stroke on a pixel edge smears into two rows.
void drawSeparator(QPainter* painter, const QRect& rect, const QPalette& palette)
{
    QColor line = palette.color(QPalette::Mid);
    line.setAlphaF(NoteListDelegate::kSeparatorOpacity);
    painter->fillRect(QRect(rect.left() + kHorizontalPadding, rect.bottom(),
                            rect.width() - (2 * kHorizontalPadding), 1),
                      line);
}

QRect iconRect(int left, const QRect& line)
{
    return {left, line.center().y() - (kIconSize / 2) + 1, kIconSize, kIconSize};
}

// The decoded thumbnail of the note at `index`, at `side` device pixels
// square, cropped to fill it; null if there is none to decode. Cached, so
// scrolling doesn't decode again.
QPixmap thumbnailFor(const QModelIndex& index, int side)
{
    const QString key = index.data(models::kThumbnailKeyRole).toString();
    const QByteArray encoded = index.data(models::kThumbnailRole).toByteArray();
    if (key.isEmpty() || encoded.isEmpty()) {
        return {};
    }
    const QString cacheKey = QStringLiteral("noteThumbnail:%1@%2").arg(key).arg(side);
    QPixmap pixmap;
    if (QPixmapCache::find(cacheKey, &pixmap)) {
        return pixmap;
    }
    const QImage image = QImage::fromData(encoded);
    if (image.isNull()) {
        return {};
    }
    const QImage filled =
        image.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    pixmap = QPixmap::fromImage(
        filled.copy((filled.width() - side) / 2, (filled.height() - side) / 2, side, side));
    QPixmapCache::insert(cacheKey, pixmap);
    return pixmap;
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
    // The same line as between notes, under the header.
    drawSeparator(painter, option.rect, option.palette);
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
            drawSeparator(painter, option.rect, palette);
        }
    }

    QRect content = option.rect.adjusted(kHorizontalPadding + kNoteIndent, kVerticalPadding,
                                         -kHorizontalPadding, -kVerticalPadding);

    // The note's first image (task 3.7), on the right, cropped to a square.
    if (index.data(models::kHasThumbnailRole).toBool()) {
        const int side = content.height();
        const QRectF slot(content.right() - side + 1, content.top(), side, side);
        const qreal ratio = painter->device()->devicePixelRatioF();
        const QPixmap thumbnail = thumbnailFor(index, qRound(side * ratio));
        const QRectF frame = slot.adjusted(0.5, 0.5, -0.5, -0.5);
        if (thumbnail.isNull()) {
            painter->setPen(palette.color(QPalette::Mid));
            painter->setBrush(palette.color(QPalette::Midlight));
            painter->drawRoundedRect(frame, kThumbnailRadius, kThumbnailRadius);
        } else {
            QPainterPath rounded;
            rounded.addRoundedRect(slot, kThumbnailRadius, kThumbnailRadius);
            painter->save();
            painter->setClipPath(rounded);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawPixmap(slot, thumbnail, QRectF(thumbnail.rect()));
            painter->restore();
            // A hairline edge, so a white image doesn't melt into the row.
            QColor edge = palette.color(QPalette::Text);
            edge.setAlphaF(kThumbnailEdgeAlpha);
            painter->setPen(edge);
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(frame, kThumbnailRadius, kThumbnailRadius);
        }
        content.setRight(static_cast<int>(slot.left()) - kHorizontalPadding);
    }

    const QFont titleFont = NoteListDelegate::titleFont(option.font);
    const QFontMetrics titleMetrics(titleFont);
    const QFont smallFont = detailFont(option.font);
    const QFontMetrics smallMetrics(smallFont);
    QRect titleLine(content.left(), content.top(), content.width(), titleMetrics.height());
    QRect detailLine(content.left(), titleLine.bottom() + 1 + kLineGap, content.width(),
                     smallMetrics.height());

    const QColor textColor = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor mutedColor =
        selected ? selectedMutedColor(palette) : palette.color(QPalette::PlaceholderText);

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
    painter->drawText(titleLine, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight,
                                                        titleLine.width()));

    painter->setFont(smallFont);
    const QString time = index.data(models::kTimeTextRole).toString();
    // The preview starts after the time as drawn, not as measured apart from
    // the painter: on a desktop whose font DPI differs from the screen's
    // (GNOME at 125 %: Xft.dpi 192), QFontMetrics(font) measured the time
    // narrower than it was drawn and the preview touched it ("Yesterdaynot").
    QRect drawnTime;
    painter->drawText(detailLine, Qt::AlignLeft | Qt::AlignVCenter, time, &drawnTime);
    if (!time.isEmpty()) {
        detailLine.setLeft(drawnTime.right() + 1 + kTimeGap);
    }

    const QString preview =
        locked ? NoteListDelegate::tr("Locked") : index.data(models::kPreviewRole).toString();
    painter->setPen(mutedColor);
    painter->drawText(
        detailLine, Qt::AlignLeft | Qt::AlignVCenter,
        painter->fontMetrics().elidedText(preview, Qt::ElideRight, detailLine.width()));
}

QColor NoteListDelegate::selectedMutedColor(const QPalette& palette)
{
    constexpr qreal kTextShare = 0.7;
    const QColor text = palette.color(QPalette::HighlightedText);
    const QColor accent = palette.color(QPalette::Highlight);
    const auto mix = [&](qreal fg, qreal bg) {
        return (kTextShare * fg) + ((1.0 - kTextShare) * bg);
    };
    return QColor::fromRgbF(static_cast<float>(mix(text.redF(), accent.redF())),
                            static_cast<float>(mix(text.greenF(), accent.greenF())),
                            static_cast<float>(mix(text.blueF(), accent.blueF())));
}

QFont NoteListDelegate::titleFont(const QFont& base)
{
    return boldFont(base, 1.0, QFont::Bold);
}

QFont NoteListDelegate::detailFont(const QFont& base)
{
    QFont font = base;
    if (font.pointSizeF() > kDetailSizeReduction) { // -1 for pixel-sized fonts
        font.setPointSizeF(font.pointSizeF() - kDetailSizeReduction);
    }
    return font;
}

QSize NoteListDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // Every note row has the same height; headers are compact (UI review: full
    // note-height headers left a large gap above "Today", "Yesterday", ...).
    if (index.data(models::kIsSectionHeaderRole).toBool()) {
        return {0, QFontMetrics(boldFont(option.font, kHeaderScale)).height() + kHeaderTopPadding +
                       kHeaderBottomPadding};
    }
    const int height = QFontMetrics(titleFont(option.font)).height() + kLineGap +
                       QFontMetrics(detailFont(option.font)).height() + (2 * kVerticalPadding);
    return {0, height};
}

} // namespace notes::ui
