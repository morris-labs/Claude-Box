#pragma once

#include <QDialog>
#include <QStringList>

class DockerBackend;
class QLabel;
class QListWidget;
class QPushButton;

// Dialog for switching between Claude account profiles (~/.claude-A,
// ~/.claude-B, etc.) as managed by the claude-account-switch script or
// set up manually. Handles the full switch lifecycle: stops running
// containers, performs the profile swap, then emits switchCompleted so
// the caller can restart those boxes under the new account.
class AccountSwitchDialog : public QDialog {
    Q_OBJECT
public:
    explicit AccountSwitchDialog(DockerBackend *docker, QWidget *parent = nullptr);

signals:
    // Emitted after a successful switch. stoppedBoxNames is the list of
    // containers that were running and were stopped; the caller restarts them.
    void switchCompleted(const QStringList &stoppedBoxNames);

private slots:
    void onSelectionChanged();
    void onSwitchClicked();

private:
    void buildUi();
    void refreshProfileList();
    bool performSwitch(const QString &toProfile, QString *errorOut);

    // Profile name active right now (e.g. "A"), or empty if ~/.claude is
    // not a symlink pointing at a ~/.claude-* directory.
    static QString activeProfile();
    // All ~/.claude-* profile directories under $HOME, sorted; excludes
    // ~/.claude-projects-shared and anything that doesn't look like a profile.
    static QStringList availableProfiles();

    DockerBackend *m_docker;
    QLabel       *m_statusLabel = nullptr;
    QListWidget  *m_list        = nullptr;
    QPushButton  *m_switchBtn   = nullptr;
    QLabel       *m_noteLabel   = nullptr;
};
