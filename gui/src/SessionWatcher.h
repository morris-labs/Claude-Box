#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

// Keeps a running box's record in step with what its claude process is
// actually doing, by reading the per-process files Claude Code keeps in
// its sessions directory (<pid>.json). Each holds the live `sessionId` and
// `name`, and Claude rewrites them when the session is cleared (/clear
// starts a new session id) or renamed (/rename).
//
// Without this the record keeps the session id it launched with, and the
// next restart resumes the conversation from before the /clear.
//
// Where the files are matters. Every container numbers its pids from the same
// small values, so in a directory shared by all boxes (the host's
// ~/.claude/sessions) boxes overwrite each other's files, and stale ones from
// killed containers pile up. New boxes therefore get their own directory,
// BoxRecord::sessionsDir(name), mounted over the container's sessions
// directory (DockerBackend). Whatever is in it belongs to that box, so no
// matching is needed. A box started before that mount existed still writes to
// the shared directory; for those the process is found by session id, and only
// when that is unambiguous.
class SessionWatcher : public QObject {
    Q_OBJECT
public:
    explicit SessionWatcher(QObject *parent = nullptr);

    // The names of the boxes that are running now. Also triggers a scan, so
    // calling it from the regular poll is the backstop for missed file events.
    void setRunning(const QStringList &names);

signals:
    // A box's record was updated from its session file.
    void boxChanged(const QString &boxName);

private:
    struct Entry {
        int pid = 0;
        QString pidDomain;
        QString procStart;
        QString sessionId;
        QString name;
        QString entrypoint;
        qint64 updatedAt = 0;
        bool nameIsUser = false; // nameSource == "user"; absent means auto-generated
        QString key() const { return QString::number(pid) + '|' + pidDomain + '|' + procStart; }
    };
    struct Binding {
        QString key;            // legacy (shared directory) mode only
        bool seen = false;      // a first reading has been taken
        QString lastSession;
        QString lastName;
    };

    void scan();
    void watchPaths(const QStringList &dirs, const QStringList &files);
    static QString sharedSessionsDir();
    static QHash<QString, Entry> readEntries(const QString &dir, QStringList *filesOut);
    static bool heldByOtherBox(const QString &sessionId, const QString &boxName);

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QSet<QString> m_running;
    QHash<QString, Binding> m_bindings; // box name -> what was last seen
};
