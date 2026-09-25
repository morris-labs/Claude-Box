#include "MainWindow.h"

#include "BoxDetailsPanel.h"
#include "BoxRecord.h"
#include "Icons.h"
#include "ConversationCatalog.h"
#include "ManageSshRemotesDialog.h"
#include "NewBoxDialog.h"
#include "PortAllocator.h"
#include "SetupWizard.h"
#include "SshRemoteCatalog.h"
#include "SshTunnelSession.h"
#include "TerminalWidget.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTableView>
#include <QTabWidget>
#include <QProcess>
#include <QScrollBar>
#include <QFileDialog>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int kRefreshIntervalMs = 3000;

// Pre-fills a dialog with tentative port allocations without advancing the
// stored next-base pointer. Returns the tentative list so the caller can
// pass it to commitPortAllocation() after a successful dialog accept.
// Cancelling without calling commitPortAllocation leaves the range unchanged.
QList<int> setupPortAllocation(NewBoxDialog &dlg)
{
    const QList<int> ports = PortAllocator::tentativeAllocate(5, BoxRecord::loadAll());
    dlg.preallocatePorts(ports);
    return ports;
}

void commitPortAllocation(const QList<int> &tentativePorts)
{
    if (tentativePorts.isEmpty())
        return;
    int next = tentativePorts.last() + 1;
    if (next > PortAllocator::kRangeEnd)
        next = PortAllocator::kRangeStart;
    PortAllocator::setNextBase(next);
}

// QSettings keys. Grouped under a prefix so the file stays readable if
// anything else ever needs to persist state.
const QString kGeometryKey    = QStringLiteral("ui/geometry");
const QString kWindowStateKey = QStringLiteral("ui/windowState");
const QString kOuterSplitKey  = QStringLiteral("ui/outerSplitter");
const QString kTopSplitKey    = QStringLiteral("ui/topSplitter");
const QString kDetailsKey     = QStringLiteral("ui/detailsVisible");
const QString kTableHeaderKey = QStringLiteral("ui/tableHeader");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("claude-box"));
    resize(1250, 820);

    buildActions();
    buildUi();
    buildMenus();
    buildToolBar();
    buildStatusBar();

    m_refreshWatcher = new QFutureWatcher<QList<BoxInfo>>(this);
    connect(m_refreshWatcher, &QFutureWatcher<QList<BoxInfo>>::finished,
            this, &MainWindow::onBoxesLoaded);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(kRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshBoxes);
    m_refreshTimer->start();

    restoreSettings();
    refreshBoxes();
    updateActionStates();
    updateTabPlaceholder();

    // Deferred rather than shown inline here: this way the main window
    // paints first, so the wizard reads as "the app opened, then a
    // dialog appeared over it" instead of blocking the window from ever
    // being seen. Only unprompted on a genuine first run -- SetupWizard
    // is reachable any time afterward via File > Setup....
    if (!SetupWizard::hasCompletedSetup())
        QTimer::singleShot(0, this, &MainWindow::onSetupWizard);
}

// --- construction -------------------------------------------------------

void MainWindow::buildActions()
{
    m_newAction = new QAction(Icons::newBox(), QStringLiteral("&New Box…"), this);
    m_newAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    m_newAction->setStatusTip(QStringLiteral("Create a new box and start a conversation in it"));
    connect(m_newAction, &QAction::triggered, this, &MainWindow::onNew);

    m_setupAction = new QAction(QStringLiteral("&Setup…"), this);
    m_setupAction->setStatusTip(QStringLiteral("Check Docker, the claude-code image, and SSH keypair setup"));
    connect(m_setupAction, &QAction::triggered, this, &MainWindow::onSetupWizard);

    m_manageRemotesAction = new QAction(QStringLiteral("Manage SSH &Remotes…"), this);
    m_manageRemotesAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    m_manageRemotesAction->setStatusTip(QStringLiteral("Add, edit, or remove named SSH remotes boxes can attach to"));
    connect(m_manageRemotesAction, &QAction::triggered, this, &MainWindow::onManageSshRemotes);

    m_editAction = new QAction(Icons::editBox(), QStringLiteral("&Edit Box…"), this);
    m_editAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    m_editAction->setStatusTip(QStringLiteral("Change ports, mounts, model/effort, or SSH forwards"));
    connect(m_editAction, &QAction::triggered, this, &MainWindow::onEdit);

    m_forkAction = new QAction(Icons::fork(), QStringLiteral("F&ork Conversation…"), this);
    m_forkAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    m_forkAction->setStatusTip(QStringLiteral(
        "Start a new, independent box preloaded with this one's conversation history"));
    connect(m_forkAction, &QAction::triggered, this, &MainWindow::onFork);

    m_openAction = new QAction(Icons::open(), QStringLiteral("&Start"), this);
    m_openAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    m_openAction->setStatusTip(QStringLiteral("Start this box and resume its conversation"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_closeAction = new QAction(Icons::closeBox(), QStringLiteral("S&top"), this);
    m_closeAction->setStatusTip(QStringLiteral("Stop the selected running container(s)"));
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::onClose);

    m_stopAllAction = new QAction(QStringLiteral("Stop &All Running"), this);
    m_stopAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    m_stopAllAction->setStatusTip(QStringLiteral("Stop every running container"));
    connect(m_stopAllAction, &QAction::triggered, this, &MainWindow::onStopAll);

    m_openAllAction = new QAction(QStringLiteral("Start All &Stopped"), this);
    m_openAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+U")));
    m_openAllAction->setStatusTip(QStringLiteral("Start every stopped box that was running before the last reboot or crash"));
    connect(m_openAllAction, &QAction::triggered, this, &MainWindow::onOpenAll);

    m_openExternalAction = new QAction(QStringLiteral("Open in &Terminal (tmux)"), this);
    m_openExternalAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    m_openExternalAction->setStatusTip(QStringLiteral("Open this container in an external terminal with tmux"));
    connect(m_openExternalAction, &QAction::triggered, this, &MainWindow::onOpenExternal);

    m_removeAction = new QAction(Icons::removeBox(), QStringLiteral("&Remove"), this);
    m_removeAction->setStatusTip(QStringLiteral("docker rm this stopped container"));
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::onRemove);

    m_purgeAction = new QAction(Icons::purge(), QStringLiteral("&Purge…"), this);
    m_purgeAction->setStatusTip(QStringLiteral("Delete conversation history and records for this directory"));
    connect(m_purgeAction, &QAction::triggered, this, &MainWindow::onPurge);

    m_refreshAction = new QAction(Icons::refresh(), QStringLiteral("&Refresh Now"), this);
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_refreshAction->setStatusTip(QStringLiteral("Re-read docker state immediately"));
    connect(m_refreshAction, &QAction::triggered, this, [this] {
        m_docker.invalidateStats(); // explicit refresh means "now", not "cached"
        refreshBoxes();
    });

    m_detailsAction = new QAction(QStringLiteral("Show &Details Panel"), this);
    m_detailsAction->setCheckable(true);
    m_detailsAction->setChecked(true);
    m_detailsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    connect(m_detailsAction, &QAction::toggled, this, [this](bool on) {
        if (m_details)
            m_details->setVisible(on);
    });

    m_closeTabAction = new QAction(QStringLiteral("&Close Tab"), this);
    m_closeTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(m_closeTabAction, &QAction::triggered, this, &MainWindow::onCloseCurrentTab);

    m_moveWorkDirAction = new QAction(QStringLiteral("&Move Working Directory…"), this);
    m_moveWorkDirAction->setStatusTip(
        QStringLiteral("Move the working directory to a new location and update the record"));
    connect(m_moveWorkDirAction, &QAction::triggered,
            this, &MainWindow::onMoveWorkingDirectory);

    m_changeWorkDirAction = new QAction(QStringLiteral("C&hange Working Directory…"), this);
    m_changeWorkDirAction->setStatusTip(
        QStringLiteral("Re-point this box at a different existing directory (no file move)"));
    connect(m_changeWorkDirAction, &QAction::triggered,
            this, &MainWindow::onChangeWorkingDirectory);
}

