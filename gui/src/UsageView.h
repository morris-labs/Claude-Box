#pragma once

#include "DockerBackend.h"

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
    void rebuild(const QList<BoxInfo> &boxes);

    QProgressBar *m_totalCpuBar  = nullptr;
    QProgressBar *m_totalMemBar  = nullptr;
    QWidget      *m_rowsWidget   = nullptr;
    QVBoxLayout  *m_rowsLayout   = nullptr;
};
