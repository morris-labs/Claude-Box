#pragma once

#include "BoxRecord.h"

#include <QString>
#include <QStringList>

// Turns an existing workspace subfolder (an independent clone the agent set up
// under the project root) into a git worktree of the project's primary
// repository, keeping the conversation resumable.
//
// Nothing is deleted. The new worktree is built beside the old folder, the
// conversation's transcripts are copied (never moved) to where Claude will
// look for them from the worktree, and only after all of that succeeds is the
// old folder renamed to SLUG.pre-worktree. A failure before the rename removes
// what this run created and leaves the old folder, the record and every
// transcript as they were. Run plan() first and show its report().
namespace WorktreeMigration {

struct Plan {
    bool ok = false;
    QString problem; // why this subfolder cannot be converted, when !ok

    QString oldDir;       // PROJECT/SLUG, the clone
    QString repo;         // the primary repository
    QString slug;
    QString branch;       // the new branch, claude/SLUG
    QString cloneBranch;  // what the clone has checked out
    QString worktree;     // REPO/.worktrees/SLUG
    QString notesDir;

    QStringList copyFiles;     // relative paths copied over from the clone
    QStringList removeFiles;   // relative paths deleted in the clone, removed in the worktree
    QStringList skippedIgnored; // ignored paths left behind because they are build output
    QStringList notesFiles;    // PLAN.md, STATUS.md, INFRA.md moved to the notes directory
    QStringList transcripts;   // transcript file names to copy
    QString transcriptFrom;    // project directory the transcripts are in now
    QString transcriptTo;      // where Claude looks from the worktree
    bool ownTranscriptFound = true; // the box's own session transcript exists

    QStringList report() const;
};

Plan plan(const BoxRecord &rec);

// Performs the conversion and updates and saves `rec`. On failure rolls back
// and returns false with `error` set.
bool execute(BoxRecord &rec, const Plan &plan, QString *error);

} // namespace WorktreeMigration