void MainWindow::buildUi()
{
    // --- table + filter -------------------------------------------------
    m_model = new BoxTableModel(this);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1); // match against every column
    m_proxy->setSortRole(BoxTableModel::SortRole);

    m_table = new QTableView(this);
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(0, Qt::AscendingOrder);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideMiddle);

    // Every column interactively resizable -- ResizeToContents/Stretch
    // both lock a column against manual dragging, which is exactly what
    // stopped Status from being resized before. Starting widths are a
    // sensible default; restoreSettings() overwrites them from QSettings
    // on every launch after the first.
    QHeaderView *header = m_table->horizontalHeader();
    header->setStretchLastSection(false);
    for (int col = 0; col < m_model->columnCount(); ++col)
        header->setSectionResizeMode(col, QHeaderView::Interactive);
    m_table->resizeColumnToContents(0); // Status
    header->resizeSection(1, 320);      // Directory
    header->resizeSection(2, 190);      // Conversation

    // Shown only when there are no boxes at all. Parented to the viewport
    // with a layout so it stays centered without any resize plumbing.
    m_emptyTableLabel = new QLabel(
        QStringLiteral("No boxes yet.\n\nUse New Box (Ctrl+Shift+N) to start one."));
    m_emptyTableLabel->setAlignment(Qt::AlignCenter);
    m_emptyTableLabel->setStyleSheet(QString("color: %1; background: transparent;")
                                         .arg(Theme::dimText().name()));
    auto *viewportLayout = new QVBoxLayout(m_table->viewport());
    viewportLayout->addWidget(m_emptyTableLabel, 0, Qt::AlignCenter);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter by name, conversation, or directory…"));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    auto *tableSide = new QWidget(this);
    auto *tableLayout = new QVBoxLayout(tableSide);
    tableLayout->setContentsMargins(8, 8, 4, 4);
    tableLayout->setSpacing(6);
    tableLayout->addWidget(m_filterEdit);
    tableLayout->addWidget(m_table, 1);

    // --- details panel --------------------------------------------------
    m_details = new BoxDetailsPanel(this);
    m_details->setMinimumWidth(280);
    connect(m_details, &BoxDetailsPanel::reconnectRequested, this, &MainWindow::onReconnectTunnels);
    connect(m_details, &BoxDetailsPanel::relinkRequested, this,
            [this](const QString &boxName, const QString &newDir) {
        // The button that sends this is disabled while Running (see
        // BoxDetailsPanel::setBox), but re-check here too: the box could
        // have started running again between the button's last repaint and
        // this click landing.
        for (int i = 0; i < m_model->rowCount(); ++i) {
            const BoxInfo *row = m_model->boxAt(i);
            if (row && row->name == boxName && row->status == BoxInfo::Status::Running) {
                QMessageBox::information(this, QStringLiteral("Relink"),
                    QStringLiteral("This box is running. Stop it before relinking its "
                                   "directory."));
                return;
            }
        }
        const BoxRecord oldRec = BoxRecord::load(boxName);
        const QString oldDir = oldRec.targetDir;
        BoxRecord rec = oldRec;
        rec.targetDir = newDir;
        rec.save();
        refreshBoxes();
        // The transcript is stored under ~/.claude/projects/<encoded-old-path>/
        // because the directory moved externally (before Relink). The box will
        // resume correctly only if that directory is renamed to match the new
        // path. Show the paths so the user can do it manually if needed.
        if (!oldDir.isEmpty() && oldDir != newDir) {
            const QString oldProject = ConversationCatalog::projectDirFor(oldDir);
            const QString newProject = ConversationCatalog::projectDirFor(newDir);
            if (QDir(oldProject).exists() && !QDir(newProject).exists()) {
                QMessageBox::information(this, QStringLiteral("Relink"),
                    QStringLiteral("Record updated. To keep conversation history findable, "
                                   "rename the transcript directory:\n\n"
                                   "From: %1\n\nTo: %2")
                        .arg(oldProject, newProject));
            }
        }
    });

    m_topSplitter = new QSplitter(Qt::Horizontal, this);
    m_topSplitter->addWidget(tableSide);
    m_topSplitter->addWidget(m_details);
    m_topSplitter->setStretchFactor(0, 3);
    m_topSplitter->setStretchFactor(1, 1);
    m_topSplitter->setChildrenCollapsible(false);
    m_topSplitter->setSizes({880, 340});

    // --- terminal tabs --------------------------------------------------
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested,
            this, &MainWindow::showTabContextMenu);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        // Closing a tab only detaches our local `docker attach` client --
        // the container itself keeps running (see PtySession).
        QWidget *w = m_tabs->widget(index);
        m_tabs->removeTab(index);
        if (w)
            w->deleteLater();
        updateTabPlaceholder();
    });

    auto *tabPlaceholder = new QLabel(
        QStringLiteral("No open terminals.\n\n"
                       "Double-click a running box to attach to it,\n"
                       "or open a box that isn't running to start it back up."));
    tabPlaceholder->setAlignment(Qt::AlignCenter);
    tabPlaceholder->setStyleSheet(QString("color: %1;").arg(Theme::dimText().name()));

    m_tabStack = new QStackedWidget(this);
    m_tabStack->addWidget(tabPlaceholder); // index 0
    m_tabStack->addWidget(m_tabs);         // index 1

    m_topTabWidget = new QTabWidget(this);
    m_topTabWidget->setDocumentMode(true);
    m_topTabWidget->setTabsClosable(false);
    m_topTabWidget->addTab(m_topSplitter, QStringLiteral("Boxes"));
    m_usageView = new UsageView(this);
    m_topTabWidget->addTab(m_usageView, QStringLiteral("Usage"));

    m_outerSplitter = new QSplitter(Qt::Vertical, this);
    m_outerSplitter->addWidget(m_topTabWidget);
    m_outerSplitter->addWidget(m_tabStack);
    m_outerSplitter->setStretchFactor(0, 1);
    m_outerSplitter->setStretchFactor(1, 2);
    m_outerSplitter->setChildrenCollapsible(false);
    m_outerSplitter->setSizes({440, 360});
    setCentralWidget(m_outerSplitter);

    // When the Usage tab is active, the terminal panel is irrelevant and
    // would only waste vertical space. Hide it so Usage gets full height;
    // show it again when switching back to the Boxes tab.
    connect(m_topTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        const bool onUsage = (index == 1);
        m_tabStack->setVisible(!onUsage);
    });

    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::onRowDoubleClicked);
    connect(m_table, &QTableView::customContextMenuRequested, this, &MainWindow::showTableContextMenu);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateActionStates);

    // Alt+1..9 jumps straight to a terminal tab. Reserved from the
    // terminal's key handling in TerminalWidget::isApplicationChord.
    for (int i = 0; i < 9; ++i) {
        auto *sc = new QShortcut(QKeySequence(Qt::ALT | (Qt::Key_1 + i)), this);
        connect(sc, &QShortcut::activated, this, [this, i] {
            if (i < m_tabs->count())
                m_tabs->setCurrentIndex(i);
        });
    }
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_setupAction);
    fileMenu->addAction(m_manageRemotesAction);
    fileMenu->addSeparator();
    QAction *quit = fileMenu->addAction(QStringLiteral("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Q")));

    QMenu *boxMenu = menuBar()->addMenu(QStringLiteral("&Box"));
    boxMenu->addAction(m_openAction);
    boxMenu->addAction(m_editAction);
    boxMenu->addAction(m_forkAction);
    boxMenu->addSeparator();
    boxMenu->addAction(m_closeAction);
    boxMenu->addAction(m_stopAllAction);
    boxMenu->addAction(m_openAllAction);
    boxMenu->addAction(m_openExternalAction);
    boxMenu->addSeparator();
    boxMenu->addAction(m_moveWorkDirAction);
    boxMenu->addAction(m_changeWorkDirAction);
    boxMenu->addSeparator();
    boxMenu->addAction(m_removeAction);
    boxMenu->addAction(m_purgeAction);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_refreshAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_detailsAction);
    QAction *focusFilter = viewMenu->addAction(QStringLiteral("&Filter Boxes"), this, [this] {
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });
    focusFilter->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));

    QMenu *termMenu = menuBar()->addMenu(QStringLiteral("&Terminal"));
    termMenu->addAction(m_closeTabAction);
    QAction *nextTab = termMenu->addAction(QStringLiteral("&Next Tab"), this, &MainWindow::onNextTab);
    nextTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Right")));
    QAction *prevTab = termMenu->addAction(QStringLiteral("&Previous Tab"), this, &MainWindow::onPrevTab);
    prevTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Left")));

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&About claude-box"), this, &MainWindow::onAbout);
}

void MainWindow::buildToolBar()
{
    QToolBar *toolbar = addToolBar(QStringLiteral("Actions"));
    toolbar->setObjectName(QStringLiteral("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(18, 18));

    toolbar->addAction(m_newAction);
    toolbar->addSeparator();
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_editAction);
    toolbar->addAction(m_forkAction);
    toolbar->addAction(m_closeAction);
    toolbar->addAction(m_openExternalAction);
    toolbar->addSeparator();
    toolbar->addAction(m_removeAction);
    toolbar->addAction(m_purgeAction);

    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    toolbar->addAction(m_refreshAction);
}

void MainWindow::buildStatusBar()
{
    m_statusCounts = new QLabel(this);
    m_statusRefreshed = new QLabel(this);
    statusBar()->addWidget(m_statusCounts, 1);
    statusBar()->addPermanentWidget(m_statusRefreshed);
}

// --- selection helpers --------------------------------------------------

const BoxInfo *MainWindow::selectedBoxInfo() const
{
    if (!m_table->selectionModel())
        return nullptr;
    const QModelIndexList sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return nullptr;
    return m_model->boxAt(m_proxy->mapToSource(sel.first()).row());
}

QList<const BoxInfo *> MainWindow::selectedBoxInfos() const
{
    QList<const BoxInfo *> result;
    if (!m_table->selectionModel())
        return result;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows()) {
        if (const BoxInfo *info = m_model->boxAt(m_proxy->mapToSource(idx).row()))
            result << info;
    }
    return result;
}

