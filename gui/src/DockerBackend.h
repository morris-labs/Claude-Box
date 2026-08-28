#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QList>

#include "BoxRecord.h"

// One row of dashboard state, merged from `docker ps`/`docker ps -a` and
// the on-disk BoxRecord for that name. Status::Known covers both "you
// closed it" and "it crashed/host rebooted" -- docker ps is the sole
// ground truth for "is it running", so there's nothing else to
// distinguish; either way, reopening starts a fresh container with
// `claude --resume <uuid>`.
struct BoxInfo {
    enum class Status { Running, Stopped, Known };
    Status status = Status::Known;
    QString name;
    QString conversationName;
    QString targetDir;
    QString detail; // human-readable status text for the Details column

    bool operator==(const BoxInfo &o) const
    {
        return status == o.status && name == o.name && conversationName == o.conversationName
            && targetDir == o.targetDir && detail == o.detail;
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

    // Creates a box. `rec` must have targetDir/conversationName/yolo/rc/
    // ports/dirs set; on success this fills in rec.name, saves the
    // record, and starts the container detached
    // (`docker run -d -i -t --rm ...`).
    //
    // Two modes, chosen by whether rec.sessionUuid is already set:
    //   empty  -- brand-new conversation: a uuid is minted and passed as
    //             --session-id. If rec.conversationName is non-empty and
    //             targetDir has an executable new-issue.sh, the
    //             issue-provisioning workflow runs first (matching the
    //             old claude-box.bash --name behavior).
    //   set    -- adopt the existing transcript with that uuid (one the
    //             user picked out of ConversationCatalog, possibly
    //             started outside this app) and pass --resume instead.
    //             Issue provisioning is skipped in this mode.
    bool createNew(BoxRecord &rec, QString *errorOut) const;

    // Restarts a known conversation's container with `claude --resume
    // <rec.sessionUuid>`. rec must already be fully populated (from
    // BoxRecord::load). Does not touch the record.
    bool reopen(const BoxRecord &rec, QString *errorOut) const;

    bool stop(const QString &name, QString *errorOut) const;
    bool remove(const QString &name, QString *errorOut) const; // docker rm

private:
    // `docker stats --no-stream` takes 1-2 seconds to return no matter how
    // many containers it reports on, because it samples CPU over an
    // interval before printing. So it is asked for every container at once
    // and the answer is cached: the old code ran it once PER running box
    // on every refresh tick, which cost more time than the interval
    // between ticks as soon as a second box existed.
    mutable QHash<QString, QString> m_statsCache;
    mutable QElapsedTimer m_statsAge;
    mutable bool m_statsSampled = false;
    void refreshStatsCache() const;

    bool runDocker(const QStringList &args, QString *stdoutOut, QString *errorOut, int timeoutMs) const;
    bool runContainer(const BoxRecord &rec, const QStringList &claudeArgs, QString *errorOut) const;
    QString uniqueBoxName(const QString &base) const;
};
