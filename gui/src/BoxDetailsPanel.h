#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include "DockerBackend.h"

class QLabel;
class QStackedWidget;
class QVBoxLayout;
class CollapsibleSection;
struct SshRemote;

// Per-remote SSH tunnel state, as tracked by MainWindow's SshTunnelSession
// bookkeeping -- this panel has no process handle of its own, it only
// renders what MainWindow already knows. Indices line up positionally with
// BoxRecord::sshRemotes (remote 0's status is tunnelStatuses[0], etc.); a
// short list (or none at all) just means "not attempted yet", same as an
// absent record field elsewhere in this panel.
struct SshTunnelStatus {
    bool attempted = false; // a session has been started for this remote at least once
    bool running = false;   // its ssh process is alive right now
    QString lastError;      // most recent stderr line captured, if any (shown on hover when down)
};

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

    // Pass nullptr to show the "nothing selected" placeholder. sshRemotes
    // is the box's remotes already resolved (a catalog attachment's host/
    // identity/forwards looked up by name -- see
    // MainWindow::resolvedSshRemotesForBox()), since this panel has no
    // catalog access of its own. tunnelStatuses is positional against it;
    // leave both empty if the caller has nothing to report (no remotes
    // configured, or the box isn't Running -- MainWindow only tracks
    // tunnels for Running boxes).
    void setBox(const BoxInfo *info, const QList<SshRemote> &sshRemotes = {},
                const QList<SshTunnelStatus> &tunnelStatuses = {});

signals:
    // The panel has no way to touch a tunnel itself -- MainWindow owns the
    // SshTunnelSessions -- so the "Reconnect" button just asks for one.
    void reconnectRequested(const QString &boxName);

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
    QLabel *m_detail = nullptr;

    // SSH forwards no longer render as a single multi-line QLabel: each
    // remote gets its own status dot (green = tunnel process alive, red =
    // attempted and not running, dim = not attempted -- box not Running,
    // typically), plus a Reconnect button. Rebuilt from scratch on every
    // setBox() call rather than diffed in place; this section only ever
    // holds a handful of rows and rebuilding is simpler than reconciling.
    QWidget *m_sshContainer = nullptr;
    QVBoxLayout *m_sshLayout = nullptr;
    QString m_currentBoxName;

    // Collapsed by default: cpu/mem, refreshed every poll for a Running
    // box, isn't the reason most people open this panel -- see the "move
    // cpu/etc status to the info sidebar" request this was built for.
    CollapsibleSection *m_statsSection = nullptr;
    QLabel *m_statsLabel = nullptr;

    QLabel *addField(class QVBoxLayout *layout, const QString &label);
    void rebuildSshSection(const QList<SshRemote> &remotes,
                            const QList<SshTunnelStatus> &tunnelStatuses);
};
