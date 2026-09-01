#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QCheckBox;
class QListWidget;
class QDialogButtonBox;
class QPushButton;
struct BoxRecord;

// Form for creating a new box: target directory, which conversation to
// run in it, name, model/effort, the permission-bypass toggle, repeatable
// port-mapping / extra-dir-mount rows, and a button to configure SSH -L/-R
// forwards in a separate dialog (SshForwardsDialog).
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

    // SSH tunnel (background `ssh -N` on the host -- see SshTunnelSession).
    QString sshTunnelHost() const;     // "user@host[:port]"
    QString sshIdentityFile() const;   // optional -i path
    QStringList sshForwards() const;   // "L:bindAddr:bindPort:destHost:destPort" / "R:..."

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
    void configureSshForwards();
    void tryAccept();

private:
    void rememberWorkspaceChoice();
    void updateSshSummary();

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

    QListWidget *m_portList = nullptr;
    QLineEdit *m_hostPortEdit = nullptr;
    QLineEdit *m_containerPortEdit = nullptr;

    QListWidget *m_dirList = nullptr;
    QLineEdit *m_hostDirEdit = nullptr;
    QLineEdit *m_containerDirEdit = nullptr;

    // SSH forwards live in their own dialog (SshForwardsDialog) now, not
    // inline -- that section alone was enough content to push this
    // dialog's minimum height past what fits on a real screen. Only the
    // resulting config is kept here, plus a button + one-line summary.
    QLabel *m_sshSummaryLabel = nullptr;
    QPushButton *m_sshConfigButton = nullptr;
    QString m_sshHost;
    QString m_sshIdentity;
    QStringList m_sshForwards;

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
