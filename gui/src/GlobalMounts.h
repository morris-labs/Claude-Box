#pragma once

#include <QStringList>

// Mounts every box gets, on top of its own or its project's: the
// `~/.claude-box/global` file, one `dir=HOST_PATH[:CONTAINER_PATH]` per line.
// Like any mount, a change applies when a container is next created.
namespace GlobalMounts {

QString filePath();
QStringList load();
bool save(const QStringList &dirs);

// `own` followed by the global mounts whose container path `own` does not
// already use (docker refuses two mounts at one container path).
QStringList mergedWith(const QStringList &own);

} // namespace GlobalMounts
