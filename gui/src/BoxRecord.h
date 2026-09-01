#pragma once

#include <QString>
#include <QStringList>
#include <QList>

// One SSH target a box tunnels through: a background `ssh -N` runs on
// the *host* (not inside the box -- see SshTunnelSession) implementing
// every forward below over one connection to `host`. A box can have any
// number of these -- e.g. one tunnel to a Windows machine and a separate
// one to a Mac, each with its own identity and forward set, running
// concurrently. Empty `forwards` means this remote contributes no live
// tunnel, regardless of what host/identity hold.
struct SshRemote {
    QString host;          // "user@host" or "user@host:port"
    QString identity;      // optional `-i` path; empty = agent/default key
    // "L:<bindAddr>:<bindPort>:<destHost>:<destPort>" or "R:...", repeatable.
    // bindAddr may be empty (ssh's own default bind for that direction).
    // Mirrors ssh's own -L/-R argument, just with the direction letter
    // glued on front and always all five fields present.
    QStringList forwards;
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
    QStringList ports;        // "HOST:CONTAINER", repeatable
    QStringList dirs;         // "HOSTPATH:CONTAINERPATH", repeatable

    // Any number of SSH tunnels -- see SshRemote above. Persisted as
    // indexed ssh_remote=/ssh_identity=/ssh_forward= lines (see save()/
    // load()); a record written before multi-remote support existed had
    // one flat ssh_host=/ssh_identity=/ssh_forward= (no index) instead,
    // which load() reads as a single remote at index 0.
    QList<SshRemote> sshRemotes;

    // There is deliberately no remote-control field: --remote-control is
    // now passed unconditionally (see DockerBackend::baseClaudeArgs), so
    // the old `rc=` key is read from nothing and written by nothing. It's
    // simply ignored in records written by earlier versions.

    bool isValid() const { return !name.isEmpty(); }

    // Base ~/.claude-box/known directory (created on demand by save()).
    static QString knownDir();

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
