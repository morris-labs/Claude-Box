#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;

// Standalone editor for one box's SSH tunnel configuration: the ssh
// target/identity plus a repeatable list of -L/-R forwards (see
// SshTunnelSession for what these actually run as).
//
// Split out of NewBoxDialog, which used to hold this inline -- that
// section alone had grown enough content to push NewBoxDialog's minimum
// height past what fits on a real screen (see the "Fix New/Edit Box
// dialog getting shoved onto another monitor" commit). NewBoxDialog now
// just shows a one-line summary and a button that opens this.
class SshForwardsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SshForwardsDialog(QWidget *parent = nullptr);

    void setConfig(const QString &host, const QString &identity, const QStringList &forwards);

    QString sshHost() const;
    QString sshIdentity() const;
    QStringList sshForwards() const; // "L:bindAddr:bindPort:destHost:destPort" / "R:..."

private slots:
    void browseForIdentity();
    void addForward();
    void removeSelectedForward();
    void tryAccept();

private:
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_identityEdit = nullptr;
    QListWidget *m_forwardList = nullptr;
    QComboBox *m_dirCombo = nullptr;
    QLineEdit *m_bindEdit = nullptr;
    QLineEdit *m_bindPortEdit = nullptr;
    QLineEdit *m_destHostEdit = nullptr;
    QLineEdit *m_destPortEdit = nullptr;
};
