#include "MainWindow.h"

#include "BoxRecord.h"
#include "NewBoxDialog.h"
#include "TerminalWidget.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("claude-box");
    resize(1100, 750);

    m_model = new BoxTableModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_table);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);

    QToolBar *toolbar = addToolBar("Actions");
    m_newAction = toolbar->addAction("New", this, &MainWindow::onNew);
    m_openAction = toolbar->addAction("Open", this, &MainWindow::onOpen);
    m_closeAction = toolbar->addAction("Close", this, &MainWindow::onClose);
    m_removeAction = toolbar->addAction("Remove", this, &MainWindow::onRemove);
    m_purgeAction = toolbar->addAction("Purge…", this, &MainWindow::onPurge);
    toolbar->addSeparator();
    toolbar->addAction("Refresh Now", this, &MainWindow::refreshBoxes);

    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::onRowDoubleClicked);
    connect(m_table, &QTableView::customContextMenuRequested, this, &MainWindow::showTableContextMenu);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateActionStates);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        // Closing a tab only detaches our local `docker attach` client --
        // the container itself keeps running (see PtySession/TerminalWidget).
        QWidget *w = m_tabs->widget(index);
        m_tabs->removeTab(index);
        if (w)
            w->deleteLater();
    });

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(3000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshBoxes);
    m_refreshTimer->start();

    refreshBoxes();
    updateActionStates();
}

const BoxInfo *MainWindow::selectedBoxInfo() const
{
    if (!m_table->selectionModel())
        return nullptr;
    const QModelIndexList sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return nullptr;
    return m_model->boxAt(sel.first().row());
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
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *info = m_model->boxAt(i);
        if (info && info->name == name) {
            m_table->selectRow(i);
            return;
        }
    }
}

void MainWindow::refreshBoxes()
{
    const QString selected = currentSelectedName();
    m_model->setBoxes(m_docker.listBoxes());
    reselectByName(selected);
    updateActionStates();
}

void MainWindow::updateActionStates()
{
    const BoxInfo *info = selectedBoxInfo();
    m_openAction->setEnabled(info && info->status == BoxInfo::Status::Known);
    m_closeAction->setEnabled(info && info->status == BoxInfo::Status::Running);
    m_removeAction->setEnabled(info && info->status == BoxInfo::Status::Stopped);
    m_purgeAction->setEnabled(info && !info->targetDir.isEmpty());
}

void MainWindow::showTableContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.addAction(m_openAction);
    menu.addAction(m_closeAction);
    menu.addAction(m_removeAction);
    menu.addAction(m_purgeAction);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::onRowDoubleClicked(const QModelIndex &index)
{
    const BoxInfo *info = m_model->boxAt(index.row());
    if (!info)
        return;

    if (info->status == BoxInfo::Status::Known)
        openKnownBox(info->name);
    else if (info->status == BoxInfo::Status::Running)
        openTerminalTab(info->name, info->conversationName.isEmpty() ? info->name : info->conversationName);
}

void MainWindow::openTerminalTab(const QString &name, const QString &title)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property("boxName").toString() == name) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto *term = new TerminalWidget(m_tabs);
    term->setProperty("boxName", name);
    connect(term, &TerminalWidget::sessionFinished, this, &MainWindow::onTerminalSessionFinished);

    if (!term->attachToContainer(name)) {
        QMessageBox::warning(this, "Open", "Failed to attach to " + name);
        term->deleteLater();
        return;
    }

    const int index = m_tabs->addTab(term, title.isEmpty() ? name : title);
    m_tabs->setCurrentIndex(index);
    term->setFocus();
}

void MainWindow::closeTabForBox(const QString &name)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->property("boxName").toString() == name) {
            QWidget *w = m_tabs->widget(i);
            m_tabs->removeTab(i);
            w->deleteLater();
            return;
        }
    }
}

void MainWindow::onTerminalSessionFinished(int exitCode)
{
    Q_UNUSED(exitCode);
    auto *term = qobject_cast<TerminalWidget *>(sender());
    if (!term)
        return;
    const int index = m_tabs->indexOf(term);
    if (index >= 0) {
        m_tabs->removeTab(index);
        term->deleteLater();
    }
    refreshBoxes();
}

void MainWindow::onNew()
{
    NewBoxDialog dlg(this);
    dlg.setInitialDir(QDir::homePath());
    if (dlg.exec() != QDialog::Accepted)
        return;

    BoxRecord rec;
    rec.targetDir = dlg.targetDir();
    rec.conversationName = dlg.conversationName();
    rec.yolo = dlg.yolo();
    rec.rc = dlg.rc();
    rec.ports = dlg.ports();
    rec.dirs = dlg.dirs();

    QString error;
    if (!m_docker.createNew(rec, &error)) {
        QMessageBox::warning(this, "New Box", "Failed to start box:\n" + error);
        return;
    }

    refreshBoxes();
    openTerminalTab(rec.name, rec.conversationName);
}

void MainWindow::openKnownBox(const QString &name)
{
    const BoxRecord rec = BoxRecord::load(name);
    if (!rec.isValid()) {
        QMessageBox::warning(this, "Open", "No record found for " + name);
        return;
    }

    QString error;
    if (!m_docker.reopen(rec, &error)) {
        QMessageBox::warning(this, "Open", "Failed to reopen:\n" + error);
        return;
    }

    refreshBoxes();
    openTerminalTab(rec.name, rec.conversationName);
}

void MainWindow::onOpen()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status != BoxInfo::Status::Known)
        return;
    openKnownBox(info->name);
}

void MainWindow::onClose()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status != BoxInfo::Status::Running)
        return;

    const QString name = info->name;
    QString error;
    if (!m_docker.stop(name, &error)) {
        QMessageBox::warning(this, "Close", "Failed to stop:\n" + error);
        return;
    }

    closeTabForBox(name);
    refreshBoxes();
}

void MainWindow::onRemove()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->status != BoxInfo::Status::Stopped)
        return;

    const QString name = info->name;
    if (QMessageBox::question(this, "Remove", "Remove stopped container " + name + "?")
        != QMessageBox::Yes)
        return;

    QString error;
    if (!m_docker.remove(name, &error))
        QMessageBox::warning(this, "Remove", "Failed to remove:\n" + error);

    refreshBoxes();
}

void MainWindow::onPurge()
{
    const BoxInfo *info = selectedBoxInfo();
    if (!info || info->targetDir.isEmpty())
        return;
    const QString dir = info->targetDir;

    // Refuse while any box for this directory is currently running.
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const BoxInfo *row = m_model->boxAt(i);
        if (row && row->status == BoxInfo::Status::Running && row->targetDir == dir) {
            QMessageBox::warning(this, "Purge",
                                  row->name + " is running against this directory -- close it first.");
            return;
        }
    }

    QString encoded = dir;
    encoded.replace('/', '-');
    const QString projectDir = QDir::homePath() + "/.claude/projects/" + encoded;

    QStringList toRemove;
    if (QDir(projectDir).exists())
        toRemove << projectDir;

    const QList<BoxRecord> known = BoxRecord::loadAll();
    for (const BoxRecord &rec : known) {
        if (rec.targetDir == dir)
            toRemove << (BoxRecord::knownDir() + "/" + rec.name);
    }

    if (toRemove.isEmpty()) {
        QMessageBox::information(this, "Purge", "Nothing tracked for " + dir);
        return;
    }

    const QString msg = "This will remove:\n\n" + toRemove.join('\n')
        + "\n\nThe directory's own contents (" + dir + ") are not touched.\n\nProceed?";
    if (QMessageBox::question(this, "Purge", msg) != QMessageBox::Yes)
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
