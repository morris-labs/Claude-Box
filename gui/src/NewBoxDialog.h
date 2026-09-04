#pragma once

#include "BoxRecord.h" // for BoxRecord, taken by loadForEdit()

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QCheckBox;
class QListWidget;
class QDialogButtonBox;
class QPushButton;
class QVBoxLayout;
class CollapsibleSection;

// Form for creating a new box: target directory, which conversation to
// run in it, name, model/effort, the permission-bypass toggle, and three
// collapsed-by-default sections -- Local Port Forwarding (docker -p),
// Remote Port Forwarding (any number of SSH tunnels, each configured as a
// whole in SshForwardsDialog), and Add folders to sandbox (extra -v
// mounts) -- in that order.
//
// The conversation picker lists every transcript Claude already has for
// the chosen directory (see ConversationCatalog), so a box can adopt a
// conversation that was started outside this app -- from a bare `claude`
// in a terminal, say -- instead of only ever starting a fresh one.
//
// Port and mount entries are collected as separate host/container fields
// and only joined into docker's "A:B" syntax on the way out: the syntax
// is an implementation detail of the `-p`/`-v` flags, not something the
// form should make anyone type. There is no remote-control toggle
// because --remote-control is always passed (see
// DockerBackend::baseClaudeArgs).
//
// Also doubles as the *edit* form for an existing box (loadForEdit()):
// same fields, minus the ones that aren't safe to change after creation
// (target directory, which conversation is resumed). MainWindow::onEdit
// is the one that decides what to do with the result -- update the record
// only, or also restart the container if it's running and something that
// needs a restart changed.
//
// Purely a form -- MainWindow is the one that turns the result into a
// BoxRecord and calls DockerBackend::createNew (or, in edit mode, saves
// over an existing record).
class NewBoxDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewBoxDialog(QWidget *parent = nullptr);

    void setInitialDir(const QString &dir);

    // Switches the dialog into edit mode for an existing box: locks the
    // directory and conversation picker (neither is safe to change after
    // creation) and prefills everything else from `rec`.
    void loadForEdit(const BoxRecord &rec);
    bool isEditMode() const { return m_editMode; }

    QString targetDir() const;
    QString conversationName() const;
    // Empty means "start a fresh conversation"; otherwise the uuid of an
    // existing transcript the new box should resume.
    QString sessionUuid() const;
    bool skipPermissions() const;
    // True when the agent should get a subfolder of the target directory
    // named after the conversation, instead of working in the tree root.
    bool workspaceSubdir() const;
    // Both start out showing what the settings files already resolve to,
    // and are always passed on to claude explicitly.
    QString model() const;
    QString effort() const;
    QStringList ports() const; // "HOST:CONTAINER"
    QStringList dirs() const;  // "HOSTPATH:CONTAINERPATH"

    // Names of SshRemoteCatalog entries this box attaches to -- see
    // BoxRecord::sshRemoteRefs. Managing what a remote actually connects to
    // (host/identity/forwards) happens in ManageSshRemotesDialog, not here;
    // this dialog only picks which of the catalog's remotes this box wants.
    QStringList sshRemoteRefs() const;

private slots:
    void browseForDir();
    void browseForMountDir();
    void reloadForDirectory();
    void onConversationChanged(int index);
    void updateWorkspaceHint();
    void addPort();
    void removeSelectedPort();
    void addDirMount();
    void removeSelectedDirMount();
    void openManageRemotes();
    void tryAccept();

private:
    void rememberWorkspaceChoice();
    // Repopulates m_remoteList from SshRemoteCatalog::loadAll(), ticking
    // the rows named in m_checkedRemoteNames.
    void refreshRemoteList();
    // Folds the widget's current tick state back into m_checkedRemoteNames
    // (so a Manage Remotes round trip doesn't drop this box's attachments
    // when the list is rebuilt). Not called from the loadForEdit() seeding
    // path -- see its definition.
    void syncCheckedRemoteNames();

    QLineEdit *m_dirEdit = nullptr;
    QPushButton *m_dirBrowseButton = nullptr;
    QComboBox *m_sessionCombo = nullptr;
    QLabel *m_sessionHint = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_effortCombo = nullptr;
    QCheckBox *m_skipPermsCheck = nullptr;
    QCheckBox *m_workspaceCheck = nullptr;
    QLabel *m_workspaceHint = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    CollapsibleSection *m_portsSection = nullptr;
    QListWidget *m_portList = nullptr;
    QLineEdit *m_hostPortEdit = nullptr;
    QLineEdit *m_containerPortEdit = nullptr;

    CollapsibleSection *m_dirsSection = nullptr;
    QListWidget *m_dirList = nullptr;
    QLineEdit *m_hostDirEdit = nullptr;
    QLineEdit *m_containerDirEdit = nullptr;

    // Which SshRemoteCatalog entries this box attaches to (a Windows box
    // and a Mac, say) -- unlike ports/mounts, what a remote actually
    // connects to is defined once, host-wide, in the catalog and only
    // *picked* here, so two boxes reaching the same machine share one
    // tunnel instead of each opening a redundant connection to it.
    //
    // One real QCheckBox per catalog entry, rebuilt into m_remoteChecksLayout
    // by refreshRemoteList(). Deliberately *not* checkable QListWidget rows:
    // a stylesheet ::item padding offsets the check-indicator's hit rect
    // from where it's drawn, so the top row's box could highlight-but-not-
    // toggle. A QCheckBox has none of that.
    CollapsibleSection *m_remotesSection = nullptr;
    QWidget *m_remoteChecks = nullptr;
    QVBoxLayout *m_remoteChecksLayout = nullptr;
    QLabel *m_legacyRemotesHint = nullptr;
    // Which catalog names should show checked next time refreshRemoteList()
    // rebuilds the list -- synced from the live checkbox state at the top
    // of every refresh, and seeded from BoxRecord::sshRemoteRefs by
    // loadForEdit(). Needed because the list itself is rebuilt from
    // scratch (see refreshRemoteList()) whenever the catalog might have
    // changed, e.g. after "Manage Remotes…" returns.
    QStringList m_checkedRemoteNames;

    // Directory the conversation list was built from, so
    // re-entering the same path doesn't rescan (and doesn't reset a
    // selection the user already made).
    QString m_scannedDir;
    // Whether the scanned directory still carries an executable
    // new-issue.sh from the bash era. Never run; it only seeds the
    // workspace checkbox for a tree that has no remembered answer yet.
    bool m_hasProvisioner = false;
    // Set once the user picks a model/effort by hand, after which changing
    // the target directory no longer re-resolves that combo from settings.
    bool m_modelTouched = false;
    bool m_effortTouched = false;
    // Last title this dialog auto-filled into m_nameEdit, so it can be
    // replaced when the selection changes but a name the user typed
    // themselves is never overwritten.
    QString m_autoFilledName;
    // True from loadForEdit() onward -- see isEditMode().
    bool m_editMode = false;
};
