#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

// Keeps a running box's record in step with what its claude process is
// actually doing, by reading the per-process files Claude Code keeps in
// ~/.claude/sessions/<pid>.json. Each holds the live `sessionId` and
// `name`, and Claude rewrites them when the session is cleared (/clear
// starts a new session id) or renamed (/rename). Boxes bind-mount ~/.claude,
// so a box's claude writes these files straight to the host.
//
// Without this the record keeps the session id it launched with, and the
// next restart resumes the conversation from before the /clear.
//
// A file is matched to a box once, by finding the one process whose
// sessionId equals the box's recorded session_uuid. After that the box is
// tracked by the process's identity (pid, pidDomain, procStart), and only
// *changes* since the previous scan are applied. That keeps a deliberate
// edit made through the app (a picked conversation or rename waiting for the
// next restart) from being reverted while the old process is still running.
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
        bool nameIsUser = false; // nameSource == "user"; absent means auto-generated
        QString key() const { return QString::number(pid) + '|' + pidDomain + '|' + procStart; }
    };
    struct Binding {
        QString key;
        QString lastSession;
        QString lastName;
    };

    void scan();
    void watchFiles(const QStringList &files);
    static QString sessionsDir();
    static QHash<QString, Entry> readEntries(QStringList *filesOut);
    static bool heldByOtherBox(const QString &sessionId, const QString &boxName);

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QSet<QString> m_running;
    QHash<QString, Binding> m_bindings; // box name -> tracked process
};
