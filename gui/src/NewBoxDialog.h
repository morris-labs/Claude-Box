#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QCheckBox;
class QListWidget;

// Form for creating a new box: target directory, which conversation to
// run in it, name, model/effort, the permission-bypass toggle, and
// repeatable port-mapping / extra-dir-mount rows.
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
// Purely a form -- MainWindow is the one that turns the result into a
// BoxRecord and calls DockerBackend::createNew.
class NewBoxDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewBoxDialog(QWidget *parent = nullptr);

    void setInitialDir(const QString &dir);

    QString targetDir() const;
    QString conversationName() const;
    // Empty means "start a fresh conversation"; otherwise the uuid of an
    // existing transcript the new box should resume.
    QString sessionUuid() const;
    bool skipPermissions() const;
    // Both empty when left on "default", meaning: pass no flag and let
    // the settings in ~/.claude/settings.json decide.
    QString model() const;
    QString effort() const;
    QStringList ports() const; // "HOST:CONTAINER"
    QStringList dirs() const;  // "HOSTPATH:CONTAINERPATH"

private slots:
    void browseForDir();
    void browseForMountDir();
    void reloadForDirectory();
    void onConversationChanged(int index);
    void addPort();
    void removeSelectedPort();
    void addDirMount();
    void removeSelectedDirMount();
    void tryAccept();

private:
    QLineEdit *m_dirEdit = nullptr;
    QComboBox *m_sessionCombo = nullptr;
    QLabel *m_sessionHint = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_effortCombo = nullptr;
    QCheckBox *m_skipPermsCheck = nullptr;

    QListWidget *m_portList = nullptr;
    QLineEdit *m_hostPortEdit = nullptr;
    QLineEdit *m_containerPortEdit = nullptr;

    QListWidget *m_dirList = nullptr;
    QLineEdit *m_hostDirEdit = nullptr;
    QLineEdit *m_containerDirEdit = nullptr;

    // Directory the conversation list was built from, so
    // re-entering the same path doesn't rescan (and doesn't reset a
    // selection the user already made).
    QString m_scannedDir;
    // Last title this dialog auto-filled into m_nameEdit, so it can be
    // replaced when the selection changes but a name the user typed
    // themselves is never overwritten.
    QString m_autoFilledName;
};
