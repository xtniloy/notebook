#pragma once

#include <QStyledItemDelegate>

namespace notes::ui {

// Paints note list rows (Plan §3.4): bold title, then the time and first line of
// the preview; a thumbnail slot on the right; pin and lock icons. Locked notes
// show "Locked" instead of a preview. Section headers are bold captions sitting
// at the bottom of their row, so the space above separates the groups.
//
// Every row has the same height, which lets the view skip per-row layout.
class NoteListDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

private:
    static void paintHeader(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index);
    static void paintNote(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index);
};

} // namespace notes::ui
