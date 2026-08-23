#pragma once

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
};

// Every interaction with `docker` for managing claude-box containers.
// Deliberately synchronous (QProcess::waitForFinished): these are short,
// local, non-network commands invoked either from explicit user actions
// or a several-second refresh timer, and a blocking call here is far
// easier to get right without a compiler in the loop than a fully async
// design would be. PtySession (the long-lived attach connection) is the
// one place that genuinely needs to be event-driven.
class DockerBackend {
public:
    // Running + stopped-but-not-removed + known-but-not-running boxes,
    // merged with ~/.claude-box/known/ records. Running entries include a
    // `docker stats` snapshot in detail.
    QList<BoxInfo> listBoxes() const;

    bool isRunning(const QString &name) const;

    // Starts a brand-new conversation. `rec` must have targetDir/
    // conversationName/yolo/rc/ports/dirs set; on success this fills in
    // rec.name/rec.sessionUuid, saves the record, and starts the
    // container detached (`docker run -d -t --rm ...`) with a freshly
    // minted --session-id. If rec.conversationName is non-empty and
    // targetDir has an executable new-issue.sh, this also runs the
    // issue-provisioning workflow (matching the old claude-box.bash
    // --name behavior) before starting the container.
    bool createNew(BoxRecord &rec, QString *errorOut) const;

    // Restarts a known conversation's container with `claude --resume
    // <rec.sessionUuid>`. rec must already be fully populated (from
    // BoxRecord::load). Does not touch the record.
    bool reopen(const BoxRecord &rec, QString *errorOut) const;

    bool stop(const QString &name, QString *errorOut) const;
    bool remove(const QString &name, QString *errorOut) const; // docker rm

private:
    bool runDocker(const QStringList &args, QString *stdoutOut, QString *errorOut, int timeoutMs) const;
    bool runContainer(const BoxRecord &rec, const QStringList &claudeArgs, QString *errorOut) const;
    QString uniqueBoxName(const QString &base) const;
};
