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
    // A QPersistentModelIndex property on the view: the row a drag would drop
    // onto, drawn with an accent outline (task 2.12).
    static constexpr char kDropTargetProperty[] = "dropTarget";

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    // Whether `index` is the view's kDropTargetProperty.
    [[nodiscard]] static bool isDropTarget(const QStyleOptionViewItem& option,
                                           const QModelIndex& index);
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
};

} // namespace notes::ui
