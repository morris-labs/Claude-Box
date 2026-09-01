#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QGridLayout;

// First-run (and re-runnable any time, via View > Setup...) checklist:
// confirms docker and an ssh client are reachable, that the claude-code
// image is built, that a default ssh keypair exists with sane
// permissions, and lets the user pick a default directory for New Box
// instead of it always defaulting to their home directory.
//
// Nothing here is a hard gate -- Close/Done works regardless of which
// checks pass, since a user might legitimately fix docker *after*
// dismissing this. hasCompletedSetup() just tracks whether it's been
// seen once, for whether to pop it up unprompted on startup.
class SetupWizard : public QDialog {
    Q_OBJECT
public:
    explicit SetupWizard(QWidget *parent = nullptr);

    // Whether this has ever been closed via Done (not just constructed --
    // MainWindow uses this to decide whether to show it unprompted).
    static bool hasCompletedSetup();

    // QSettings-backed, both usable before this dialog has ever run:
    // defaultTargetDir() falls back to the home directory if nothing (or
    // a since-deleted directory) is configured; defaultSshIdentity()
    // returns the configured identity if both halves of it still exist,
    // otherwise auto-detects an id_ed25519/id_rsa already sitting under
    // ~/.ssh, otherwise empty. Used by MainWindow::onNew() and
    // SshForwardsDialog respectively so neither needs this dialog to
    // have ever been opened to get a sane default.
    static QString defaultTargetDir();
    static QString defaultSshIdentity();

private slots:
    void refreshChecks();
    void browseDefaultDir();
    void buildDockerImage();
    void generateSshKey();
    void fixSshPermissions();
    void finish();

private:
    // One row's live widgets, so refreshChecks() can update status text
    // in place instead of rebuilding the whole dialog.
    struct CheckRow {
        QLabel *dot = nullptr;
        QLabel *detail = nullptr;
    };
    CheckRow addCheckRow(QGridLayout *grid, int row, const QString &label, QPushButton *action = nullptr);
    void setRowStatus(const CheckRow &row, bool ok, const QString &detail);

    QLineEdit *m_targetDirEdit = nullptr;

    CheckRow m_dockerRow;
    CheckRow m_imageRow;
    CheckRow m_sshClientRow;
    CheckRow m_sshKeyRow;
    CheckRow m_sshPermRow;

    QPushButton *m_buildImageButton = nullptr;
    QPushButton *m_generateKeyButton = nullptr;
    QPushButton *m_fixPermButton = nullptr;
};
