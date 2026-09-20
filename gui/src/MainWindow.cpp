#include "MainWindow.h"

#include "BoxDetailsPanel.h"
#include "BoxRecord.h"
#include "Icons.h"
#include "ConversationCatalog.h"
#include "ManageSshRemotesDialog.h"
#include "NewBoxDialog.h"
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
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int kRefreshIntervalMs = 3000;

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

    m_openAction = new QAction(Icons::open(), QStringLiteral("&Open"), this);
    m_openAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    m_openAction->setStatusTip(QStringLiteral("Restart this box and resume its conversation"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_closeAction = new QAction(Icons::closeBox(), QStringLiteral("&Close"), this);
    m_closeAction->setStatusTip(QStringLiteral("Stop the selected running container(s)"));
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::onClose);

    m_stopAllAction = new QAction(QStringLiteral("Stop &All Running"), this);
    m_stopAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    m_stopAllAction->setStatusTip(QStringLiteral("Stop every running container"));
    connect(m_stopAllAction, &QAction::triggered, this, &MainWindow::onStopAll);

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
    // stopped Status/Name/Details from being resized before. Starting
    // widths mirror what those modes used to compute, so this is a
    // one-time-editable version of the old layout, not a fresh guess: the
    // narrow columns get resizeColumnToContents()'s answer and Directory
    // (previously the one Stretch column, and still the field most likely
    // to need extra room) starts wide. restoreSettings() overwrites all of
    // this from QSettings on every launch after the first.
    QHeaderView *header = m_table->horizontalHeader();
    header->setStretchLastSection(false);
    for (int col = 0; col < m_model->columnCount(); ++col)
        header->setSectionResizeMode(col, QHeaderView::Interactive);
    m_table->resizeColumnToContents(0); // Status
    m_table->resizeColumnToContents(1); // Name
    header->resizeSection(2, 190);      // Conversation
    header->resizeSection(3, 320);      // Directory
    m_table->resizeColumnToContents(4); // Details

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

    m_outerSplitter = new QSplitter(Qt::Vertical, this);
    m_outerSplitter->addWidget(m_topSplitter);
    m_outerSplitter->addWidget(m_tabStack);
    m_outerSplitter->setStretchFactor(0, 1);
    m_outerSplitter->setStretchFactor(1, 2);
    m_outerSplitter->setChildrenCollapsible(false);
    m_outerSplitter->setSizes({440, 360});
    setCentralWidget(m_outerSplitter);

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
    boxMenu->addAction(m_openExternalAction);
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

void MainWindow::reselectByName(const QString &name)
{
    if (name.isEmpty())
        return;
    for (int i = 0; i < m_proxy->rowCount(); ++i) {
        const BoxInfo *info = m_model->boxAt(m_proxy->mapToSource(m_proxy->index(i, 0)).row());
        if (info && info->name == name) {
            m_table->selectRow(i);
            return;
        }
    }
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

    const QString selected = currentSelectedName();
    m_model->setBoxes(m_refreshWatcher->result());
    reselectByName(selected);
    syncTunnels();

    if (wasFirstLoad)
        QTimer::singleShot(0, this, &MainWindow::refreshBoxes);

    int running = 0, stopped = 0, known = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *info = m_model->boxAt(i);
        if (!info)
            continue;
        switch (info->status) {
        case BoxInfo::Status::Running: ++running; break;
        case BoxInfo::Status::Stopped: ++stopped; break;
        case BoxInfo::Status::Known:   ++known;   break;
        }
    }

    QStringList parts;
    parts << QStringLiteral("%1 running").arg(running);
    if (stopped > 0)
        parts << QStringLiteral("%1 stopped").arg(stopped);
    parts << QStringLiteral("%1 not running").arg(known);
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

    bool anyRunning = false, anyKnown = false, anyStopped = false;
    for (const BoxInfo *info : sel) {
        if (info->status == BoxInfo::Status::Running) anyRunning = true;
        if (info->status == BoxInfo::Status::Known)   anyKnown   = true;
        if (info->status == BoxInfo::Status::Stopped) anyStopped = true;
    }

    // Single-item actions: require exactly one selection.
    m_editAction->setEnabled(single && rec.isValid());
    m_forkAction->setEnabled(single && rec.isValid() && !rec.sessionUuid.isEmpty());
    m_purgeAction->setEnabled(single && !single->targetDir.isEmpty());
    m_openExternalAction->setEnabled(single && single->status == BoxInfo::Status::Running);

    // Multi-item actions: any matching selection is enough.
    m_openAction->setEnabled(anyKnown);
    m_closeAction->setEnabled(anyRunning);
    // Remove is only safe when nothing in the selection is still running or
    // just known (no container to rm): enable only for pure stopped sets.
    m_removeAction->setEnabled(anyStopped && !anyRunning && !anyKnown);

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

    if (info->status == BoxInfo::Status::Known)
        openKnownBox(info->name);
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
        QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Failed to attach to ") + name);
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

void MainWindow::onNew()
{
    NewBoxDialog dlg(this);
    dlg.setInitialDir(SetupWizard::defaultTargetDir());
    if (dlg.exec() != QDialog::Accepted)
        return;

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
    if (dlg.exec() != QDialog::Accepted)
        return;

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

void MainWindow::openKnownBox(const QString &name)
{
    BoxRecord rec = BoxRecord::load(name);
    if (!rec.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Open"),
                             QStringLiteral("No record found for ") + name);
        return;
    }

    // Sync the conversation name from the transcript before reopening.
    // Claude may have auto-generated a title since the record was created
    // (or a different account may have renamed it). This keeps --name in
    // sync with what the transcript actually says, which is what the user
    // expects to see inside the Claude TUI.
    if (!rec.sessionUuid.isEmpty() && !rec.targetDir.isEmpty()) {
        const QString transcriptTitle =
            ConversationCatalog::titleForUuid(rec.targetDir, rec.sessionUuid);
        if (!transcriptTitle.isEmpty() && transcriptTitle != rec.conversationName) {
            rec.conversationName = transcriptTitle;
            rec.save();
        }
    }

    QString error;
    if (!m_docker.reopen(rec, &error)) {
        QMessageBox::warning(this, QStringLiteral("Open"),
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
            if (info->status == BoxInfo::Status::Known)
                r << info->name;
        return r;
    }();
    for (const QString &name : names)
        openKnownBox(name);
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
    for (const QString &name : names) {
        QString error;
        if (!m_docker.stop(name, &error))
            QMessageBox::warning(this, QStringLiteral("Close"),
                                 name + QStringLiteral(": ") + error);
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

    for (const QString &name : names) {
        QString error;
        if (!m_docker.stop(name, &error))
            QMessageBox::warning(this, QStringLiteral("Stop All"),
                                 name + QStringLiteral(": ") + error);
        else {
            closeTabForBox(name);
            stopTunnelsForBox(name);
        }
    }
    refreshBoxes();
}

void MainWindow::onOpenExternal()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status != BoxInfo::Status::Running)
        return;

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
}

void MainWindow::onRemove()
{
    const QStringList names = [this] {
        QStringList r;
        for (const BoxInfo *info : selectedBoxInfos())
            if (info->status == BoxInfo::Status::Stopped)
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

void MainWindow::onPurge()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->targetDir.isEmpty())
        return;
    const QString dir = info->targetDir;

    // Refuse while any box for this directory is currently running.
    // Deliberately walks the *source* model, not the proxy: a filtered-out
    // running box is still a running box, and this check must not depend
    // on what the user happens to have typed in the filter field.
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (row && row->status == BoxInfo::Status::Running && row->targetDir == dir) {
            QMessageBox::warning(this, QStringLiteral("Purge"),
                                 row->name + QStringLiteral(" is running against this directory -- close it first."));
            return;
        }
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
