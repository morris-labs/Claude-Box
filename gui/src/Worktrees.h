#pragma once

#include <QString>
#include <QStringList>

// Git worktree handling for conversations. Everything shells out to `git` on
// the host: worktrees stay on disk, under the project, so they are visible in
// the host's file manager and survive the container.
//
// The functions return false and fill `error` on failure and never throw. None
// of them delete anything git has not already agreed is safe to delete: remove()
// refuses a worktree with uncommitted work and a branch with unmerged commits.
namespace Worktrees {

// Directory name under the primary repository that holds the worktrees.
inline const QString kDirName = QStringLiteral(".worktrees");
// Branch prefix for the branch each conversation works on.
inline const QString kBranchPrefix = QStringLiteral("claude/");
// Directory under the project root for per-conversation notes.
inline const QString kNotesDirName = QStringLiteral(".claude-box");

struct Result {
    bool ok = false;
    QString output; // stdout on success, stderr on failure
};

// Runs `git -C dir args...`.
// `trim` strips surrounding whitespace from the output; turn it off for
// formats where leading spaces mean something (`status --porcelain -z`).
Result git(const QString &dir, const QStringList &args, int timeoutMs = 30000, bool trim = true);

bool isRepo(const QString &dir);
// Absolute path of the repository's top-level working directory.
QString topLevel(const QString &dir);
QString currentBranch(const QString &dir); // empty when HEAD is detached
QString originUrl(const QString &dir);

// Where the worktree for `slug` goes: REPO/.worktrees/SLUG.
QString worktreePath(const QString &repo, const QString &slug);
QString branchFor(const QString &slug);

// Per-conversation notes: ROOT/.claude-box/conversations/SLUG, and the
// shared ROOT/.claude-box/INFRA.md.
QString notesDir(const QString &projectRoot, const QString &slug);
QString infraFile(const QString &projectRoot);

// Adds `.worktrees/` and `.claude-box/` to the repository's info/exclude so
// neither shows up as untracked. info/exclude lives in the common git
// directory, so one entry covers every worktree. Idempotent.
bool ensureExcluded(const QString &repo, QString *error = nullptr);

// `git worktree add`, creating the branch when it does not exist. Reuses a
// worktree that is already there. On Windows, turns on relative worktree
// paths first, because the absolute path in the .git file is a host path the
// container cannot resolve there.
bool add(const QString &repo, const QString &slug, QString *pathOut, QString *error);

// Creates the notes directory for a conversation. Returns its path.
QString ensureNotesDir(const QString &projectRoot, const QString &slug);

struct RemoveCheck {
    bool safe = false;
    QString reason; // why not, when !safe
};
// Whether the worktree can go without losing work: nothing uncommitted and
// nothing on its branch that is not reachable from another branch.
RemoveCheck checkRemovable(const QString &repo, const QString &path);
// Removes the worktree and its branch, only when checkRemovable() says so.
bool remove(const QString &repo, const QString &path, QString *error);

} // namespace Worktrees
