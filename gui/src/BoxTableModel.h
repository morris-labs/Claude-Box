#pragma once

#include <QAbstractTableModel>

#include "DockerBackend.h"

// Read-only table model over a QList<BoxInfo> for the dashboard view.
// Columns: Status, Directory, Conversation.
class BoxTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // Sorting reads this rather than DisplayRole so the Status column
    // orders by urgency (Running, then Stopped, then Exited) instead of
    // alphabetically, which would bury running boxes under "Stopped".
    static constexpr int SortRole  = Qt::UserRole + 1;
    // Float cpuPct (0-100, or -1 if no sample yet). Reserved for future
    // use; the table's Usage column was moved to the Usage tab.
    static constexpr int StatsRole = Qt::UserRole + 10;

    explicit BoxTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setBoxes(const QList<BoxInfo> &boxes);
    const BoxInfo *boxAt(int row) const;

private:
    QList<BoxInfo> m_boxes;
};
