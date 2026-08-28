#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

// One Claude Code conversation transcript found on disk, read straight
// out of Claude's own store rather than out of ~/.claude-box/known/.
//
// This exists because boxes are not the only way conversations get
// started: the same directory may have been worked on from a plain
// `claude` invocation in a terminal, or from an older box whose record
// was purged. Those conversations are perfectly resumable -- their
// transcripts sit in ~/.claude/projects/ like any other -- there just
// isn't a BoxRecord pointing at them, so nothing in the dashboard could
// reach them before. NewBoxDialog offers this list so a new box can
// adopt an existing conversation instead of always minting a fresh one.
struct ConversationInfo {
    QString sessionUuid;   // the transcript's filename, and what `claude --resume` wants
    QString title;         // ai-title if the transcript has one, else the opening prompt
    QString lastPrompt;    // most recent user prompt, for the tooltip
    QDateTime lastActive;  // transcript mtime -- cheaper and no less accurate than parsing
    int userTurns = 0;
    QString trackedBy;     // container name of the BoxRecord already bound to this uuid, if any
};

namespace ConversationCatalog {

// ~/.claude/projects/<encoded>, where <encoded> is the absolute target
// dir with every non-alphanumeric byte replaced by '-'. That encoding is
// Claude Code's, not ours; it is reproduced here (and verified against
// the `cwd` recorded inside the transcripts) because it's the only way
// to find a directory's conversations. Note it collapses '.' as well as
// '/', so `/a/b/app.example.com` becomes `-a-b-app-example-com`.
QString projectDirFor(const QString &targetDir);

// Every resumable conversation for targetDir, newest first. Transcripts
// with no user turns (bridge-session stubs and other zero-content files
// Claude leaves behind) are skipped -- resuming one lands you in an
// empty session, which is just a worse "new conversation".
QList<ConversationInfo> forDirectory(const QString &targetDir);

// Human-readable "3h ago" / "2d ago" / absolute date for older ones.
QString relativeTime(const QDateTime &when);

}
