#pragma once

#include "BoxTableModel.h"
#include "ProjectRecord.h"

#include <QSortFilterProxyModel>

// The table's proxy: the existing free-text filter, plus an optional
// restriction to one project. The text filter and the project filter must
// both pass for a row to show.
class BoxFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    // Empty = every project.
    void setProjectRoot(const QString &root)
    {
        if (root == m_root)
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
        m_root = root;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        m_root = root;
        invalidateFilter();
#endif
    }
    const QString &projectRoot() const { return m_root; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (!m_root.isEmpty()) {
            const auto *model = qobject_cast<const BoxTableModel *>(sourceModel());
            const BoxInfo *info = model ? model->boxAt(row) : nullptr;
            if (!info || ProjectRecord::normalizeRoot(info->targetDir) != m_root)
                return false;
        }
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }

private:
    QString m_root;
};
