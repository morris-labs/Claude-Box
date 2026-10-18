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
// old folder renamed to SLUG.pre-worktree. The aim is that the old folder can
// then be deleted by hand: every commit (all branches, tags and stashes), every
// uncommitted and ignored file, the local ignore rules and the transcripts are
// carried over. What cannot be (hooks and git settings only the clone had) is
// listed in the report, and build output is copied when asked. A failure before the rename removes
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
    QString notesDir;       // WORKTREE/.claude-box
    QString keepPrefix;     // refs/heads/claude-keep/SLUG/, where the extra commits are kept

    QStringList copyFiles;     // relative paths copied over from the clone
    QStringList removeFiles;   // relative paths deleted in the clone, removed in the worktree
    QStringList skippedIgnored; // ignored build output, copied only when includeBuildOutput is set
    bool includeBuildOutput = false;
    qint64 skippedBytes = 0;    // size of skippedIgnored
    QStringList keepBranches;   // the clone's other branches that exist nowhere else
    QStringList keepTags;       // tags that exist nowhere else
    QStringList keepStashes;    // stash commits, kept as branches
    int excludeRules = 0;       // lines of the clone's info/exclude to merge
    QStringList cloneOnly;      // hooks and git settings that are not carried over
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
