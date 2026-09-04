#include "ManageSshRemotesDialog.h"

#include "BoxRecord.h"
#include "SshForwardsDialog.h"
#include "SshRemoteCatalog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString summaryFor(const SshRemote &r)
{
    return QStringLiteral("%1  —  %2  (%3 forward%4)")
        .arg(r.name, r.host.isEmpty() ? QStringLiteral("(no target set)") : r.host)
        .arg(r.forwards.size())
        .arg(r.forwards.size() == 1 ? QString() : QStringLiteral("s"));
}

// Every box currently attached to `remoteName`, for the "still in use"
// warning before a remote is removed. Deliberately not restricted to
// Running boxes -- a Stopped or Known one would just start failing to
// find it next time it runs, which is worth knowing about too.
QStringList boxesUsing(const QString &remoteName)
{
    QStringList names;
    for (const BoxRecord &rec : BoxRecord::loadAll()) {
        if (rec.sshRemoteRefs.contains(remoteName))
            names << rec.name;
    }
    return names;
}
}

ManageSshRemotesDialog::ManageSshRemotesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Manage SSH Remotes");
    resize(480, 380);

    auto *mainLayout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        "A remote defined here (name, ssh target, and forwards) can be attached to any number "
        "of boxes -- they share one background ssh connection instead of each opening a "
        "redundant one to the same host.", this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ManageSshRemotesDialog::editSelectedRemote);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const bool has = m_list->currentRow() >= 0;
        m_editButton->setEnabled(has);
        m_removeButton->setEnabled(has);
    });
    mainLayout->addWidget(m_list, 1);

    auto *buttonRow = new QHBoxLayout();
    auto *addButton = new QPushButton("Add…", this);
    m_editButton = new QPushButton("Edit…", this);
    m_removeButton = new QPushButton("Remove", this);
    m_editButton->setEnabled(false);
    m_removeButton->setEnabled(false);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    mainLayout->addLayout(buttonRow);

    connect(addButton, &QPushButton::clicked, this, &ManageSshRemotesDialog::addRemote);
    connect(m_editButton, &QPushButton::clicked, this, &ManageSshRemotesDialog::editSelectedRemote);
    connect(m_removeButton, &QPushButton::clicked, this, &ManageSshRemotesDialog::removeSelectedRemote);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &ManageSshRemotesDialog::accept);
    // Close is the only action -- everything here saves itself immediately
    // (see addRemote()/editSelectedRemote()/removeSelectedRemote()), so
    // there's no separate "OK" state to commit.
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &ManageSshRemotesDialog::accept);
    mainLayout->addWidget(buttons);

    refreshList();
}

void ManageSshRemotesDialog::refreshList()
{
    m_list->clear();
    for (const SshRemote &r : SshRemoteCatalog::loadAll()) {
        auto *item = new QListWidgetItem(summaryFor(r), m_list);
        item->setData(Qt::UserRole, r.name);
    }
    m_editButton->setEnabled(false);
    m_removeButton->setEnabled(false);
}

void ManageSshRemotesDialog::addRemote()
{
    SshForwardsDialog dlg(this);
    QStringList existing;
    for (const SshRemote &r : SshRemoteCatalog::loadAll())
        existing << r.name;
    dlg.setReservedNames(existing);
    if (dlg.exec() != QDialog::Accepted)
        return;

    SshRemote remote;
    remote.name = dlg.remoteName();
    remote.host = dlg.sshHost();
    remote.identity = dlg.sshIdentity();
    remote.forwards = dlg.sshForwards();

    const QString clash = SshRemoteCatalog::collidingName(remote.name);
    if (!clash.isEmpty()) {
        QMessageBox::warning(this, "Manage SSH Remotes",
            QStringLiteral("\"%1\" is too similar to the existing remote \"%2\" -- they'd be stored "
                           "in the same file. Pick a more distinct name.").arg(remote.name, clash));
        return;
    }

    SshRemoteCatalog::save(remote);
    refreshList();
}

void ManageSshRemotesDialog::editSelectedRemote()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const QString oldName = item->data(Qt::UserRole).toString();
    const SshRemote existing = SshRemoteCatalog::load(oldName);

    SshForwardsDialog dlg(this);
    dlg.setConfig(existing.name, existing.host, existing.identity, existing.forwards);
    QStringList reserved;
    for (const SshRemote &r : SshRemoteCatalog::loadAll()) {
        if (r.name != oldName)
            reserved << r.name;
    }
    dlg.setReservedNames(reserved);
    if (dlg.exec() != QDialog::Accepted)
        return;

    SshRemote remote;
    remote.name = dlg.remoteName();
    remote.host = dlg.sshHost();
    remote.identity = dlg.sshIdentity();
    remote.forwards = dlg.sshForwards();

    const QString clash = SshRemoteCatalog::collidingName(remote.name, oldName);
    if (!clash.isEmpty()) {
        QMessageBox::warning(this, "Manage SSH Remotes",
            QStringLiteral("\"%1\" is too similar to the existing remote \"%2\" -- they'd be stored "
                           "in the same file. Pick a more distinct name.").arg(remote.name, clash));
        return;
    }

    SshRemoteCatalog::save(remote, oldName);

    // A rename needs every box that referenced the old name repointed at
    // the new one, or they'd silently stop resolving to anything.
    if (remote.name != oldName) {
        for (BoxRecord rec : BoxRecord::loadAll()) {
            const int idx = rec.sshRemoteRefs.indexOf(oldName);
            if (idx < 0)
                continue;
            rec.sshRemoteRefs[idx] = remote.name;
            rec.save();
        }
    }

    refreshList();
}

void ManageSshRemotesDialog::removeSelectedRemote()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();

    const QStringList users = boxesUsing(name);
    QString msg = QStringLiteral("Remove SSH remote \"%1\"?").arg(name);
    if (!users.isEmpty()) {
        msg += QStringLiteral("\n\n%1 box%2 still attached to it: %3\n\nThey'll simply have nothing to "
                              "tunnel through it any more -- this doesn't touch the boxes themselves.")
                   .arg(users.size())
                   .arg(users.size() == 1 ? QString() : QStringLiteral("es"))
                   .arg(users.join(", "));
    }
    if (QMessageBox::question(this, "Remove SSH Remote", msg) != QMessageBox::Yes)
        return;

    SshRemoteCatalog::remove(name);
    refreshList();
}
