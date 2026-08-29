#pragma once

#include <QString>

// Maps a host filesystem path to the path it should appear at *inside*
// the container.
//
// On Linux (and, presumptively, macOS) a box's target directory is
// mounted at the identical path on both sides -- `rec.targetDir` is
// already something the container can legally have as a path, and every
// call site in this codebase grew up leaning on that. It is not a safe
// assumption anywhere else: a Windows host path like `C:\Users\name\proj`
// cannot be a container-side path at all (containers stay Linux even on
// a Windows host -- Docker Desktop runs them in a VM), and its own
// drive-letter colon collides outright with docker's `HOST:CONTAINER`
// bind-mount syntax.
//
// Every place that needs "the path this directory will have inside the
// container" -- the primary bind's destination, `-w`/WorkingDir, the
// default when an extra `--dir` mount is given no explicit container
// side, and (this is the one that actually matters)
// ConversationCatalog::projectDirFor(), since Claude Code's own
// ~/.claude/projects/<encoded> naming is based on whatever cwd it sees
// *inside* the container, never the host path -- should go through
// hostToContainer() rather than reusing the host string directly.
namespace ContainerPaths {

// Linux/macOS: identity (returns the absolute host path unchanged, which
// is exactly what every call site did before this existed -- zero
// behavior change there).
//
// Windows: a deterministic, reversible mapping into a path a Linux
// container can actually have, e.g. `C:\Users\name\project` becomes
// `/mnt/host/c/Users/name/project` (lowercase drive letter, forward
// slashes). This deliberately does not reuse WSL's own `/mnt/c/...`
// convention -- this app is a native Windows process talking to Docker
// Desktop directly, not something running inside WSL, and a distinct
// prefix keeps that visibly true to anyone inspecting a running
// container rather than reading as a real WSL mount that happens to be
// there.
QString hostToContainer(const QString &hostPath);

// Splits a "HOSTPATH:CONTAINERPATH" or bare "HOSTPATH" mount spec (the
// format --dir mounts are stored in, both in BoxRecord's `dir=` lines and
// in the values NewBoxDialog builds) into its two halves.
//
// A naive first-colon split breaks the moment the host half is a Windows
// path: "C:/Users/x:/data" would split into host "C" / container
// "/Users/x:/data". This skips a leading single-letter drive colon before
// searching, so the colon actually found is the real separator -- safe
// because a container-side path is always POSIX-rooted and so never
// contains a ':' of its own to collide with.
void splitMountSpec(const QString &spec, QString &hostOut, QString &containerOut);

} // namespace ContainerPaths
