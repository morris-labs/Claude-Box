#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QModelIndex>
#include <QPoint>
#include <QString>

#include "BoxDetailsPanel.h" // for SshTunnelStatus, a value member below
#include "BoxTableModel.h"
#include "DockerBackend.h"
#include "UsageView.h"

class BoxDetailsPanel;
class SshTunnelSession;
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
    // Kicks off a background poll. Cheap and non-blocking; the results
    // land in onBoxesLoaded().
    void refreshBoxes();
    void onBoxesLoaded();
    void updateActionStates();
    void onRowDoubleClicked(const QModelIndex &index);
    void showTableContextMenu(const QPoint &pos);
    void showTabContextMenu(const QPoint &pos);

    void onNew();
    void onEdit();
    void offerResumeAfterReboot();
    void onFork();
    void onSetupWizard();
    void onManageSshRemotes();
    void onOpen();
    void onClose();
    void onStopAll();
    void onOpenAll();
    void onOpenExternal();
    void onAttachClaude();
    void onRemove();
    void onPurge();
    void onAbout();

    void onFilterChanged(const QString &text);
    void onCloseCurrentTab();
    void onNextTab();
    void onPrevTab();

    void onMoveWorkingDirectory();
    void onChangeWorkingDirectory();

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
    QTabWidget *m_topTabWidget = nullptr;
    QStackedWidget *m_tabStack = nullptr;

    QSplitter *m_outerSplitter = nullptr;
    QSplitter *m_topSplitter = nullptr;

    UsageView *m_usageView = nullptr;

    QTimer *m_refreshTimer = nullptr;

    // Docker polling runs on a worker thread: `docker stats` alone costs
    // 1-2 seconds per call, which froze the GUI for most of every refresh
    // interval when it ran inline. Only one poll is ever in flight, which
    // is also what makes DockerBackend's stats cache safe to touch without
    // a mutex.
    QFutureWatcher<QList<BoxInfo>> *m_refreshWatcher = nullptr;
    bool m_hasLoadedOnce = false;
    QLabel *m_statusCounts = nullptr;
    QLabel *m_statusRefreshed = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_setupAction = nullptr;
    QAction *m_manageRemotesAction = nullptr;
    QAction *m_editAction = nullptr;
    QAction *m_forkAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_stopAllAction = nullptr;
    QAction *m_openAllAction = nullptr;
    QAction *m_openExternalAction = nullptr;
    QAction *m_attachClaudeAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_purgeAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_detailsAction = nullptr;
    QAction *m_closeTabAction = nullptr;
    QAction *m_moveWorkDirAction   = nullptr;
    QAction *m_changeWorkDirAction = nullptr;

    // SSH tunnels (see SshTunnelSession): one background `ssh -N` per key
    // (see syncTunnels()'s own comment for what a key looks like -- a
    // shared catalog remote or a legacy per-box one), kept alive for as
    // long as at least one Running box wants it. m_tunnelSignatures tracks
    // what each session was last started with, so an edit to a remote's
    // config is picked up by restarting its tunnel instead of leaving the
    // old one running.
    QHash<QString, SshTunnelSession *> m_tunnels;
    QHash<QString, QString> m_tunnelSignatures;
    // Mirrors m_tunnels (same keys) but survives a session's death:
    // SshTunnelStatus::running goes false and lastError is filled in when
    // a tunnel exits, instead of the key just vanishing the way m_tunnels
    // itself would -- BoxDetailsPanel needs to be able to show "this one
    // is down" and *why*, not just "nothing to show".
    QHash<QString, SshTunnelStatus> m_tunnelStatus;
    // Recomputed wholesale by every syncTunnels() call: which Running box
    // names currently attach to each shared catalog remote (by name). The
    // ref-count stopTunnelsForBox() and the "last user tears it down"
    // logic are both built on this.
    QHash<QString, QSet<QString>> m_remoteUsers;

    void buildUi();
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();

    // The table is behind a filter proxy, so every row index that arrives
    // from the view has to be mapped back before it means anything.
    const BoxInfo *selectedBoxInfo() const;      // first selected row, or null
    QList<const BoxInfo *> selectedBoxInfos() const; // all selected rows
    QString currentSelectedName() const;
    QStringList currentSelectedNames() const;    // every selected row's name
    void reselectByName(const QString &name);
    void reselectByNames(const QStringList &names);
    // Name of a currently-Running box targeting `dir`, or empty if none.
    // Walks the *source* model, not the proxy, so a filtered-out running box
    // still counts. Shared by onPurge and onMoveWorkingDirectory, both of
    // which act on every BoxRecord that shares a directory, not just the
    // selected one.
    QString runningBoxForDir(const QString &dir) const;

    // False if the conversation the user picked in the New Box dialog
    // can't (or shouldn't) be adopted -- see the definition.
    bool confirmConversationAdoption(const QString &sessionUuid);

    // syncTranscriptTitle: see the definition -- pass false when starting
    // several boxes in one loop to avoid a stall proportional to
    // transcript size times box count.
    void startBox(const QString &name, bool syncTranscriptTitle = true);
    void openTerminalTab(const QString &name, const QString &title);
    void closeTabForBox(const QString &name);
    int tabIndexForBox(const QString &name) const;
    void updateTabPlaceholder();

    // Starts/stops SshTunnelSessions to match which boxes are currently
    // Running and what each one's record asks for. Cheap to call often --
    // it's a no-op for any remote whose tunnel is already up with an
    // unchanged configuration.
    void syncTunnels();
    // Shared by both remote kinds syncTunnels() handles -- see its own
    // comment. Starts or restarts the session for `key` only if it isn't
    // already up with this exact configuration.
    void ensureTunnel(const QString &key, const SshRemote &remote);
    void stopTunnel(const QString &key);
    void stopTunnelsForBox(const QString &boxName);
    // One box's remotes, fully resolved (a catalog attachment's host/
    // identity/forwards looked up by name), in the same order
    // tunnelStatusesForBox() below uses -- what BoxDetailsPanel::setBox()'s
    // sshRemotes argument is built from.
    QList<SshRemote> resolvedSshRemotesForBox(const BoxRecord &rec) const;
    // Statuses positionally aligned with resolvedSshRemotesForBox()'s
    // result for the same box -- what BoxDetailsPanel::setBox()'s
    // tunnelStatuses argument is built from. Empty for a box with no
    // record or no configured remotes.
    QList<SshTunnelStatus> tunnelStatusesForBox(const QString &boxName) const;
    // Tears a box's tunnels down and immediately calls syncTunnels() to
    // bring them back up -- the "Reconnect" button's handler. Bypasses the
    // usual signature check on purpose: the point is to retry right now
    // even though nothing about the configuration has changed.
    void onReconnectTunnels(const QString &boxName);

    void saveSettings();
    void restoreSettings();
};
