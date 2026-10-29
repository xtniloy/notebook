#pragma once

#include <QStyledItemDelegate>

namespace notes::ui {

// Paints sidebar rows: section headers as small muted captions, other rows with
// an icon, the name, a rounded selection highlight and a right-aligned note
// count badge (models::kCountRole).
class SidebarDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static constexpr int kRowHeight = 28;
    static constexpr int kHeaderHeight = 30;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
};

} // namespace notes::ui