QString MainWindow::currentSelectedName() const
{
    const BoxInfo *info = selectedBoxInfo();
    return info ? info->name : QString();
}

QStringList MainWindow::currentSelectedNames() const
{
    QStringList names;
    for (const BoxInfo *info : selectedBoxInfos())
        names << info->name;
    return names;
}

void MainWindow::reselectByName(const QString &name)
{
    if (name.isEmpty())
        return;
    reselectByNames({name});
}

void MainWindow::reselectByNames(const QStringList &names)
{
    if (names.isEmpty() || !m_table->selectionModel())
        return;
    const QSet<QString> wanted(names.begin(), names.end());
    // Build the whole selection before applying it, and use selectionModel()
    // directly instead of selectRow() per row, so Qt does not call scrollTo()
    // and fight the user's scroll position, and so an ExtendedSelection of
    // several rows survives a refresh tick instead of collapsing to one.
    QItemSelection combined;
    QModelIndex firstMatch;
    for (int i = 0; i < m_proxy->rowCount(); ++i) {
        const QModelIndex idx0 = m_proxy->index(i, 0);
        const BoxInfo *info = m_model->boxAt(m_proxy->mapToSource(idx0).row());
        if (info && wanted.contains(info->name)) {
            const QModelIndex last = m_proxy->index(i, m_model->columnCount() - 1);
            combined.select(idx0, last);
            if (!firstMatch.isValid())
                firstMatch = idx0;
        }
    }
    if (combined.isEmpty())
        return;
    m_table->selectionModel()->select(combined, QItemSelectionModel::ClearAndSelect);
    m_table->selectionModel()->setCurrentIndex(firstMatch, QItemSelectionModel::NoUpdate);
}

// --- refresh ------------------------------------------------------------

void MainWindow::refreshBoxes()
{
    // One poll at a time. If the previous one is still going (a stats
    // sample takes a second or two) just let it finish -- queueing more
    // work behind it is how the old synchronous version fell permanently
    // behind its own timer.
    if (m_refreshWatcher->isRunning())
        return;

    const bool sampleStats = m_hasLoadedOnce;
    m_refreshWatcher->setFuture(QtConcurrent::run([this, sampleStats] {
        return m_docker.listBoxes(sampleStats);
    }));
}

void MainWindow::onBoxesLoaded()
{
    // The very first poll deliberately skips stats so the table appears
    // immediately instead of after a two-second sample; the next tick
    // fills the cpu/mem readouts in.
    const bool wasFirstLoad = !m_hasLoadedOnce;
    m_hasLoadedOnce = true;

    const QStringList selected = currentSelectedNames();
    // setBoxes calls beginResetModel/endResetModel, which resets the view's
    // scroll position. Save and restore it so the table doesn't jump on every
    // refresh tick while the user is browsing away from the selected row.
    const int scrollPos = m_table->verticalScrollBar()->value();
    QList<BoxInfo> freshBoxes = m_refreshWatcher->result();

    // Detect boxes that stopped externally (no terminal tab was open to fire
    // onTerminalSessionFinished) and clear their wasRunning flag so they are
    // not offered for restart-after-reboot on the next launch. freshBoxes'
    // own wasRunning came from the on-disk record as of listBoxes(), before
    // this clears it, so it's updated in place here too -- otherwise
    // updateActionStates() would see the stale true for one more tick.
    QSet<QString> prevRunning;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (const BoxInfo *b = m_model->boxAt(i); b && b->status == BoxInfo::Status::Running)
            prevRunning.insert(b->name);
    }

    // A live Docker Engine restart (Docker Desktop update, WSL2 restart)
    // while the GUI stays open drops running boxes from `docker ps` the
    // same way an isolated box exiting does -- but clearing wasRunning for
    // boxes lost to an Engine event here would erase the very recovery
    // signal offerResumeAfterReboot() needs, and that only runs on this
    // app's own startup, not on an Engine-level event. Two signals
    // distinguish the two cases:
    //
    //  - lastPollDaemonUnreachable(): the poll that just produced
    //    freshBoxes couldn't actually reach the daemon at all, rather than
    //    reaching it and finding fewer containers. This is the strong
    //    signal -- it covers a single running box just as well as a whole
    //    fleet, since it doesn't depend on counting anything that
    //    "disappeared".
    //  - the older count-based heuristic, kept as a fallback for a daemon
    //    that answered the poll (so isn't flagged unreachable) but still
    //    lost most of a non-trivial fleet at once -- e.g. an Engine
    //    restart landing between two ticks in a way this poll's query
    //    still succeeded against a freshly-restarted, empty daemon.
    //
    // An isolated single-box exit while the daemon stays reachable still
    // clears normally below; an isolated mass crash that isn't actually an
    // Engine event just means the boxes stay offered for resume, which is
    // a stale-but-harmless prompt rather than a lost one.
    QSet<QString> stillRunning;
    for (const BoxInfo &b : freshBoxes) {
        if (b.status == BoxInfo::Status::Running)
            stillRunning.insert(b.name);
    }
    int droppedCount = 0;
    for (const QString &name : prevRunning) {
        if (!stillRunning.contains(name))
            ++droppedCount;
    }
    const bool likelyEngineRestart =
        m_docker.lastPollDaemonUnreachable() ||
        (prevRunning.size() >= 2 && droppedCount * 2 >= prevRunning.size());

    if (!likelyEngineRestart) {
        for (BoxInfo &b : freshBoxes) {
            if (b.status != BoxInfo::Status::Running && prevRunning.contains(b.name)) {
                BoxRecord rec = BoxRecord::load(b.name);
                if (rec.isValid() && rec.wasRunning) {
                    rec.wasRunning = false;
                    rec.save();
                }
                b.wasRunning = false;
            }
        }
    }

    m_model->setBoxes(freshBoxes);
    reselectByNames(selected);
    m_table->verticalScrollBar()->setValue(scrollPos);
    m_usageView->setBoxes(freshBoxes);
    syncTunnels();

    if (wasFirstLoad) {
        QTimer::singleShot(0, this, &MainWindow::refreshBoxes);
        QTimer::singleShot(0, this, &MainWindow::offerResumeAfterReboot);
    }

    int running = 0, stopped = 0, exited = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *info = m_model->boxAt(i);
        if (!info)
            continue;
        switch (info->status) {
        case BoxInfo::Status::Running: ++running; break;
        case BoxInfo::Status::Stopped: ++stopped; break;
        case BoxInfo::Status::Exited:  ++exited;  break;
        }
    }

    QStringList parts;
    parts << QStringLiteral("%1 running").arg(running);
    parts << QStringLiteral("%1 not running").arg(stopped);
    if (exited > 0)
        parts << QStringLiteral("%1 exited").arg(exited);
    const int hidden = m_model->rowCount() - m_proxy->rowCount();
    if (hidden > 0)
        parts << QStringLiteral("%1 hidden by filter").arg(hidden);
    m_statusCounts->setText(parts.join(QStringLiteral("   ·   ")));
    m_statusRefreshed->setText(
        QStringLiteral("updated %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));

    m_emptyTableLabel->setVisible(m_model->rowCount() == 0);

    // With nothing selected the details panel is just a placeholder, which
    // makes the app look broken on launch. Pick the first row -- sorted
    // Running-first -- so there's something to look at immediately.
    if (!selectedBoxInfo() && m_proxy->rowCount() > 0)
        m_table->selectRow(0);

    updateActionStates();
}

void MainWindow::updateActionStates()
{
    const QList<const BoxInfo *> sel = selectedBoxInfos();
    const BoxInfo *single = sel.size() == 1 ? sel.first() : nullptr;
    const BoxRecord rec = single ? BoxRecord::load(single->name) : BoxRecord();

    bool anyRunning = false, anyStopped = false, anyExited = false;
    for (const BoxInfo *info : sel) {
        if (info->status == BoxInfo::Status::Running) anyRunning = true;
        if (info->status == BoxInfo::Status::Stopped) anyStopped = true;
        if (info->status == BoxInfo::Status::Exited)  anyExited  = true;
    }

    // Single-item actions: require exactly one selection.
    m_editAction->setEnabled(single && rec.isValid());
    m_forkAction->setEnabled(single && rec.isValid() && !rec.sessionUuid.isEmpty());
    m_purgeAction->setEnabled(single && !single->targetDir.isEmpty());
    const bool canRelocate = single && single->status != BoxInfo::Status::Running
                             && rec.isValid() && !single->targetDir.isEmpty();
    m_moveWorkDirAction->setEnabled(canRelocate);
    m_changeWorkDirAction->setEnabled(single && single->status != BoxInfo::Status::Running
                                      && rec.isValid());
    m_openExternalAction->setEnabled(single && single->status == BoxInfo::Status::Running);

    // Multi-item actions: any matching selection is enough.
    m_openAction->setEnabled(anyStopped);
    m_closeAction->setEnabled(anyRunning);
    // Remove is only safe for exited containers (stopped but not removed --
    // rare with --rm): disable when anything is running or just stopped.
    m_removeAction->setEnabled(anyExited && !anyRunning && !anyStopped);

    // Stop All: model-wide, not selection-gated.
    bool anyModelRunning = false;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (const BoxInfo *row = m_model->boxAt(i);
            row && row->status == BoxInfo::Status::Running) {
            anyModelRunning = true;
            break;
        }
    }
    m_stopAllAction->setEnabled(anyModelRunning);

    bool anyResumable = false;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (!row || row->status != BoxInfo::Status::Stopped)
            continue;
        if (row->wasRunning) {
            anyResumable = true;
            break;
        }
    }
    m_openAllAction->setEnabled(anyResumable);

    m_closeTabAction->setEnabled(m_tabs->count() > 0);

    if (single) {
        m_details->setBox(single, resolvedSshRemotesForBox(rec), tunnelStatusesForBox(single->name));
    } else {
        m_details->setBox(nullptr);
    }
}

