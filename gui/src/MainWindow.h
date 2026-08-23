#pragma once

#include <QMainWindow>
#include <QModelIndex>
#include <QPoint>
#include <QString>

#include "BoxTableModel.h"
#include "DockerBackend.h"

class BoxDetailsPanel;
class QAction;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTabWidget;
class QTimer;

// The whole app: a filterable dashboard table and a details panel on top,
// a tab strip of live TerminalWidgets below, and menu/toolbar actions for
// New/Open/Close/Remove/Purge gated by the selected row's status.
//
// Keyboard convention, which the whole shortcut scheme depends on: every
// application chord is Ctrl+Shift+something. Plain Ctrl chords belong to
// Claude Code running inside the terminal panes, which would otherwise
// lose them to the menu bar. See TerminalWidget::event().
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshBoxes();
    void updateActionStates();
    void onRowDoubleClicked(const QModelIndex &index);
    void showTableContextMenu(const QPoint &pos);
    void showTabContextMenu(const QPoint &pos);

    void onNew();
    void onOpen();
    void onClose();
    void onRemove();
    void onPurge();
    void onAbout();

    void onFilterChanged(const QString &text);
    void onCloseCurrentTab();
    void onNextTab();
    void onPrevTab();

    void onTerminalSessionFinished(int exitCode);

private:
    DockerBackend m_docker;
    BoxTableModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;

    QTableView *m_table = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_emptyTableLabel = nullptr;
    BoxDetailsPanel *m_details = nullptr;

    QTabWidget *m_tabs = nullptr;
    QStackedWidget *m_tabStack = nullptr;

    QSplitter *m_outerSplitter = nullptr;
    QSplitter *m_topSplitter = nullptr;

    QTimer *m_refreshTimer = nullptr;
    QLabel *m_statusCounts = nullptr;
    QLabel *m_statusRefreshed = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_purgeAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_detailsAction = nullptr;
    QAction *m_closeTabAction = nullptr;

    void buildUi();
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();

    // The table is behind a filter proxy, so every row index that arrives
    // from the view has to be mapped back before it means anything.
    const BoxInfo *selectedBoxInfo() const;
    QString currentSelectedName() const;
    void reselectByName(const QString &name);

    void openKnownBox(const QString &name);
    void openTerminalTab(const QString &name, const QString &title);
    void closeTabForBox(const QString &name);
    int tabIndexForBox(const QString &name) const;
    void updateTabPlaceholder();

    void saveSettings();
    void restoreSettings();
};
