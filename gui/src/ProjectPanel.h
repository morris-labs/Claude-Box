#pragma once

#include "DockerBackend.h"

#include <QList>
#include <QString>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

// The project list shown left of the box table. Picking a project narrows the
// table to that project's conversations; "All projects" shows everything.
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget *parent = nullptr);

    // Rebuilds the list from the current boxes plus any saved project
    // records, keeping the selection when that project still exists.
    void setBoxes(const QList<BoxInfo> &boxes);

    // Empty when "All projects" is selected.
    QString currentRoot() const;
    void selectRoot(const QString &root);

signals:
    void projectSelected(const QString &root);
    // A rename was saved; the owner should rebuild the list (setBoxes).
    void projectRenamed();
    // The user asked to move the project's existing boxes into its shared
    // container; the owner does it, since it stops and starts boxes.
    void convertToSharedRequested(const QString &root);
    // The project's mounts were saved; the owner checks whether a running
    // container needs recreating to apply them.
    void mountsChanged(const QString &root);

private:
    void showContextMenu(const QPoint &pos);
    void renameProject(const QString &root);
    void setSharedContainer(const QString &root, bool on);
    void editMounts(const QString &root);

    QListWidget *m_list = nullptr;
    bool m_rebuilding = false;
};
