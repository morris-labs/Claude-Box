#include "UsageBarDelegate.h"

#include "BoxTableModel.h"
#include "Theme.h"

#include <QApplication>
#include <QModelIndex>
#include <QPainter>
#include <QStyleOptionViewItem>

namespace {

// Linear interpolation between two colors based on t in [0, 1].
QColor blendColors(const QColor &a, const QColor &b, double t)
{
    return QColor(
        int(a.red()   + (b.red()   - a.red())   * t),
        int(a.green() + (b.green() - a.green()) * t),
        int(a.blue()  + (b.blue()  - a.blue())  * t));
}

// Color shifts from green (low load) through yellow to red (high load).
QColor barColor(double fraction)
{
    // 0-0.6: green; 0.6-1.0: blend toward red
    const QColor green(45, 160, 80);
    const QColor red(200, 80, 50);
    if (fraction <= 0.6)
        return green;
    return blendColors(green, red, (fraction - 0.6) / 0.4);
}

} // namespace

UsageBarDelegate::UsageBarDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void UsageBarDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    // Draw the standard item background (handles selection highlight).
    QStyledItemDelegate::paint(painter, option, index);

    const QVariant v = index.data(BoxTableModel::StatsRole);
    if (!v.isValid())
        return;

    const double value = v.toFloat();
    const double fraction = qBound(0.0, value / 100.0, 1.0);

    const QRect cell = option.rect;
    const QRect bar  = cell.adjusted(4, 4, -4, -4);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    constexpr double kRadius = 4.0;

    if (value < 0.0) {
        // No sample yet -- draw a dimmed empty pill outline only.
        painter->setPen(Theme::border());
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(bar), kRadius, kRadius);
        painter->restore();
        return;
    }

    // Background track.
    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::panelBg());
    painter->drawRoundedRect(QRectF(bar), kRadius, kRadius);

    // Filled portion, clipped to bar bounds so the left end of the chunk
    // inherits the bar's rounded corners without per-pixel math.
    if (fraction > 0.0) {
        painter->save();
        painter->setClipRect(bar);
        QRectF fill(bar.x(), bar.y(), bar.width() * fraction, bar.height());
        painter->setBrush(barColor(fraction));
        painter->drawRoundedRect(fill, kRadius, kRadius);
        painter->restore();
    }

    // Percentage label over the bar.
    painter->setPen(Qt::white);
    QFont f = option.font;
    f.setPointSizeF(f.pointSizeF() - 1.0);
    painter->setFont(f);
    painter->drawText(bar, Qt::AlignCenter,
                      QStringLiteral("%1%").arg(int(value + 0.5)));

    painter->restore();
}

QSize UsageBarDelegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    Q_UNUSED(index);
    // option.rect is typically invalid/zero in sizeHint; use font metrics
    // for the row height rather than the (often-zero) option.rect.height().
    return QSize(80, option.fontMetrics.height() + 6);
}
