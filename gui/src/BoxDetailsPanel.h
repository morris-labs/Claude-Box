#pragma once

#include <QWidget>

#include "DockerBackend.h"

class QLabel;
class QStackedWidget;
class CollapsibleSection;

// Read-only detail view for the selected dashboard row. Everything here
// used to be either crammed into table columns or invisible entirely --
// session uuid, port mappings and extra mounts had no UI at all once a box
// was created, despite being the fields you most want to check when a box
// misbehaves.
//
// Values come from two places: the live BoxInfo (status/detail, straight
// from docker) and the on-disk BoxRecord (everything the box was created
// with). A running box with no record -- one started outside this app --
// shows the former and dashes for the latter.
class BoxDetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit BoxDetailsPanel(QWidget *parent = nullptr);

    // Pass nullptr to show the "nothing selected" placeholder.
    void setBox(const BoxInfo *info);

private:
    // Two pages rather than show/hide on individual widgets: the
    // placeholder needs to sit centered in the whole panel, which it
    // cannot do while sharing a layout with top-aligned detail rows.
    QStackedWidget *m_stack = nullptr;

    QLabel *m_heading = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_placeholder = nullptr;
    QWidget *m_fields = nullptr;

    QLabel *m_conversation = nullptr;
    QLabel *m_directory = nullptr;
    QLabel *m_sessionUuid = nullptr;
    QLabel *m_flags = nullptr;
    QLabel *m_ports = nullptr;
    QLabel *m_mounts = nullptr;
    QLabel *m_ssh = nullptr;
    QLabel *m_detail = nullptr;

    // Collapsed by default: cpu/mem, refreshed every poll for a Running
    // box, isn't the reason most people open this panel -- see the "move
    // cpu/etc status to the info sidebar" request this was built for.
    CollapsibleSection *m_statsSection = nullptr;
    QLabel *m_statsLabel = nullptr;

    QLabel *addField(class QVBoxLayout *layout, const QString &label);
};
