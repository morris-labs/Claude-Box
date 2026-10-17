#include "ProjectPanel.h"

#include "BoxRecord.h"
#include "PortAllocator.h"
#include "ProjectRecord.h"
#include "Theme.h"

#include <QDesktopServices>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kRootRole = Qt::UserRole + 1;

struct Tally {
    int total = 0;
    int running = 0;
};
} // namespace

ProjectPanel::ProjectPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *heading = new QLabel(QStringLiteral("Projects"), this);
    heading->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(Theme::dimText().name()));

    m_list = new QListWidget(this);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setUniformItemSizes(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 4, 4);
    layout->setSpacing(6);
    layout->addWidget(heading);
    layout->addWidget(m_list, 1);

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!m_rebuilding && item)
            emit projectSelected(item->data(kRootRole).toString());
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ProjectPanel::showContextMenu);
}

QString ProjectPanel::currentRoot() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(kRootRole).toString() : QString();
}

void ProjectPanel::selectRoot(const QString &root)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kRootRole).toString() == root) {
            m_list->setCurrentRow(i);
            return;
        }
    }
    m_list->setCurrentRow(0); // gone: fall back to "All projects"
}

void ProjectPanel::setBoxes(const QList<BoxInfo> &boxes)
{
    QHash<QString, Tally> tally;
    for (const BoxInfo &b : boxes) {
        const QString root = ProjectRecord::normalizeRoot(b.targetDir);
        if (root.isEmpty())
            continue;
        Tally &t = tally[root];
        ++t.total;
        if (b.status == BoxInfo::Status::Running)
            ++t.running;
    }
    // A saved project with no boxes is still a project.
    for (const ProjectRecord &rec : ProjectRecord::loadAll())
        tally[rec.root];

    QList<ProjectRecord> projects;
    int totalBoxes = 0;
    int totalRunning = 0;
    for (auto it = tally.constBegin(); it != tally.constEnd(); ++it) {
        projects << ProjectRecord::forRoot(it.key());
        totalBoxes += it.value().total;
        totalRunning += it.value().running;
    }
    std::sort(projects.begin(), projects.end(), [](const ProjectRecord &a, const ProjectRecord &b) {
        return a.name().compare(b.name(), Qt::CaseInsensitive) < 0;
    });

    const QString keep = currentRoot();
    m_rebuilding = true;
    m_list->clear();

    auto *all = new QListWidgetItem(QStringLiteral("All projects  (%1)").arg(totalBoxes));
    all->setData(kRootRole, QString());
    all->setToolTip(QStringLiteral("%1 running").arg(totalRunning));
    m_list->addItem(all);

    for (const ProjectRecord &p : std::as_const(projects)) {
        const Tally t = tally.value(p.root);
        auto *item = new QListWidgetItem(
            t.running > 0 ? QStringLiteral("%1  (%2/%3)").arg(p.name()).arg(t.running).arg(t.total)
                          : QStringLiteral("%1  (%2)").arg(p.name()).arg(t.total));
        item->setData(kRootRole, p.root);
        item->setToolTip(QStringLiteral("%1\n%2 conversation(s), %3 running%4")
                             .arg(p.root).arg(t.total).arg(t.running)
                             .arg(p.primaryRepo.isEmpty() ? QString() : QStringLiteral("\nGit repository")));
        m_list->addItem(item);
    }
    m_rebuilding = false;

    // Re-select without re-announcing when nothing changed, so the 3-second
    // refresh does not keep resetting the table's selection.
    int row = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kRootRole).toString() == keep)
            row = i;
    }
    m_rebuilding = true;
    m_list->setCurrentRow(row);
    m_rebuilding = false;
    if (m_list->item(row)->data(kRootRole).toString() != keep)
        emit projectSelected(m_list->item(row)->data(kRootRole).toString());
}

void ProjectPanel::showContextMenu(const QPoint &pos)
{
    const QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const QString root = item->data(kRootRole).toString();
    if (root.isEmpty())
        return;

    const ProjectRecord project = ProjectRecord::forRoot(root);
    QMenu menu(this);
    QAction *rename = menu.addAction(QStringLiteral("Rename project…"));
    QAction *open = menu.addAction(QStringLiteral("Open folder"));
    menu.addSeparator();
    QAction *share = menu.addAction(QStringLiteral("One container for this project"));
    share->setCheckable(true);
    share->setChecked(project.sharedContainer);
    int classic = 0;
    for (const BoxRecord &box : BoxRecord::loadAll()) {
        if (ProjectRecord::normalizeRoot(box.targetDir) == root && !box.isShared())
            ++classic;
    }
    QAction *convert = menu.addAction(QStringLiteral("Move %1 existing box(es) into the shared container…").arg(classic));
    convert->setEnabled(project.sharedContainer && classic > 0);
    QAction *mounts = menu.addAction(QStringLiteral("Project mounts…"));
    mounts->setEnabled(project.sharedContainer);
    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == rename)
        renameProject(root);
    else if (chosen == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(root));
    else if (chosen == share)
        setSharedContainer(root, share->isChecked());
    else if (chosen == convert)
        emit convertToSharedRequested(root);
    else if (chosen == mounts)
        editMounts(root);
}

void ProjectPanel::renameProject(const QString &root)
{
    ProjectRecord rec = ProjectRecord::forRoot(root);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Rename project"),
        QStringLiteral("Name (leave empty to use the folder name):"),
        QLineEdit::Normal, rec.displayName, &ok).trimmed();
    if (!ok)
        return;
    rec.displayName = name;
    if (!rec.save()) {
        QMessageBox::warning(this, QStringLiteral("Rename project"),
                             QStringLiteral("Could not save the new name to ") + ProjectRecord::projectsDir());
        return;
    }
    emit projectRenamed();
}

// Sharing is the default; this is the opt-out. Turning it off affects only
// conversations started afterwards, and the project keeps its pool of ports.
void ProjectPanel::setSharedContainer(const QString &root, bool on)
{
    ProjectRecord rec = ProjectRecord::forRoot(root);
    rec.sharedContainer = on;
    if (!rec.save()) {
        QMessageBox::warning(this, QStringLiteral("Shared container"),
                             QStringLiteral("Could not save the project."));
        return;
    }
    emit projectRenamed(); // rebuilds the list
}

void ProjectPanel::editMounts(const QString &root)
{
    ProjectRecord rec = ProjectRecord::forRoot(root);
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, QStringLiteral("Project mounts"),
        QStringLiteral("One mount per line, HOST_PATH or HOST_PATH:CONTAINER_PATH.\n"
                       "Applied when the container is next created, which is when no conversation "
                       "in this project is running."),
        rec.dirs.join('\n'), &ok);
    if (!ok)
        return;
    rec.dirs.clear();
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            rec.dirs << t;
    }
    if (!rec.save())
        QMessageBox::warning(this, QStringLiteral("Project mounts"), QStringLiteral("Could not save the project."));
}
