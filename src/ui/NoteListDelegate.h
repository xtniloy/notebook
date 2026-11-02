#pragma once

#include <QFont>
#include <QStyledItemDelegate>

namespace notes::ui {

// Paints note list rows (Plan §3.4): bold title, then, a little smaller, the time and first line of
// the preview; a thumbnail slot on the right; pin and lock icons. Locked notes
// show "Locked" instead of a preview. Section headers are bold captions with
// a little space above, separating the groups.
//
// Note rows share one height; section headers are shorter.
class NoteListDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

    // A note's title: Bold (700), so it clearly leads the line below.
    [[nodiscard]] static QFont titleFont(const QFont& base);
    // The time and preview line: 0.1 pt smaller than `base`, the title's size
    // (10.9 pt under an 11 pt system font), regular weight.
    [[nodiscard]] static QFont detailFont(const QFont& base);

private:
    static void paintHeader(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index);
    static void paintNote(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index);
};

} // namespace notes::ui
