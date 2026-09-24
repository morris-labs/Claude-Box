#pragma once

#include "DockerBackend.h"

#include <QMap>
#include <QWidget>

class QLabel;
class QProgressBar;
class QScrollArea;
class QVBoxLayout;

// A summary view of resource usage across all running boxes.
// Shows total CPU and memory at the top, then a per-box breakdown below.
// Receives box data from MainWindow via setBoxes() on every refresh tick.
class UsageView : public QWidget {
    Q_OBJECT
public:
    explicit UsageView(QWidget *parent = nullptr);
    void setBoxes(const QList<BoxInfo> &boxes);

private:
    // Holds the live widgets for one per-box row.
    struct RowWidgets {
        QWidget      *container = nullptr;
        QLabel       *nameLabel = nullptr;
        QProgressBar *cpuBar    = nullptr;
        QProgressBar *memBar    = nullptr;
        // Cached stylesheet strings: setStyleSheet forces a full re-polish of
        // the widget subtree, so only call it when the color actually changes.
        QString       lastCpuStyle;
        QString       lastMemStyle;
    };

    void rebuild(const QList<BoxInfo> &boxes);
    void updateTotals(const QList<BoxInfo> &boxes);
    void updateRow(const QString &name, const BoxInfo &b);

    QProgressBar *m_totalCpuBar  = nullptr;
    QProgressBar *m_totalMemBar  = nullptr;
    QWidget      *m_rowsWidget   = nullptr;
    QVBoxLayout  *m_rowsLayout   = nullptr;

    // Per-box row widgets, keyed by box name. Rows are added/removed only
    // when the running set changes; bar values are updated in place each tick.
    QMap<QString, RowWidgets> m_rowCache;
};
