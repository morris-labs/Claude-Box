#pragma once

#include <QString>
#include <QStringList>
#include <QList>

// One SSH target: a background `ssh -N` runs on the *host* (not inside
// any box -- see SshTunnelSession) implementing every forward below over
// one connection to `host`. Empty `forwards` means this remote contributes
// no live tunnel, regardless of what host/identity hold.
//
// Two distinct places construct one of these:
//  - SshRemoteCatalog: named, host-wide, shared -- "Windows box" or "Mac
//    box", defined once and attached to by any number of boxes that all
//    want the same tunnel (see BoxRecord::sshRemoteRefs). `name` is set
//    and is the catalog key.
//  - A legacy per-box inline remote (BoxRecord::sshRemotes below) --
//    `name` is always empty for these. Kept only so a record saved before
//    the catalog existed keeps working; new boxes never create one.
struct SshRemote {
    QString name;           // catalog key; empty for a legacy per-box inline remote
    QString host;          // "user@host" or "user@host:port"
    QString identity;      // optional `-i` path; empty = agent/default key
    // "L:<bindAddr>:<bindPort>:<destHost>:<destPort>" or "R:...", repeatable.
    // bindAddr may be empty (ssh's own default bind for that direction).
    // Mirrors ssh's own -L/-R argument, just with the direction letter
    // glued on front and always all five fields present.
    QStringList forwards;

    bool isValid() const { return !host.trimmed().isEmpty(); }
};

// Persistent record for one claude-box container, stored as a simple
// key=value file under ~/.claude-box/known/<name>. This is the only
// file-based state the app keeps -- docker itself is the live "is it
// running" ground truth (see DockerBackend), so there's no separate
// crash-signal file the way the old bash tooling had one.
struct BoxRecord {
    QString name;             // container name, also the record's filename
    QString targetDir;
    QString sessionUuid;
    QString conversationName;
    // conversationName as of the last time it was written into the
    // transcript via `claude --name` (creation, adoption, or fork -- see
    // DockerBackend::createNew()). reopen() never passes --name, so an
    // Edit-dialog rename changes conversationName without changing this
    // field; MainWindow::startBox() compares the two to tell "the record
    // still matches what's in the transcript" from "the user renamed it
    // through the app since," and only pulls a transcript-side title
    // change into conversationName in the first case -- otherwise a
    // deliberate rename would get silently reverted on the next Start.
    QString transcriptSyncedName;
    // The model and effort the running claude was started with. When the
    // record has since been edited (a change waiting for a restart), stopping
    // the box must not overwrite it with what the session was using.
    QString launchedModel;
    QString launchedEffort;
    // `claude --dangerously-skip-permissions`. Still stored under the old
    // `yolo=` key so existing records keep working; only the name here
    // and in the UI changed, to say what the flag actually does.
    bool skipPermissions = false;
    QString model;            // `claude --model <alias-or-name>`; empty = whatever the settings say
    // Subfolder of targetDir the agent was pointed at, when the box was
    // created with a workspace folder; empty when it works in the target
    // directory itself. Informational -- the container mounts and runs in
    // targetDir either way, so nothing about reopening depends on it.
    QString workspaceDir;
    QString effort;           // `claude --effort <level>`; empty = whatever the settings say
    // Host path of the git worktree this conversation works in, and its
    // branch. Empty for a box that works in targetDir (or a workspace
    // subfolder of it). When set, the container's working directory -- and so
    // Claude's own, which names its transcript directory -- is this path,
    // while targetDir stays the mounted project root. Always use workDir()
    // to find the transcripts, never targetDir.
    QString worktreeDir;
    // Set by Convert to Worktree: the folder the conversation used to work in.
    // The next start tells the agent about the move once, then clears it.
    QString movedFrom;
    QString branch;

    // A box in a project's shared container: the container it runs in and
    // its tmux session there. Both empty for a classic box, which owns a
    // container of its own named `name` and runs claude in session "main".
    QString container;
    QString session;
    QStringList ports;        // "HOST:CONTAINER", repeatable
    QStringList dirs;         // "HOSTPATH:CONTAINERPATH", repeatable

    // Names of SshRemoteCatalog entries this box tunnels through -- the
    // current model: a remote is defined once (host/identity/forwards) and
    // any number of boxes can attach to it, sharing one ssh -N process
    // instead of each redundantly opening its own connection to the same
    // host (see MainWindow::syncTunnels(), which ref-counts by name across
    // every Running box). Persisted as repeatable ssh_remote_ref= lines.
    QStringList sshRemoteRefs;

    // Set when a box starts (createNew or reopen) and cleared on a deliberate
    // stop. After a reboot all running containers vanish; this flag is what
    // "Resume All Known" uses to distinguish boxes that were running at the
    // time from ones the user had already closed.
    bool wasRunning = false;

    // Legacy per-box inline remotes -- see SshRemote's own comment. Never
    // written by current UI (NewBoxDialog only ever touches sshRemoteRefs
    // now); read-only backward compatibility for a record saved before the
    // catalog existed. Persisted as indexed ssh_remote=/ssh_identity=/
    // ssh_forward= lines (see save()/load()); a record written before
    // *multi*-remote support existed had one flat ssh_host=/ssh_identity=/
    // ssh_forward= (no index) instead, which load() reads as index 0.
    QList<SshRemote> sshRemotes;

    // There is deliberately no remote-control field: --remote-control is
    // now passed unconditionally (see DockerBackend::baseClaudeArgs), so
    // the old `rc=` key is read from nothing and written by nothing. It's
    // simply ignored in records written by earlier versions.

    bool isValid() const { return !name.isEmpty(); }
    // The directory Claude runs in: the worktree when there is one.
    QString workDir() const { return worktreeDir.isEmpty() ? targetDir : worktreeDir; }
    bool isShared() const { return !container.isEmpty() && container != name; }
    QString containerName() const { return container.isEmpty() ? name : container; }
    // tmux turns "." and ":" in a session name into "_" (they separate window
    // and pane in a target), so a box named after a folder like app.odinhelp.com
    // is only ever found under its rewritten name. The record keeps the name as
    // it was; this is what every tmux command and state-file comparison uses.
    QString tmuxSession() const
    {
        QString name = session.isEmpty() ? QStringLiteral("main") : session;
        name.replace(QLatin1Char('.'), QLatin1Char('_'));
        name.replace(QLatin1Char(':'), QLatin1Char('_'));
        return name;
    }

    // Base ~/.claude-box/known directory (created on demand by save()).
    static QString knownDir();
    // Host directory mounted over the container's ~/.claude/sessions, so this
    // box's Claude process writes its pid file where no other box can overwrite
    // it. See SessionWatcher.
    static QString sessionsDir(const QString &boxName);

    // Every record currently on disk under knownDir().
    static QList<BoxRecord> loadAll();

    // A single record by container name. Returns an invalid (isValid()
    // == false) BoxRecord if none exists.
    static BoxRecord load(const QString &name);

    // Writes this record to knownDir()/name, creating the directory if
    // needed. Returns false on I/O failure.
    bool save() const;

    // Deletes the on-disk record (used by Purge/Remove). Returns true if
    // the file didn't exist or was removed successfully.
    bool remove() const;
};
