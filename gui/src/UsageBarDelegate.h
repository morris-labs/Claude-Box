#pragma once
#include <QStyledItemDelegate>

// Paints a filled progress bar for a float value in [0, 100] read from
// BoxTableModel::StatsRole on the model index. Values < 0 (no sample yet)
// render as an empty, dimmed bar with no fill or label.
class UsageBarDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit UsageBarDelegate(QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
};
