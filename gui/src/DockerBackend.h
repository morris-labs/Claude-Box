#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QList>

#include "BoxRecord.h"

// One row of dashboard state, merged from `docker ps`/`docker ps -a` and
// the on-disk BoxRecord for that name. Status::Stopped covers both "you
// stopped it" and "it crashed/host rebooted" -- docker ps is the sole
// ground truth for "is it running", so there's nothing else to
// distinguish; either way, starting it again runs a fresh container with
// `claude --resume <uuid>`. Status::Exited is for containers that exist
// in `docker ps -a` but are not running (rare with --rm, only from
// containers started outside the app or on failure).
struct BoxInfo {
    enum class Status { Running, Stopped, Exited };
    Status status = Status::Stopped;
    QString name;
    QString conversationName;
    QString targetDir;
    QString detail; // human-readable status text for the Details column
    // "cpu N% · mem A / B" for a Running box, empty otherwise -- kept
    // separate from `detail` so the table's Details column can stay pure
    // docker status while BoxDetailsPanel shows this in its own
    // (collapsible) Resource Usage section.
    QString stats;

    bool operator==(const BoxInfo &o) const
    {
        return status == o.status && name == o.name && conversationName == o.conversationName
            && targetDir == o.targetDir && detail == o.detail && stats == o.stats;
    }
    bool operator!=(const BoxInfo &o) const { return !(*this == o); }
};

// Every interaction with `docker` for managing claude-box containers.
//
// These calls are synchronous (QProcess::waitForFinished), which is fine
// only because MainWindow runs the polling ones on a worker thread. That
// is not a stylistic choice: `docker stats` costs 1-2 SECONDS per
// invocation, so calling it on the GUI thread every few seconds froze the
// entire application more often than it ran. Anything added here should be
// assumed slow and kept off the UI thread.
class DockerBackend {
public:
    // Running + stopped-but-not-removed + known-but-not-running boxes,
    // merged with ~/.claude-box/known/ records. Running entries carry a
    // cached `docker stats` snapshot in detail.
    //
    // SLOW -- call from a worker thread, never the GUI thread. The `ps`
    // queries are milliseconds, but when `sampleStats` is true and the
    // cache has gone stale this also spends a second or two refreshing it.
    QList<BoxInfo> listBoxes(bool sampleStats = true) const;

    // Forces the next listBoxes(true) to re-sample stats regardless of
    // cache age. Used by the explicit Refresh action.
    void invalidateStats();

    bool isRunning(const QString &name) const;

    // Creates a box. `rec` must have targetDir/conversationName/
    // skipPermissions/model/effort/ports/dirs set; on success this fills in rec.name, saves the
    // record, and starts the container detached
    // (`docker run -d -i -t --rm ...`).
    //
    // Three modes:
    //   rec.sessionUuid empty, forkFromUuid empty -- brand-new
    //     conversation: a uuid is minted and passed as --session-id.
    //   rec.sessionUuid set -- adopt the existing transcript with that
    //     uuid (one the user picked out of ConversationCatalog, possibly
    //     started outside this app) and pass --resume instead. Workspace
    //     provisioning is skipped in this mode.
    //   forkFromUuid set -- fork: like the brand-new case (a uuid is
    //     minted for rec.sessionUuid and workspace provisioning runs
    //     normally), except the container is started with `--resume
    //     forkFromUuid --fork-session --session-id <the minted uuid>`, so
    //     the new session starts as a copy of forkFromUuid's transcript
    //     instead of empty. The two conversations are independent from
    //     that point on -- later turns in either don't touch the other.
    //     Mutually exclusive with rec.sessionUuid being set.
    //
    // workspaceSubdir asks for a folder named after the conversation
    // inside targetDir, which the agent is told to work in (the container
    // still mounts and runs in targetDir). If the target dir ships an
    // executable new-issue.sh it provisions the folder; otherwise an empty
    // one is created. Existing folders are left untouched -- that means
    // "resuming", not "starting fresh". On success rec.workspaceDir holds
    // the folder's name.
    bool createNew(BoxRecord &rec, bool workspaceSubdir, QString *errorOut,
                    const QString &forkFromUuid = QString()) const;

    // Restarts a known conversation's container with `claude --resume
    // <rec.sessionUuid>`. rec must already be fully populated (from
    // BoxRecord::load). Does not touch the record.
    bool reopen(const BoxRecord &rec, QString *errorOut) const;

    bool stop(const QString &name, QString *errorOut) const;
    bool remove(const QString &name, QString *errorOut) const; // docker rm

private:
    // Everything here has two implementations: the Engine API (see
    // DockerApi -- fast, portable to Windows, and the only way to get a
    // stats sample in under a second) and the `docker` CLI, kept as the
    // fallback for setups the socket client deliberately doesn't handle,
    // such as a tcp:// or ssh:// DOCKER_HOST.
    bool listViaApi(QList<BoxInfo> &result, QSet<QString> &seen, bool sampleStats) const;
    void listViaCli(QList<BoxInfo> &result, QSet<QString> &seen, bool sampleStats) const;

    // `docker stats --no-stream` takes 1-2 seconds to return no matter how
    // many containers it reports on, because it samples CPU over an
    // interval before printing. So on the CLI path it is asked for every
    // container at once and the answer is cached: the old code ran it once
    // PER running box on every refresh tick, which cost more time than the
    // interval between ticks as soon as a second box existed. The API path
    // needs neither trick -- see statsDetail().
    mutable QHash<QString, QString> m_statsCache;
    mutable QElapsedTimer m_statsAge;
    mutable bool m_statsSampled = false;
    void refreshStatsCache() const;

    // CPU percentage is a rate, and one API sample carries only counters,
    // so the previous sample is kept and the delta taken across refresh
    // ticks. First sighting of a container therefore reports memory only.
    struct CpuSample {
        quint64 containerUsage = 0;
        quint64 systemUsage = 0;
        int onlineCpus = 1;
    };
    mutable QHash<QString, CpuSample> m_prevCpu;
    QString statsDetail(const QString &id) const;

    bool runDocker(const QStringList &args, QString *stdoutOut, QString *errorOut, int timeoutMs) const;
    bool runContainer(const BoxRecord &rec, const QStringList &claudeArgs, QString *errorOut) const;
    // The API half of runContainer(): POST /containers/create + /start.
    // Returns false (without having created anything) when the API isn't
    // usable, which is the caller's cue to shell out instead.
    bool runContainerViaApi(const BoxRecord &rec, const QStringList &claudeArgs,
                            QString *errorOut) const;

    // The single description of how a box is launched, from which both
    // the CLI flags and the API's JSON body are derived.
    struct LaunchSpec {
        QStringList cmd;      // the container's argv
        QStringList env;      // KEY=VALUE
        QStringList binds;    // "HOSTPATH:CONTAINERPATH"
        QStringList ports;    // "HOST:CONTAINER"
        QString envFile;      // agent.env, when the target dir has one
        QString workingDir;
        QString containerUser; // username inside the container (matches host USER)
    };
    static LaunchSpec launchSpec(const BoxRecord &rec, const QStringList &claudeArgs);

    // Flags shared by createNew() and reopen(): --remote-control (always),
    // plus whatever the record asks for.
    static QStringList baseClaudeArgs(const BoxRecord &rec);
    QString uniqueBoxName(const QString &base) const;
};
