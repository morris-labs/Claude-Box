#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QCheckBox;
class QListWidget;

// Form for creating a new box: target directory, conversation name,
// YOLO/RC toggles, and repeatable port-mapping / extra-dir-mount rows.
// Purely a form -- MainWindow is the one that turns the result into a
// BoxRecord and calls DockerBackend::createNew.
class NewBoxDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewBoxDialog(QWidget *parent = nullptr);

    void setInitialDir(const QString &dir);

    QString targetDir() const;
    QString conversationName() const;
    bool yolo() const;
    bool rc() const;
    QStringList ports() const;
    QStringList dirs() const;

private slots:
    void browseForDir();
    void addPort();
    void removeSelectedPort();
    void addDirMount();
    void removeSelectedDirMount();
    void tryAccept();

private:
    QLineEdit *m_dirEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_yoloCheck = nullptr;
    QCheckBox *m_rcCheck = nullptr;

    QListWidget *m_portList = nullptr;
    QLineEdit *m_portEntry = nullptr;

    QListWidget *m_dirList = nullptr;
    QLineEdit *m_dirEntry = nullptr;
};
