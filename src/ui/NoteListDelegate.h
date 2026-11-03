#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
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
    // How strongly the lines between notes and under section headers show the
    // theme's separator color.
    static constexpr float kSeparatorOpacity = 0.5F;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

    // A note's title: Bold (700), so it clearly leads the line below.
    [[nodiscard]] static QFont titleFont(const QFont& base);
    // The time and preview line: 0.15 pt smaller than `base`, the title's size
    // (10.85 pt under an 11 pt system font), regular weight. The editor's date
    // line uses it too.
    [[nodiscard]] static QFont detailFont(const QFont& base);
    // The preview's muted color on a selected row: the selection's text color
    // at 70% over the accent, so the preview stays secondary there too while
    // keeping WCAG AA contrast (4.5:1).
    [[nodiscard]] static QColor selectedMutedColor(const QPalette& palette);

private:
    static void paintHeader(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index);
    static void paintNote(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index);
};

} // namespace notes::ui
