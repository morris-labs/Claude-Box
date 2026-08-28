#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QCheckBox;
class QListWidget;

// Form for creating a new box: target directory, which conversation to
// run in it, conversation name, YOLO/RC toggles, and repeatable
// port-mapping / extra-dir-mount rows.
//
// The conversation picker lists every transcript Claude already has for
// the chosen directory (see ConversationCatalog), so a box can adopt a
// conversation that was started outside this app -- from a bare `claude`
// in a terminal, say -- instead of only ever starting a fresh one.
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
    bool yolo() const;
    bool rc() const;
    QStringList ports() const;
    QStringList dirs() const;

private slots:
    void browseForDir();
    void reloadConversations();
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

    // Directory the combo's current contents were scanned from, so
    // re-entering the same path doesn't rescan (and doesn't reset a
    // selection the user already made).
    QString m_scannedDir;
    // Last title this dialog auto-filled into m_nameEdit, so it can be
    // replaced when the selection changes but a name the user typed
    // themselves is never overwritten.
    QString m_autoFilledName;
    QCheckBox *m_yoloCheck = nullptr;
    QCheckBox *m_rcCheck = nullptr;

    QListWidget *m_portList = nullptr;
    QLineEdit *m_portEntry = nullptr;

    QListWidget *m_dirList = nullptr;
    QLineEdit *m_dirEntry = nullptr;
};