void MainWindow::onFilterChanged(const QString &text)
{
    const QString selected = currentSelectedName();
    m_proxy->setFilterFixedString(text);
    reselectByName(selected);
    updateActionStates();
}

void MainWindow::onMoveWorkingDirectory()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status == BoxInfo::Status::Running)
        return;

    // Copy fields out before opening a dialog: info points into the model's
    // live vector, and the 3s refresh timer replaces that vector whenever a
    // modal event loop is spinning (same reason onEdit() copies wasRunning).
    const QString boxName    = info->name;
    const QString currentDir = info->targetDir;
    if (currentDir.isEmpty())
        return;

    // Move renames the directory and repoints every sibling BoxRecord that
    // shares it, not just the selected box -- refuse if any of them is
    // currently running with the old path bind-mounted.
    const QString runningName = runningBoxForDir(currentDir);
    if (!runningName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Move"),
            runningName + QStringLiteral(" is running against this directory -- close it first."));
        return;
    }

    const QFileInfo fi(currentDir);
    const QString newParent = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Choose new parent directory for '%1'").arg(fi.fileName()),
        fi.absolutePath());
    if (newParent.isEmpty())
        return;

    const QString newPath = newParent + "/" + fi.fileName();
    if (QDir(newPath) == QDir(currentDir))
        return;

    if (QFileInfo::exists(newPath)) {
        QMessageBox::warning(this, QStringLiteral("Move failed"),
            QStringLiteral("A directory named '%1' already exists in the chosen location.")
                .arg(fi.fileName()));
        return;
    }

    if (!QDir().rename(currentDir, newPath)) {
        QMessageBox::critical(this, QStringLiteral("Move failed"),
            QStringLiteral("Could not move '%1' to '%2'.\n"
                           "Check that you have write access to both locations.")
                .arg(currentDir, newPath));
        return;
    }

    // Rename the Claude project directory alongside the working directory so
    // that conversations remain findable (ConversationCatalog encodes the
    // container-side path into the directory name).
    const QString oldProjectDir = ConversationCatalog::projectDirFor(currentDir);
    const QString newProjectDir = ConversationCatalog::projectDirFor(newPath);
    if (oldProjectDir != newProjectDir && QDir(oldProjectDir).exists()
        && !QDir(newProjectDir).exists()) {
        if (!QDir().rename(oldProjectDir, newProjectDir)) {
            QMessageBox::warning(this, QStringLiteral("Move"),
                QStringLiteral("Directory moved, but could not rename conversation history "
                               "from '%1' to '%2'. The conversation may not resume correctly.")
                    .arg(oldProjectDir, newProjectDir));
        }
    }

    // Repoint this box's record first.
    BoxRecord rec = BoxRecord::load(boxName);
    rec.targetDir = newPath;
    rec.save();

    // Repoint every other box that also pointed at the old directory.
    // Do this before the project-dir rename below so the old path is still
    // valid for the lookup (though the rename is best-effort anyway).
    const QList<BoxRecord> all = BoxRecord::loadAll();
    for (BoxRecord sibling : all) {
        if (sibling.name != boxName && sibling.targetDir == currentDir) {
            sibling.targetDir = newPath;
            sibling.save();
        }
    }

    refreshBoxes();
}

void MainWindow::onChangeWorkingDirectory()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status == BoxInfo::Status::Running)
        return;

    // Copy fields before opening a dialog (see onMoveWorkingDirectory).
    const QString boxName  = info->name;
    const QString oldDir   = info->targetDir;
    const QString convName = info->conversationName;

    const QString startDir = oldDir.isEmpty() ? QDir::homePath() : oldDir;

    const QString newDir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Choose new working directory for '%1'")
            .arg(convName.isEmpty() ? boxName : convName),
        startDir);
    if (newDir.isEmpty() || newDir == oldDir)
        return;

    // Do NOT rename ~/.claude/projects/<old-encoded>/ here. Unlike Move (where
    // the directory physically moved and no other sessions point at the old
    // path), Change only repoints this box's record. The old directory still
    // exists on disk and other boxes or plain `claude` sessions may still
    // refer to it; renaming its project directory would hijack all of their
    // conversation history into this box's new namespace.
    BoxRecord rec = BoxRecord::load(boxName);
    rec.targetDir = newDir;
    rec.save();
    refreshBoxes();
}

// --- context menus ------------------------------------------------------

void MainWindow::showTableContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.addAction(m_openAction);
    menu.addAction(m_editAction);
    menu.addAction(m_forkAction);
    menu.addSeparator();
    menu.addAction(m_closeAction);
    menu.addAction(m_openExternalAction);
    menu.addSeparator();
    menu.addAction(m_moveWorkDirAction);
    menu.addAction(m_changeWorkDirAction);
    menu.addSeparator();
    menu.addAction(m_removeAction);
    menu.addAction(m_purgeAction);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::showTabContextMenu(const QPoint &pos)
{
    const int index = m_tabs->tabBar()->tabAt(pos);
    if (index < 0)
        return;

    QMenu menu(this);
    QAction *closeThis = menu.addAction(QStringLiteral("Close Tab"));
    QAction *closeOthers = menu.addAction(QStringLiteral("Close Other Tabs"));
    QAction *closeAll = menu.addAction(QStringLiteral("Close All Tabs"));
    closeOthers->setEnabled(m_tabs->count() > 1);

    QAction *chosen = menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
    if (!chosen)
        return;

    auto closeAt = [this](int i) {
        QWidget *w = m_tabs->widget(i);
        m_tabs->removeTab(i);
        if (w)
            w->deleteLater();
    };

    if (chosen == closeThis) {
        closeAt(index);
    } else if (chosen == closeOthers) {
        QWidget *keep = m_tabs->widget(index);
        for (int i = m_tabs->count() - 1; i >= 0; --i) {
            if (m_tabs->widget(i) != keep)
                closeAt(i);
        }
    } else if (chosen == closeAll) {
        for (int i = m_tabs->count() - 1; i >= 0; --i)
            closeAt(i);
    }
    updateTabPlaceholder();
    updateActionStates();
}

// --- tabs ---------------------------------------------------------------

int MainWindow::tabIndexForBox(const QString &name) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property("boxName").toString() == name)
            return i;
    }
    return -1;
}

void MainWindow::updateTabPlaceholder()
{
    m_tabStack->setCurrentIndex(m_tabs->count() > 0 ? 1 : 0);
}

void MainWindow::onRowDoubleClicked(const QModelIndex &index)
{
    const BoxInfo *info = m_model->boxAt(m_proxy->mapToSource(index).row());
    if (!info)
        return;

    if (info->status == BoxInfo::Status::Stopped)
        startBox(info->name);
    else if (info->status == BoxInfo::Status::Running)
        openTerminalTab(info->name, info->conversationName.isEmpty() ? info->name : info->conversationName);
}

