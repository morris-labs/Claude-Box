#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;

// Manages SshRemoteCatalog: the host-wide list of named SSH remotes any
// box can attach to. Reachable from the menu bar (File -> Manage SSH
// Remotes...) and from NewBoxDialog's Remote Port Forwarding section
// ("Manage Remotes..."), so a remote can be created without leaving the
// New/Edit Box flow.
//
// Each entry is edited as a whole via SshForwardsDialog, same as a box's
// own remotes used to be before they moved into the catalog -- this
// dialog is just the list-management shell around that, plus catalog-
// specific bookkeeping (name uniqueness, warning before removing an
// attached-to remote) that doesn't belong in the editor itself.
class ManageSshRemotesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManageSshRemotesDialog(QWidget *parent = nullptr);

private slots:
    void addRemote();
    void editSelectedRemote();
    void removeSelectedRemote();

private:
    void refreshList();

    QListWidget *m_list = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_removeButton = nullptr;
};
