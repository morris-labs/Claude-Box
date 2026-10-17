#pragma once

#include <QString>
#include <QStringList>

// The pieces of a shared (one per project) container that are plain text and
// files, kept apart from DockerBackend so they can be run and tested without
// Docker. See plan-projects.md, stage 3.
//
// A shared container's PID 1 is the keeper script. Each conversation is a
// tmux session started with `docker exec`. The keeper publishes the names of
// the live sessions in a state file the host reads directly.
namespace SharedContainer {

// Where the state directory appears inside the container.
inline const QString kStateMount = QStringLiteral("/run/claude-box");
inline const QString kStateFileName = QStringLiteral("sessions");

// Seconds a state file stays trustworthy. The keeper rewrites it every
// second, so an older file means the keeper (and so the container) is gone.
constexpr int kStateMaxAgeSec = 6;
// Seconds the keeper lingers with no sessions before the container exits.
constexpr int kStartupGraceSec = 30;

// Host side.
QString stateDir(const QString &container);
QString stateFile(const QString &container);

// `bash -c` script for PID 1. $1 is the state directory and $2 the number of
// seconds to linger with no sessions before exiting (also the wait for the
// first one).
QString keeperScript();

// `bash -c` script that starts one conversation. $1 is the tmux session name,
// $2 the working directory, $3 the complete command as one shell-quoted
// string.
// Succeeds only if the session is still alive a moment later; a program that
// exits at once (a bad flag, a missing transcript) is reported instead of
// vanishing.
QString sessionStartScript();

// Names listed in a state file's contents, one per line.
QStringList parseSessions(const QByteArray &fileContents);

// The live sessions of a container according to its state file, or an empty
// list when the file is missing or stale.
QStringList liveSessions(const QString &container);

} // namespace SharedContainer