void MainWindow::openTerminalTab(const QString &name, const QString &title)
{
    const int existing = tabIndexForBox(name);
    if (existing >= 0) {
        auto *term = qobject_cast<TerminalWidget *>(m_tabs->widget(existing));
        if (term && !term->isDisconnected()) {
            m_tabs->setCurrentIndex(existing);
            term->setFocus();
            return;
        }
        // A dead tab for this box is stale once it's running again --
        // replace it rather than leaving two tabs with the same name.
        QWidget *w = m_tabs->widget(existing);
        m_tabs->removeTab(existing);
        if (w)
            w->deleteLater();
    }

    auto *term = new TerminalWidget(m_tabs);
    term->setProperty("boxName", name);
    connect(term, &TerminalWidget::sessionFinished, this, &MainWindow::onTerminalSessionFinished);

    if (!term->attachToContainer(name)) {
        QMessageBox::warning(this, QStringLiteral("Start"), QStringLiteral("Failed to attach to ") + name);
        term->deleteLater();
        return;
    }

    const int index = m_tabs->addTab(term, Icons::terminal(), title.isEmpty() ? name : title);
    m_tabs->setTabToolTip(index, name);
    m_tabs->setCurrentIndex(index);
    updateTabPlaceholder();
    updateActionStates();
    term->setFocus();
}

void MainWindow::closeTabForBox(const QString &name)
{
    const int index = tabIndexForBox(name);
    if (index < 0)
        return;
    QWidget *w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (w)
        w->deleteLater();
    updateTabPlaceholder();
}

// --- SSH tunnels ---------------------------------------------------------

// Two kinds of remote share m_tunnels/m_tunnelSignatures/m_tunnelStatus,
// distinguished by key prefix:
//
//  - "remote:<name>" -- a named SshRemoteCatalog entry, shared by every
//    Running box that attaches to it (BoxRecord::sshRemoteRefs). Two boxes
//    reaching the same machine get exactly one ssh -N process between
//    them, ref-counted via m_remoteUsers so it only tears down once the
//    *last* box using it stops being Running -- this is what replaced the
//    old per-box tunnel model, after a box turned out to have a duplicate
//    remote from being configured independently in two different boxes.
//  - "legacybox:<boxName>#<remoteIndex>" -- a legacy per-box inline remote
//    (BoxRecord::sshRemotes), from before the catalog existed. Never
//    shared; always fully owned by the one box that has it.
//
// tunnelSignature() covers everything that would change what `ssh` is
// actually asked to do, so a session already running with an unchanged
// configuration is left alone rather than being torn down and restarted
// every poll tick.
namespace {
QString tunnelKeyForRemote(const QString &remoteName)
{
    return QStringLiteral("remote:") + remoteName;
}
QString tunnelKeyForLegacy(const QString &boxName, int remoteIndex)
{
    return QStringLiteral("legacybox:") + boxName + QStringLiteral("#") + QString::number(remoteIndex);
}
QString tunnelSignature(const SshRemote &remote)
{
    return remote.host + '\n' + remote.identity + '\n' + remote.forwards.join('\n');
}
}

// Starts (or restarts, on a signature change) the session for `key` if it
// isn't already up with this exact configuration; no-op otherwise. Shared
// by every place that wants a tunnel up -- both remote kinds in
// syncTunnels() go through this, so status bookkeeping can't drift between
// them.
void MainWindow::ensureTunnel(const QString &key, const SshRemote &remote)
{
    const QString sig = tunnelSignature(remote);

    auto existing = m_tunnels.find(key);
    if (existing != m_tunnels.end() && existing.value()->isRunning()
        && m_tunnelSignatures.value(key) == sig) {
        // Still up with this exact configuration. The finished-signal
        // handler below already flips this false the moment a session
        // dies, but keeping it current here too means a stale status can
        // never survive more than one poll even if that handler were
        // somehow missed.
        m_tunnelStatus[key].running = true;
        return;
    }

    stopTunnel(key); // torn down first if it exists (dead, or config changed)

    auto *session = new SshTunnelSession(this);
    connect(session, &SshTunnelSession::log, this, [this, key](const QString &line) {
        m_tunnelStatus[key].lastError = line;
        statusBar()->showMessage(QStringLiteral("ssh tunnel (%1): %2").arg(key, line), 8000);
    });
    connect(session, &SshTunnelSession::finished, this, [this, key](int) {
        if (m_tunnelStatus.contains(key))
            m_tunnelStatus[key].running = false;
    });

    const bool started = session->start(remote);
    m_tunnels.insert(key, session);
    m_tunnelSignatures.insert(key, sig);

    SshTunnelStatus &status = m_tunnelStatus[key];
    status.attempted = true;
    status.running = started && session->isRunning();
    if (started)
        status.lastError.clear(); // drop a stale error from a previous attempt
}

void MainWindow::syncTunnels()
{
    QSet<QString> stillWanted;
    QHash<QString, QSet<QString>> wantedUsers; // catalog remote name -> Running box names wanting it

    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (!row || row->status != BoxInfo::Status::Running)
            continue;

        const BoxRecord rec = BoxRecord::load(row->name);
        if (!rec.isValid())
            continue;

        for (const QString &name : rec.sshRemoteRefs) {
            if (!name.trimmed().isEmpty())
                wantedUsers[name].insert(row->name);
        }

        // Legacy inline remotes: never shared, so no ref-counting needed --
        // handled directly here rather than through the shared-remote loop
        // below.
        for (int r = 0; r < rec.sshRemotes.size(); ++r) {
            const SshRemote &remote = rec.sshRemotes.at(r);
            if (!remote.isValid() || remote.forwards.isEmpty())
                continue;
            const QString key = tunnelKeyForLegacy(row->name, r);
            stillWanted.insert(key);
            ensureTunnel(key, remote);
        }
    }

    m_remoteUsers = wantedUsers;

    for (auto it = wantedUsers.constBegin(); it != wantedUsers.constEnd(); ++it) {
        if (it.value().isEmpty())
            continue;
        const SshRemote remote = SshRemoteCatalog::load(it.key());
        if (!remote.isValid())
            continue; // a box attaches to a name the catalog no longer has -- nothing to tunnel
        const QString key = tunnelKeyForRemote(it.key());
        stillWanted.insert(key);
        ensureTunnel(key, remote);
    }

    // Anything no longer wanted by any box: not Running any more, no
    // longer attached/configured, or (for a shared remote) simply down to
    // zero users.
    const QStringList toDrop = m_tunnels.keys();
    for (const QString &key : toDrop) {
        if (!stillWanted.contains(key))
            stopTunnel(key);
    }
}

void MainWindow::stopTunnel(const QString &key)
{
    auto it = m_tunnels.find(key);
    if (it == m_tunnels.end())
        return;
    it.value()->stop();
    it.value()->deleteLater();
    m_tunnels.erase(it);
    m_tunnelSignatures.remove(key);
    m_tunnelStatus.remove(key);
}

// Called when one box stops being Running (or closes) and there's no
// reason to wait for the next poll to notice. A legacy inline remote is
// always fully owned by this box, so it's torn down outright; a shared
// catalog remote only goes down if this was its *last* user -- otherwise
// another Running box is still relying on it.
void MainWindow::stopTunnelsForBox(const QString &boxName)
{
    const QString legacyPrefix = QStringLiteral("legacybox:") + boxName + '#';
    const QStringList keys = m_tunnels.keys();
    for (const QString &key : keys) {
        if (key.startsWith(legacyPrefix))
            stopTunnel(key);
    }

    const BoxRecord rec = BoxRecord::load(boxName);
    for (const QString &name : rec.sshRemoteRefs) {
        bool otherUserRunning = false;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            const BoxInfo *row = m_model->boxAt(i);
            if (!row || row->name == boxName || row->status != BoxInfo::Status::Running)
                continue;
            const BoxRecord other = BoxRecord::load(row->name);
            if (other.isValid() && other.sshRemoteRefs.contains(name)) {
                otherUserRunning = true;
                break;
            }
        }
        if (!otherUserRunning)
            stopTunnel(tunnelKeyForRemote(name));
    }
}

// Every remote one box currently references, in display order: catalog
// attachments first (the current model), then any legacy per-box inline
// remotes an old record still carries. tunnelStatusesForBox() below builds
// its list in the same order, so the two stay positionally aligned for
// BoxDetailsPanel.
QList<SshRemote> MainWindow::resolvedSshRemotesForBox(const BoxRecord &rec) const
{
    QList<SshRemote> result;
    for (const QString &name : rec.sshRemoteRefs) {
        SshRemote remote = SshRemoteCatalog::load(name);
        if (remote.name.isEmpty())
            remote.name = name; // catalog entry missing/deleted -- still show *something* was expected
        result << remote;
    }
    result << rec.sshRemotes;
    return result;
}

QList<SshTunnelStatus> MainWindow::tunnelStatusesForBox(const QString &boxName) const
{
    const BoxRecord rec = BoxRecord::load(boxName);
    if (!rec.isValid())
        return {};

    QList<SshTunnelStatus> statuses;
    for (const QString &name : rec.sshRemoteRefs)
        statuses << m_tunnelStatus.value(tunnelKeyForRemote(name));
    for (int r = 0; r < rec.sshRemotes.size(); ++r)
        statuses << m_tunnelStatus.value(tunnelKeyForLegacy(boxName, r));
    return statuses;
}

