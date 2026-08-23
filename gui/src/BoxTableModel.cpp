#include "BoxTableModel.h"

namespace {
constexpr int ColStatus = 0;
constexpr int ColName = 1;
constexpr int ColConversation = 2;
constexpr int ColDirectory = 3;
constexpr int ColDetails = 4;
constexpr int ColumnCount = 5;

QString statusText(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running:
        return QStringLiteral("Running");
    case BoxInfo::Status::Stopped:
        return QStringLiteral("Stopped");
    case BoxInfo::Status::Known:
        return QStringLiteral("Known");
    }
    return QString();
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
    if (role != Qt::DisplayRole)
        return QVariant();

    const BoxInfo &b = m_boxes.at(index.row());
    switch (index.column()) {
    case ColStatus:
        return statusText(b.status);
    case ColName:
        return b.name;
    case ColConversation:
        return b.conversationName;
    case ColDirectory:
        return b.targetDir;
    case ColDetails:
        return b.detail;
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
    case ColName:
        return QStringLiteral("Name");
    case ColConversation:
        return QStringLiteral("Conversation");
    case ColDirectory:
        return QStringLiteral("Directory");
    case ColDetails:
        return QStringLiteral("Details");
    default:
        return QVariant();
    }
}

void BoxTableModel::setBoxes(const QList<BoxInfo> &boxes)
{
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
