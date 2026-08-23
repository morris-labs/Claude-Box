#pragma once

#include <QMainWindow>
#include <QModelIndex>
#include <QPoint>
#include <QString>

#include "BoxTableModel.h"
#include "DockerBackend.h"

class QAction;
class QTableView;
class QTabWidget;
class QTimer;

// The whole app: a dashboard table on top (auto-refreshing from
// DockerBackend::listBoxes every few seconds), a tab strip of live
// TerminalWidgets below, and toolbar/context-menu actions for
// New/Open/Close/Remove/Purge gated by the selected row's status.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refreshBoxes();
    void updateActionStates();
    void onRowDoubleClicked(const QModelIndex &index);
    void showTableContextMenu(const QPoint &pos);

    void onNew();
    void onOpen();
    void onClose();
    void onRemove();
    void onPurge();

    void onTerminalSessionFinished(int exitCode);

private:
    DockerBackend m_docker;
    BoxTableModel *m_model = nullptr;
    QTableView *m_table = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTimer *m_refreshTimer = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_purgeAction = nullptr;

    const BoxInfo *selectedBoxInfo() const;
    QString currentSelectedName() const;
    void reselectByName(const QString &name);

    void openKnownBox(const QString &name);
    void openTerminalTab(const QString &name, const QString &title);
    void closeTabForBox(const QString &name);
};