void MainWindow::onReconnectTunnels(const QString &boxName)
{
    const BoxRecord rec = BoxRecord::load(boxName);
    if (!rec.isValid())
        return;

    // Legacy inline remotes: fully owned by this box, tear down outright.
    for (int r = 0; r < rec.sshRemotes.size(); ++r)
        stopTunnel(tunnelKeyForLegacy(boxName, r));

    // Shared remotes: force a fresh reconnect for *everyone* attached to
    // them, not just this box -- nobody using a remote wants it to stay
    // dead, and killing+restarting it doesn't detach anyone.
    for (const QString &name : rec.sshRemoteRefs)
        stopTunnel(tunnelKeyForRemote(name));

    syncTunnels();
    updateActionStates(); // reflect the fresh (re-)attempt without waiting for the next poll
}

void MainWindow::onCloseCurrentTab()
{
    const int index = m_tabs->currentIndex();
    if (index < 0)
        return;
    QWidget *w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (w)
        w->deleteLater();
    updateTabPlaceholder();
    updateActionStates();
}

void MainWindow::onNextTab()
{
    if (m_tabs->count() > 1)
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
}

void MainWindow::onPrevTab()
{
    if (m_tabs->count() > 1)
        m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + m_tabs->count()) % m_tabs->count());
}

void MainWindow::onTerminalSessionFinished(int exitCode)
{
    Q_UNUSED(exitCode);
    auto *term = qobject_cast<TerminalWidget *>(sender());
    if (!term)
        return;

    // Deliberately not closing the tab. A box can die while you're looking
    // at a different tab entirely, and silently removing its terminal
    // loses both the fact that it happened and whatever was on screen when
    // it did. Mark it instead and let the user dismiss it.
    const int index = m_tabs->indexOf(term);
    if (index >= 0) {
        const QString name = term->property("boxName").toString();
        m_tabs->setTabIcon(index, Icons::statusDot(Theme::stopped()));
        m_tabs->setTabText(index, m_tabs->tabText(index) + QStringLiteral("  (ended)"));
        m_tabs->setTabToolTip(index, name + QStringLiteral(" — session ended; close this tab to dismiss"));

        // Clear wasRunning so this box is not offered for restart-after-reboot:
        // it exited on its own (not because the host shut down), so the user
        // did not lose state they did not expect to lose.
        BoxRecord rec = BoxRecord::load(name);
        if (rec.isValid() && rec.wasRunning) {
            rec.wasRunning = false;
            rec.save();
        }
    }
    refreshBoxes();
}

// --- box actions --------------------------------------------------------

// Guards the case where the conversation picked in the New Box dialog is
// already spoken for. Two containers resuming the same session both
// append to the same transcript in ~/.claude/projects, so a running
// owner is a hard no; a merely-known one is the user's call (it's a
// reasonable way to branch off an old box's conversation).
bool MainWindow::confirmConversationAdoption(const QString &sessionUuid)
{
    // Walks the *source* model for the same reason Purge does: a box
    // filtered out of the view is still running.
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (!row || row->status != BoxInfo::Status::Running)
            continue;
        if (BoxRecord::load(row->name).sessionUuid == sessionUuid) {
            QMessageBox::warning(this, QStringLiteral("New Box"),
                                 row->name + QStringLiteral(" is already running this conversation.\n\n"
                                 "Two boxes resuming the same session would write over each other's "
                                 "transcript -- close that box first, or pick another conversation."));
            return false;
        }
    }

    for (const BoxRecord &known : BoxRecord::loadAll()) {
        if (known.sessionUuid != sessionUuid)
            continue;
        const QString msg = QStringLiteral("Box %1 already tracks this conversation.\n\n"
            "Creating another box for it is fine as long as they don't run at the same "
            "time.\n\nProceed?").arg(known.name);
        return QMessageBox::question(this, QStringLiteral("New Box"), msg) == QMessageBox::Yes;
    }

    return true;
}

void MainWindow::offerResumeAfterReboot()
{
    // Collect boxes that were running before the last shutdown and aren't
    // running now. Skip the prompt entirely when any box is already running
    // (the machine wasn't rebooted -- the user just restarted the GUI).
    bool anyRunning = false;
    QStringList resumable;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (!row)
            continue;
        if (row->status == BoxInfo::Status::Running) {
            anyRunning = true;
            break;
        }
        if (row->status == BoxInfo::Status::Stopped) {
            const BoxRecord rec = BoxRecord::load(row->name);
            if (rec.isValid() && rec.wasRunning)
                resumable << row->name;
        }
    }
    if (anyRunning || resumable.isEmpty())
        return;

    const QString detail = resumable.size() == 1
        ? QStringLiteral("1 box was running before the last shutdown:\n\n  %1").arg(resumable.first())
        : QStringLiteral("%1 boxes were running before the last shutdown:\n\n  %2")
              .arg(resumable.size())
              .arg(resumable.join(QStringLiteral("\n  ")));

    QMessageBox msg(this);
    msg.setWindowTitle(QStringLiteral("Resume boxes?"));
    msg.setText(QStringLiteral("Start them now?"));
    msg.setInformativeText(detail);
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msg.setDefaultButton(QMessageBox::Yes);
    if (msg.exec() != QMessageBox::Yes)
        return;

    for (const QString &name : resumable)
        startBox(name, /*syncTranscriptTitle=*/false);
}

void MainWindow::onNew()
{
    NewBoxDialog dlg(this);
    dlg.setInitialDir(SetupWizard::defaultTargetDir());
    // Pre-fill port rows tentatively; advance the pointer only on accept.
    const QList<int> tentativePorts = setupPortAllocation(dlg);
    if (dlg.exec() != QDialog::Accepted)
        return;
    commitPortAllocation(tentativePorts);

    BoxRecord rec;
    rec.targetDir = dlg.targetDir();
    rec.conversationName = dlg.conversationName();
    rec.sessionUuid = dlg.sessionUuid(); // empty => createNew mints a new one
    rec.skipPermissions = dlg.skipPermissions();
    rec.model = dlg.model();
    rec.effort = dlg.effort();
    rec.ports = dlg.ports();
    rec.dirs = dlg.dirs();
    rec.sshRemoteRefs = dlg.sshRemoteRefs();

    if (!rec.sessionUuid.isEmpty() && !confirmConversationAdoption(rec.sessionUuid))
        return;

    QString error;
    if (!m_docker.createNew(rec, dlg.workspaceSubdir(), &error)) {
        QMessageBox::warning(this, QStringLiteral("New Box"),
                             QStringLiteral("Failed to start box:\n") + error);
        return;
    }

    refreshBoxes();
    openTerminalTab(rec.name, rec.conversationName);
}

void MainWindow::onEdit()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info)
        return;

    BoxRecord rec = BoxRecord::load(info->name);
    if (!rec.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Edit Box"),
                             QStringLiteral("No record found for ") + info->name
                             + QStringLiteral(" -- a box started outside this app can't be edited here."));
        return;
    }

    // Copied out before any modal dialog runs: `info` points into the
    // model's live vector, and the 3s refresh timer can replace that
    // vector out from under a modal event loop (same reason onRemove()
    // copies `name` out before its confirmation box, below).
    const bool wasRunning = info->status == BoxInfo::Status::Running;

    NewBoxDialog dlg(this);
    dlg.loadForEdit(rec);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Ports, mounts, model/effort and the permission flag are all baked
    // into the container at launch -- docker has no way to change any of
    // them on one that's already running, so a change to those needs a
    // restart to actually take effect. SSH forwards don't: they're a
    // separate host-side process synced from the record on every poll
    // (see syncTunnels), independent of the container's own lifecycle.
    const bool needsRestart = rec.ports != dlg.ports() || rec.dirs != dlg.dirs()
        || rec.skipPermissions != dlg.skipPermissions() || rec.model != dlg.model()
        || rec.effort != dlg.effort();

    rec.conversationName = dlg.conversationName();
    rec.skipPermissions = dlg.skipPermissions();
    rec.model = dlg.model();
    rec.effort = dlg.effort();
    rec.ports = dlg.ports();
    rec.dirs = dlg.dirs();
    rec.sshRemoteRefs = dlg.sshRemoteRefs();

    if (!rec.save()) {
        QMessageBox::warning(this, QStringLiteral("Edit Box"),
                             QStringLiteral("Failed to save changes for ") + rec.name);
        return;
    }

    if (wasRunning && needsRestart) {
        const QString msg = rec.name + QStringLiteral(
            " is running, and docker can't change port mappings, mounts, model/effort, or "
            "the permission flag on a live container -- they take effect the next time it "
            "(re)starts.\n\nRestart it now to apply them?");
        if (QMessageBox::question(this, QStringLiteral("Edit Box"), msg) == QMessageBox::Yes) {
            QString error;
            if (!m_docker.stop(rec.name, &error)) {
                QMessageBox::warning(this, QStringLiteral("Edit Box"),
                                     QStringLiteral("Failed to stop ") + rec.name + ":\n" + error);
            } else {
                closeTabForBox(rec.name);
                if (!m_docker.reopen(rec, &error))
                    QMessageBox::warning(this, QStringLiteral("Edit Box"),
                                         QStringLiteral("Failed to reopen:\n") + error);
            }
        }
    }

    refreshBoxes();
    syncTunnels(); // pick up an SSH-forward change immediately rather than waiting for the next poll
}

