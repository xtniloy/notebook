#include "ui/SidebarDelegate.h"

#include "models/SidebarRoles.h"
#include "ui/Icons.h"

#include <QPainter>

#include <optional>

namespace notes::ui {

namespace {

using models::SidebarItemKind;

constexpr int kIconSize = 16;
constexpr int kPadding = 6;
constexpr qreal kHighlightRadius = 6.0;

SidebarItemKind kindOf(const QModelIndex& index)
{
    return static_cast<SidebarItemKind>(index.data(models::kKindRole).toInt());
}

bool isHeader(SidebarItemKind kind)
{
    return kind == SidebarItemKind::AccountHeader || kind == SidebarItemKind::SectionHeader;
}

// No `default:` so the compiler flags a new kind without an icon decision.
std::optional<Icon> iconFor(SidebarItemKind kind)
{
    switch (kind) {
    case SidebarItemKind::AccountHeader:
        return Icon::OnMyComputer;
    case SidebarItemKind::SectionHeader:
        return std::nullopt;
    case SidebarItemKind::AllNotes:
    case SidebarItemKind::Folder:
        return Icon::Folder;
    case SidebarItemKind::Tag:
        return Icon::Tag;
    case SidebarItemKind::RecentlyDeleted:
        return Icon::Trash;
    }
    return std::nullopt;
}

} // namespace

void SidebarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    const SidebarItemKind kind = kindOf(index);
    const bool header = isHeader(kind);
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const QPalette& palette = option.palette;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect.adjusted(2, 1, -kPadding, -1);
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.highlight());
        painter->drawRoundedRect(rect, kHighlightRadius, kHighlightRadius);
    }
    rect.adjust(kPadding, 0, -kPadding, 0);

    QFont font = option.font;
    if (header) {
        font.setWeight(QFont::DemiBold);
        font.setPointSizeF(font.pointSizeF() * 0.9);
    }
    painter->setFont(font);

    if (const std::optional<Icon> iconId = iconFor(kind)) {
        const QRect iconRect(rect.left(), rect.center().y() - (kIconSize / 2) + 1, kIconSize,
                             kIconSize);
        icon(*iconId).paint(painter, iconRect, Qt::AlignCenter,
                            selected ? QIcon::Selected : QIcon::Normal);
        rect.setLeft(iconRect.right() + kPadding + 1);
    }

    QPalette::ColorRole textRole = header ? QPalette::PlaceholderText : QPalette::Text;
    if (selected) {
        textRole = QPalette::HighlightedText;
    }
    const QColor textColor = palette.color(textRole);

    // The count is drawn first so a long name is elided before it, not under it.
    const QVariant count = index.data(models::kCountRole);
    if (count.isValid()) {
        const QString countText = QString::number(count.toInt());
        painter->setPen(selected ? textColor : palette.color(QPalette::PlaceholderText));
        painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter, countText);
        rect.setRight(rect.right() - painter->fontMetrics().horizontalAdvance(countText) -
                      kPadding);
    }

    painter->setPen(textColor);
    const QString name = painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                           Qt::ElideRight, rect.width());
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, name);

    painter->restore();
}

QSize SidebarDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int height = isHeader(kindOf(index)) ? kHeaderHeight : kRowHeight;
    return {option.rect.width(), height};
}

} // namespace notes::ui
