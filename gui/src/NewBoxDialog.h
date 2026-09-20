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
// And as the *fork* form (loadForFork(), MainWindow::onFork): creates a
// new, independent box whose conversation starts as a copy of an existing
// box's history (see DockerBackend::createNew's forkFromUuid). Closer to
// the plain New Box form than to editing -- the directory is locked to
// the source box's, but everything else (name, model/effort, ports,
// mounts, remotes) is copied over as a starting point and stays editable,
// because the result is a brand-new box, not a change to the source one.
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

    // Switches the dialog into fork mode: seeded from `source` (whose
    // directory and conversation the new box's history comes from) much
    // like loadForEdit(), but this always produces a new box with its own
    // fresh session id -- see forkSourceUuid(). The directory is locked
    // (a fork needs source's own transcript, which is rooted there); the
    // conversation name, model/effort, permission flag, ports, mounts,
    // and SSH remotes all start out copied from `source` but stay
    // editable, since the result is a genuinely new, independent box.
    void loadForFork(const BoxRecord &source);
    bool isForkMode() const { return m_forkMode; }
    // Valid only in fork mode: the uuid of the conversation the new box's
    // history is copied from. sessionUuid() stays empty in this mode --
    // the new box always mints its own id, it just doesn't start empty.
    QString forkSourceUuid() const { return m_forkSourceUuid; }

    QString targetDir() const;
    QString conversationName() const;
    // Empty means "start a fresh conversation" (including a fork -- see
    // forkSourceUuid() above); otherwise the uuid of an existing
    // transcript the new box should resume.
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
    // Ports, mounts, and SSH remote attachments -- the part of loadForEdit()
    // and loadForFork() that's identical between them (seeding from an
    // existing record's fields and expanding any section that isn't empty).
    void seedExtras(const BoxRecord &rec);
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
    // True from loadForFork() onward -- see isForkMode().
    bool m_forkMode = false;
    // Set by loadForFork(); see forkSourceUuid().
    QString m_forkSourceUuid;
};