// Creates a brand-new box whose conversation starts as a copy of the
// selected box's history (DockerBackend::createNew's forkFromUuid). No
// confirmConversationAdoption() check here: unlike adopting an existing
// uuid, forking always mints a fresh one, so there's no shared transcript
// for two boxes to collide on.
void MainWindow::onFork()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info)
        return;

    const BoxRecord source = BoxRecord::load(info->name);
    if (!source.isValid() || source.sessionUuid.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Fork Conversation"),
                             QStringLiteral("No session recorded for ") + info->name
                             + QStringLiteral(" -- a box started outside this app can't be forked here."));
        return;
    }

    NewBoxDialog dlg(this);
    dlg.loadForFork(source);
    // Replace the source's ports with a fresh allocation -- the source's
    // ports are already in use if it's running, and two boxes sharing the
    // same docker -p bindings won't start. Tentative so cancelling does not
    // permanently consume port numbers.
    const QList<int> tentativePorts = setupPortAllocation(dlg);
    if (dlg.exec() != QDialog::Accepted)
        return;
    commitPortAllocation(tentativePorts);

    BoxRecord rec;
    rec.targetDir = dlg.targetDir();
    rec.conversationName = dlg.conversationName();
    // sessionUuid left empty -- createNew mints a fresh one and seeds it
    // from dlg.forkSourceUuid() below.
    rec.skipPermissions = dlg.skipPermissions();
    rec.model = dlg.model();
    rec.effort = dlg.effort();
    rec.ports = dlg.ports();
    rec.dirs = dlg.dirs();
    rec.sshRemoteRefs = dlg.sshRemoteRefs();

    QString error;
    if (!m_docker.createNew(rec, dlg.workspaceSubdir(), &error, dlg.forkSourceUuid())) {
        QMessageBox::warning(this, QStringLiteral("Fork Conversation"),
                             QStringLiteral("Failed to start box:\n") + error);
        return;
    }

    refreshBoxes();
    openTerminalTab(rec.name, rec.conversationName);
}

void MainWindow::startBox(const QString &name, bool syncTranscriptTitle)
{
    BoxRecord rec = BoxRecord::load(name);
    if (!rec.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Start"),
                             QStringLiteral("No record found for ") + name);
        return;
    }

    // Sync the conversation name from the transcript before reopening, but
    // only when the record still matches what was last written into that
    // transcript (rec.transcriptSyncedName) -- reopen() never passes
    // --name, so an Edit-dialog rename changes conversationName without
    // updating transcriptSyncedName, and diverges the two. Syncing
    // unconditionally in that case would silently revert the user's
    // rename back to the transcript's stale title on every Start.
    //
    // titleForUuid() reads the whole transcript (it can't safely stop
    // early: a custom-title/ai-title can appear at any point in the file,
    // and transcripts run to tens of MB), so syncTranscriptTitle lets a
    // caller starting several boxes at once (onOpen with a multi-selection,
    // onOpenAll, offerResumeAfterReboot) skip it and avoid a stall that's
    // proportional to transcript size times box count -- a single
    // interactive Start (the default) still gets the sync.
    if (syncTranscriptTitle && !rec.sessionUuid.isEmpty() && !rec.targetDir.isEmpty()
        && rec.conversationName == rec.transcriptSyncedName) {
        const QString transcriptTitle =
            ConversationCatalog::titleForUuid(rec.targetDir, rec.sessionUuid);
        if (!transcriptTitle.isEmpty() && transcriptTitle != rec.conversationName) {
            rec.conversationName = transcriptTitle;
            rec.transcriptSyncedName = transcriptTitle;
            rec.save();
        }
    }

    QString error;
    if (!m_docker.reopen(rec, &error)) {
        QMessageBox::warning(this, QStringLiteral("Start"),
                             QStringLiteral("Failed to reopen:\n") + error);
        return;
    }

    refreshBoxes();
    openTerminalTab(rec.name, rec.conversationName);
}

void MainWindow::onOpen()
{
    const QStringList names = [this] {
        QStringList r;
        for (const BoxInfo *info : selectedBoxInfos())
            if (info->status == BoxInfo::Status::Stopped)
                r << info->name;
        return r;
    }();
    // syncTranscriptTitle=false: multi-select means this can be several
    // boxes at once (see startBox()'s comment on why that matters); a
    // single-box selection still gets the correctness benefit via
    // onRowDoubleClicked's default-true call instead.
    for (const QString &name : names)
        startBox(name, /*syncTranscriptTitle=*/false);
}

void MainWindow::onClose()
{
    const QStringList names = [this] {
        QStringList r;
        for (const BoxInfo *info : selectedBoxInfos())
            if (info->status == BoxInfo::Status::Running)
                r << info->name;
        return r;
    }();
    QHash<QString, QString> errors;
    m_docker.stopMany(names, &errors);
    for (const QString &name : names) {
        if (errors.contains(name))
            QMessageBox::warning(this, QStringLiteral("Stop"),
                                 name + QStringLiteral(": ") + errors.value(name));
        else {
            closeTabForBox(name);
            stopTunnelsForBox(name);
        }
    }
    if (!names.isEmpty())
        refreshBoxes();
}

void MainWindow::onStopAll()
{
    QStringList names;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (const BoxInfo *row = m_model->boxAt(i);
            row && row->status == BoxInfo::Status::Running)
            names << row->name;
    }
    if (names.isEmpty())
        return;

    if (QMessageBox::question(
            this, QStringLiteral("Stop All"),
            QStringLiteral("Stop %1 running box(es)?").arg(names.size()))
        != QMessageBox::Yes)
        return;

    QHash<QString, QString> errors;
    m_docker.stopMany(names, &errors);
    for (const QString &name : names) {
        if (errors.contains(name))
            QMessageBox::warning(this, QStringLiteral("Stop All"),
                                 name + QStringLiteral(": ") + errors.value(name));
        else {
            closeTabForBox(name);
            stopTunnelsForBox(name);
        }
    }
    refreshBoxes();
}

void MainWindow::onOpenAll()
{
    QStringList names;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (!row || row->status != BoxInfo::Status::Stopped)
            continue;
        const BoxRecord rec = BoxRecord::load(row->name);
        if (rec.isValid() && rec.wasRunning)
            names << row->name;
    }
    if (names.isEmpty())
        return;

    for (const QString &name : names)
        startBox(name, /*syncTranscriptTitle=*/false);
}

void MainWindow::onOpenExternal()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status != BoxInfo::Status::Running)
        return;

#ifdef Q_OS_WIN
    // Untested on Windows -- see this repo's CLAUDE.md on cross-platform
    // parity; needs a Windows-side check before this is considered
    // confirmed, the way the ConPTY/PtySessionWin work was. The docker CLI
    // is a native Windows binary talking to the daemon over a named pipe
    // (see DockerApi::socketPath()), so it runs directly -- no WSL or
    // bash-on-the-host dependency, unlike the container's own `tmux
    // attach`, which runs *inside* the Linux container and is unaffected
    // by the host shell either way.
    //
    // Deliberately not routed through `cmd /k "<command string>"`: /K
    // re-parses its argument as a fresh command line, and this command
    // contains cmd.exe metacharacters (`|`, `>`) that cmd would then try
    // to interpret itself (as its own pipe/redirect operators) rather than
    // pass through to bash -- cmd's own quote-preservation rule only
    // applies when no such characters appear between the quotes, so it
    // strips them and misparses the line. Passing an argv list straight to
    // docker.exe instead means Qt builds the Win32 command line and no
    // shell ever re-tokenizes it: bash -c receives the script as one
    // literal argument, exactly as intended.
    const QStringList dockerArgs = {
        "exec", "-it", info->name, "bash", "-c",
        QStringLiteral("tmux attach 2>/dev/null || tmux")
    };

    // docker.exe launched directly, with no shell wrapper at all, is
    // tried first: it's the one path with no re-tokenizing step of any
    // kind between Qt's argv and the child process, so it's the most
    // trustworthy. A detached console-subsystem process started from
    // this console-less GUI app gets its own new console window
    // automatically, so this still opens a visible terminal; it just
    // won't stay open once the process exits the way `cmd /k` would
    // have (worth noting if `docker exec` fails immediately -- the
    // window can close before an error is readable).
    if (QProcess::startDetached(QStringLiteral("docker.exe"), dockerArgs))
        return;

    // Windows Terminal as a fallback, launching docker.exe via `--` (its
    // direct-command syntax, which also bypasses cmd.exe parsing). Tried
    // second rather than first: wt.exe's own positional-argument handling
    // has documented cases of not preserving a multi-word quoted argument
    // intact when rebuilding the child's command line, which could
    // word-split the bash -c script rather than pass it through whole --
    // a different, subtler failure than the cmd.exe bug this whole
    // rewrite exists to avoid.
    QStringList wtArgs = {"--", "docker.exe"};
    wtArgs += dockerArgs;
    if (QProcess::startDetached(QStringLiteral("wt.exe"), wtArgs))
        return;

    QMessageBox::warning(this, QStringLiteral("Open External Terminal"),
                         QStringLiteral("Could not launch a terminal.\n"
                         "Neither docker.exe nor wt.exe could be started."));
