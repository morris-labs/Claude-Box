#pragma once

#include "BoxRecord.h" // for SshRemote

#include <QList>
#include <QString>

// Host-wide store of named SSH remotes ("Windows box", "Mac box"), each
// one file under ~/.claude-box/ssh_remotes/<sanitized-name> -- same
// key=value-per-line convention as BoxRecord, and the same reason: docker
// isn't involved, so this is the one place ground truth has to live on
// disk rather than be asked of anything live.
//
// Exists so multiple boxes that need to reach the same remote machine can
// share one background `ssh -N` tunnel instead of each opening its own
// redundant connection to the same host -- see BoxRecord::sshRemoteRefs
// and MainWindow::syncTunnels(), which ref-counts a catalog entry by name
// across every currently-Running box that attaches to it. A per-box
// SshRemote (the legacy BoxRecord::sshRemotes field) has no `name` and is
// never shared; a catalog entry always does and always might be.
class SshRemoteCatalog {
public:
    static QString catalogDir();

    // Every remote currently defined, in filename order (i.e. sanitized
    // name order -- not necessarily the order they were created in).
    static QList<SshRemote> loadAll();

    // One remote by its display name. Returns an entry with an empty
    // `name` (SshRemote::isValid() may still be true or false independent
    // of that) if none exists under that name -- callers that need to
    // distinguish "not found" from "found but host is blank" should check
    // the returned name, not just isValid().
    static SshRemote load(const QString &name);

    // Writes `remote` under remote.name, creating the directory if needed.
    // If this is a rename (oldName differs from remote.name and is
    // non-empty), the old file is removed first so the catalog doesn't end
    // up with both. Returns false on I/O failure or an empty remote.name.
    static bool save(const SshRemote &remote, const QString &oldName = QString());

    // The display name of an existing catalog entry (other than `oldName`,
    // the one being edited) whose sanitized filename would collide with
    // `name`'s -- or an empty string if `name` is safe to save. Two display
    // names that sanitize to the same file (e.g. "Windows box" and
    // "Windows-box") would otherwise silently overwrite each other, leaving
    // one of them un-selectable in the per-box checklist.
    static QString collidingName(const QString &name, const QString &oldName = QString());

    // Deletes the on-disk entry. Returns true if it didn't exist or was
    // removed successfully.
    static bool remove(const QString &name);

    // Turns a display name into a safe filename: lowercase, anything
    // outside [a-z0-9_-] becomes '-', repeats collapsed. Unlike
    // DockerBackend's sanitizers this has no bash-era precedent to
    // byte-match -- any reasonable ASCII-safe mapping is fine here.
    static QString sanitizeName(const QString &name);
};
