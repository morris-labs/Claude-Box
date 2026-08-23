#pragma once

#include <QAbstractTableModel>

#include "DockerBackend.h"

// Read-only table model over a QList<BoxInfo> for the dashboard view.
// Columns: Status, Name, Conversation, Directory, Details.
class BoxTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
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
