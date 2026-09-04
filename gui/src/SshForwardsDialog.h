#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Editor for one named SSH remote in SshRemoteCatalog: a display name plus
// the ssh target/identity plus a repeatable list of -L/-R forwards (see
// SshTunnelSession for what these actually run as).
//
// Split out of NewBoxDialog, which used to hold this inline -- that
// section alone had grown enough content to push NewBoxDialog's minimum
// height past what fits on a real screen (see the "Fix New/Edit Box
// dialog getting shoved onto another monitor" commit). Originally a box
// owned its remotes directly; now a remote is defined once here and any
// number of boxes attach to it by name (see ManageSshRemotesDialog and
// BoxRecord::sshRemoteRefs), so two boxes reaching the same machine share
// one tunnel instead of each opening a redundant connection to it.
class SshForwardsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SshForwardsDialog(QWidget *parent = nullptr);

    void setConfig(const QString &name, const QString &host, const QString &identity,
                    const QStringList &forwards);

    QString remoteName() const;
    QString sshHost() const;
    QString sshIdentity() const;
    QStringList sshForwards() const; // "L:bindAddr:bindPort:destHost:destPort" / "R:..."

    // Names already taken by *other* catalog entries -- tryAccept() refuses
    // to save a name in this list (case-insensitively), since two entries
    // sharing a name would collide on the same catalog file and make
    // BoxRecord::sshRemoteRefs ambiguous about which one it means. The
    // entry being edited (if any) should not be included here.
    void setReservedNames(const QStringList &names);

private slots:
    void browseForIdentity();
    void addForward();
    void editSelectedForward();
    void removeSelectedForward();
    void testConnection();
    void tryAccept();

private:
    // Runs `ssh -o BatchMode=yes ... true` against the current target and
    // reports what happened, without touching the UI -- shared by
    // testConnection() (which does update the UI) and the auth-failure
    // path, which needs to know whether the interactive login actually
    // fixed anything once it's done.
    struct ConnectResult {
        enum class Outcome { Ok, AuthFailed, Unreachable } outcome;
        QString detail;
    };
    ConnectResult probeConnection() const;
    void offerInteractiveLogin();

    QLineEdit *m_nameEdit = nullptr;
    QStringList m_reservedNames;
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_identityEdit = nullptr;
    QListWidget *m_forwardList = nullptr;
    QComboBox *m_dirCombo = nullptr;
    QLineEdit *m_bindEdit = nullptr;
    QLineEdit *m_bindPortEdit = nullptr;
    QLineEdit *m_destHostEdit = nullptr;
    QLineEdit *m_destPortEdit = nullptr;

    QLabel *m_connectionStatus = nullptr;
    QPushButton *m_testButton = nullptr;
};