#else
    // Attach to the container and attach or start a tmux session.
    const QStringList innerCmd = {
        "bash", "-c",
        QStringLiteral("docker exec -it %1 bash -c 'tmux attach 2>/dev/null || tmux'")
            .arg(info->name)
    };

    // Try terminals in preference order: $TERMINAL env var, then common ones.
    // gnome-terminal wants `--` before the command; the rest accept `-e`.
    struct Spec { QString term; bool gnomeStyle; };
    const QList<Spec> candidates = {
        {qEnvironmentVariable("TERMINAL"), false},
        {QStringLiteral("x-terminal-emulator"), false},
        {QStringLiteral("gnome-terminal"),      true},
        {QStringLiteral("xterm"),               false},
        {QStringLiteral("kitty"),               false},
        {QStringLiteral("alacritty"),           false},
    };

    for (const Spec &s : candidates) {
        if (s.term.trimmed().isEmpty())
            continue;
        const QStringList args = s.gnomeStyle
            ? QStringList{"--"} + innerCmd
            : QStringList{"-e"} + innerCmd;
        if (QProcess::startDetached(s.term, args))
            return;
    }

    QMessageBox::warning(this, QStringLiteral("Open External Terminal"),
                         QStringLiteral("No terminal emulator found.\n"
                         "Set $TERMINAL, or install xterm or gnome-terminal."));
#endif
}

void MainWindow::onRemove()
{
    const QStringList names = [this] {
        QStringList r;
        for (const BoxInfo *info : selectedBoxInfos())
            if (info->status == BoxInfo::Status::Exited)
                r << info->name;
        return r;
    }();
    if (names.isEmpty())
        return;

    const QString msg = names.size() == 1
        ? QStringLiteral("Remove stopped container ") + names.first() + QStringLiteral("?")
        : QStringLiteral("Remove %1 stopped containers?").arg(names.size());
    if (QMessageBox::question(this, QStringLiteral("Remove"), msg) != QMessageBox::Yes)
        return;

    for (const QString &name : names) {
        QString error;
        if (!m_docker.remove(name, &error))
            QMessageBox::warning(this, QStringLiteral("Remove"),
                                 name + QStringLiteral(": ") + error);
    }
    refreshBoxes();
}

QString MainWindow::runningBoxForDir(const QString &dir) const
{
    // Deliberately walks the *source* model, not the proxy: a filtered-out
    // running box is still a running box, and this check must not depend
    // on what the user happens to have typed in the filter field.
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (row && row->status == BoxInfo::Status::Running && row->targetDir == dir)
            return row->name;
    }
    return QString();
}

void MainWindow::onPurge()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->targetDir.isEmpty())
        return;
    const QString dir = info->targetDir;

    // Refuse while any box for this directory is currently running.
    const QString runningName = runningBoxForDir(dir);
    if (!runningName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Purge"),
                             runningName + QStringLiteral(" is running against this directory -- close it first."));
        return;
    }

    // Claude's own path encoding, not a naive slash swap: it collapses
    // every non-alphanumeric byte, so a directory like app.odinhelp.com
    // lands in -home-...-app-odinhelp-com. Purging with '/'-only encoding
    // pointed at a path that never exists and silently left the
    // transcripts behind.
    const QString projectDir = ConversationCatalog::projectDirFor(dir);

    QStringList toRemove;
    if (QDir(projectDir).exists())
        toRemove << projectDir;

    const QList<BoxRecord> known = BoxRecord::loadAll();
    for (const BoxRecord &rec : known) {
        if (rec.targetDir == dir)
            toRemove << (BoxRecord::knownDir() + "/" + rec.name);
    }

    if (toRemove.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Purge"),
                                 QStringLiteral("Nothing tracked for ") + dir);
        return;
    }

    const QString msg = QStringLiteral("This will remove:\n\n") + toRemove.join('\n')
        + QStringLiteral("\n\nThe directory's own contents (") + dir + QStringLiteral(") are not touched.\n\nProceed?");
    if (QMessageBox::question(this, QStringLiteral("Purge"), msg) != QMessageBox::Yes)
        return;

    // Safety guard: only ever delete paths rooted under these two dirs,
    // regardless of what was computed above.
    const QString homeProjects = QDir::homePath() + "/.claude/projects/";
    const QString homeClaudeBox = QDir::homePath() + "/.claude-box/";

    for (const QString &path : toRemove) {
        if (!path.startsWith(homeProjects) && !path.startsWith(homeClaudeBox))
            continue;

        const QFileInfo fi(path);
        if (fi.isDir())
            QDir(path).removeRecursively();
        else if (fi.exists())
            QFile::remove(path);
    }

    refreshBoxes();
}

void MainWindow::onSetupWizard()
{
    SetupWizard dlg(this);
    dlg.exec();
}

void MainWindow::onManageSshRemotes()
{
    ManageSshRemotesDialog dlg(this);
    dlg.exec();
    // A rename/removal/host change while boxes are already Running should
    // take effect immediately, same as editing a box's own attachments
    // does -- not wait for the next 3s poll.
    syncTunnels();
    updateActionStates();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("About claude-box"),
        QStringLiteral("<h3>claude-box %1</h3>"
                       "<p>A dashboard for sandboxed Claude Code containers.</p>"
                       "<p style='color:#9297a0'>Boxes run detached under docker; closing a "
                       "terminal tab only detaches this client, while <b>Close</b> stops the "
                       "container. Conversations resume by session id, so a stopped box can be "
                       "reopened exactly where it left off.</p>"
                       "<p style='color:#9297a0'>Application shortcuts are all "
                       "<b>Ctrl+Shift</b> chords, leaving plain Ctrl keys to Claude Code "
                       "inside the terminal.</p>")
            .arg(QApplication::applicationVersion()));
}

// --- settings -----------------------------------------------------------

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(kGeometryKey, saveGeometry());
    s.setValue(kWindowStateKey, saveState());
    s.setValue(kOuterSplitKey, m_outerSplitter->saveState());
    s.setValue(kTopSplitKey, m_topSplitter->saveState());
    s.setValue(kDetailsKey, m_detailsAction->isChecked());
    s.setValue(kTableHeaderKey, m_table->horizontalHeader()->saveState());
}

void MainWindow::restoreSettings()
{
    QSettings s;
    if (s.contains(kGeometryKey))
        restoreGeometry(s.value(kGeometryKey).toByteArray());
    if (s.contains(kWindowStateKey))
        restoreState(s.value(kWindowStateKey).toByteArray());
    if (s.contains(kOuterSplitKey))
        m_outerSplitter->restoreState(s.value(kOuterSplitKey).toByteArray());
    if (s.contains(kTopSplitKey))
        m_topSplitter->restoreState(s.value(kTopSplitKey).toByteArray());
    // Column widths set up in buildUi() are just the first-run default --
    // restoreState() (when there's something saved) overrides them with
    // whatever the user last dragged them to, resize mode included, so
    // this has to come after that setup, not before it.
    if (s.contains(kTableHeaderKey))
        m_table->horizontalHeader()->restoreState(s.value(kTableHeaderKey).toByteArray());

    const bool detailsVisible = s.value(kDetailsKey, true).toBool();
    m_detailsAction->setChecked(detailsVisible);
    m_details->setVisible(detailsVisible);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();

    // The worker holds a `this` pointer; letting it outlive the window
    // would be a use-after-free.
    m_refreshTimer->stop();
    if (m_refreshWatcher->isRunning()) {
        m_refreshWatcher->disconnect(this); // don't touch widgets while tearing down
        m_refreshWatcher->waitForFinished();
    }

    // Explicit rather than left to ~QObject's child-deletion order: an ssh
    // tunnel is an external process, and terminating it here (not just
    // relying on the eventual destructor) is what keeps one from
    // outliving the window it belongs to.
    for (const QString &key : m_tunnels.keys())
        stopTunnel(key);

    QMainWindow::closeEvent(event);
}
