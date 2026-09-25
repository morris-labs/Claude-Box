#include "BoxTableModel.h"

#include "Icons.h"
#include "Theme.h"

#include <QBrush>

namespace {
constexpr int ColStatus       = 0;
constexpr int ColDirectory    = 1;
constexpr int ColConversation = 2;
constexpr int ColumnCount     = 3;

QString statusText(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running:
        return QStringLiteral("Running");
    case BoxInfo::Status::Stopped:
        return QStringLiteral("Not running");
    case BoxInfo::Status::Exited:
        return QStringLiteral("Exited");
    }
    return QString();
}

QColor statusColor(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running:
        return Theme::running();
    case BoxInfo::Status::Stopped:
        return Theme::known();
    case BoxInfo::Status::Exited:
        return Theme::stopped();
    }
    return Theme::known();
}

// Lower sorts first. See BoxTableModel::SortRole.
int statusRank(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running:
        return 0;
    case BoxInfo::Status::Stopped:
        return 1;
    case BoxInfo::Status::Exited:
        return 2;
    }
    return 3;
}

QString tooltipFor(const BoxInfo &b)
{
    QStringList lines;
    lines << QStringLiteral("%1  ·  %2").arg(b.name, statusText(b.status));
    if (!b.conversationName.isEmpty() && b.conversationName != b.name)
        lines << b.conversationName;
    if (!b.targetDir.isEmpty())
        lines << b.targetDir;
    if (!b.detail.isEmpty())
        lines << b.detail;
    if (!b.stats.isEmpty())
        lines << b.stats;
    if (b.cpuPct >= 0.0f)
        lines << QStringLiteral("cpu %1%").arg(double(b.cpuPct), 0, 'f', 1);
    if (b.memLimitBytes > 0) {
        const auto pct = int(double(b.memUsedBytes) / double(b.memLimitBytes) * 100.0 + 0.5);
        lines << QStringLiteral("mem %1%").arg(pct);
    }
    return lines.join(QLatin1Char('\n'));
}
}

BoxTableModel::BoxTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int BoxTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_boxes.size();
}

int BoxTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant BoxTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_boxes.size())
        return QVariant();

    const BoxInfo &b = m_boxes.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColStatus:
            return statusText(b.status);
        case ColDirectory:
            return b.targetDir;
        case ColConversation:
            return b.conversationName;
        default:
            return QVariant();
        }

    case Qt::DecorationRole:
        // Colored dot beside the status word: scanning a dozen rows for
        // "which of these is alive" is a color job, not a reading job.
        if (index.column() == ColStatus)
            return Icons::statusDot(statusColor(b.status));
        return QVariant();

    case Qt::ForegroundRole:
        if (index.column() == ColStatus)
            return QBrush(statusColor(b.status));
        if (index.column() == ColDirectory)
            return QBrush(Theme::dimText());
        return QVariant();

    case Qt::FontRole:
        return QVariant();

    case Qt::ToolTipRole:
        // Columns elide when narrow, so the tooltip carries the whole row.
        return tooltipFor(b);

    case SortRole:
        if (index.column() == ColStatus)
            return statusRank(b.status);
        return data(index, Qt::DisplayRole);

    default:
        return QVariant();
    }
}

QVariant BoxTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    switch (section) {
    case ColStatus:
        return QStringLiteral("Status");
    case ColDirectory:
        return QStringLiteral("Directory");
    case ColConversation:
        return QStringLiteral("Conversation");
    default:
        return QVariant();
    }
}

void BoxTableModel::setBoxes(const QList<BoxInfo> &boxes)
{
    // operator== compares only structural fields (status/name/conversationName/
    // targetDir). When those are identical, only volatile fields (stats, detail,
    // cpuPct, memUsedBytes, memLimitBytes) can have changed -- emit targeted
    // dataChanged for those rows rather than a full reset that would drop
    // selection and scroll position.
    if (boxes == m_boxes) {
        for (int i = 0; i < boxes.size(); ++i) {
            const BoxInfo &a = m_boxes.at(i);
            const BoxInfo &b = boxes.at(i);
            if (a.stats != b.stats || a.detail != b.detail
                || a.cpuPct != b.cpuPct
                || a.memUsedBytes != b.memUsedBytes
                || a.memLimitBytes != b.memLimitBytes
                || a.diskRwBytes != b.diskRwBytes
                || a.diskTotalBytes != b.diskTotalBytes) {
                m_boxes[i] = b;
                const QModelIndex left  = createIndex(i, 0);
                const QModelIndex right = createIndex(i, ColumnCount - 1);
                // Include SortRole: QSortFilterProxyModel only re-sorts on a
                // dataChanged whose roles include its configured sort role, so
                // omitting it here would silently freeze row order while a
                // sort column's values keep changing.
                emit dataChanged(left, right,
                                  {Qt::DisplayRole, Qt::ToolTipRole, SortRole});
            }
        }
        return;
    }

    // Structural shape changed (boxes added, removed, or reordered): full reset.
    beginResetModel();
    m_boxes = boxes;
    endResetModel();
}

const BoxInfo *BoxTableModel::boxAt(int row) const
{
    if (row < 0 || row >= m_boxes.size())
        return nullptr;
    return &m_boxes.at(row);
}
