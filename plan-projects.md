# Plan: projects, worktrees, and shared containers

Branch for the work: one branch per stage, named `feature/projects-N-...`.

## Goal

Make the project directory the main object in the app. A box becomes one
conversation inside a project. Conversations get their own git worktree
instead of a plain subfolder, and all conversations in a project share one
container.

## Decisions

- Nearly all work goes through git, so worktrees are the default for a project
  that has a git repository. Plain subfolders remain for a project without
  one.
- A project has a root directory and a primary repository. For most projects
  the root is the repository. For a Minecraft directory the primary repository
  is the one under development, and the sibling repositories are read-only
  references. Worktrees come from the primary repository only. The root stays
  mounted and is passed to Claude with `--add-dir`, so the references stay
  visible.
- Worktrees stay on disk, on the host, under the project. Because the
  container mounts the project at the same path, the absolute path in a
  worktree's `.git` file stays valid on Linux and macOS. On Windows, set
  `worktree.useRelativePaths` (git 2.48 or later) when creating the worktree.
- Each project reserves a pool of 10 host ports. A conversation takes some
  from the pool, and the opening prompt names them.
- Per conversation: model, effort, permission flag, session, name.
  Per project, inherited by every conversation: ports (the pool), mounts, SSH
  remotes, prompts, and later the container.

## Per-conversation notes

`PLAN.md` and `STATUS.md` describe one line of work, so each conversation
gets its own pair. They live in the project directory, outside any worktree,
so you can find them without knowing where a worktree is:

```
PROJECT/.claude-box/INFRA.md                    shared environment notes
PROJECT/.claude-box/conversations/SLUG/PLAN.md
PROJECT/.claude-box/conversations/SLUG/STATUS.md
```

- `.claude-box/` is added to the repository's `info/exclude`, which every
  worktree shares, so the notes are never committed and branches never
  conflict over them.
- The opening prompt names the exact paths, so the agent does not rely on the
  files being in its working directory. A project without worktrees or
  subfolders keeps the root-level files, as today.
- The notes are not inside the worktree, so removing a worktree never removes
  them. The directory name is a single constant, so renaming it is a
  one-line change.

## Migrating existing subfolders

An existing workspace subfolder is usually an independent clone that the
agent set up, not part of the root repository, so it cannot simply be
re-labeled as a worktree. A "Convert to worktree" action handles each
conversation, with a dry run that reports what it would do first. It never
deletes anything.

1. Classify the subfolder:
   - A clone of the same remote as a project repository: convertible.
   - A repository with a different remote: not converted in stage 2; it
     stays a plain subfolder until a project can have several repositories.
   - Not a repository: leave as a plain subfolder.
2. For a convertible subfolder, fetch its branches into the project
   repository, create the worktree on the same branch, and copy over
   uncommitted changes and untracked files. Refuse if the clone has stashes
   or a detached HEAD that cannot be mapped.
3. Move the old folder to `SLUG.pre-worktree` and leave removal to the user.
4. Copy the conversation's transcript to the project directory that matches
   the new working directory (see "Old conversations are never lost").

## Old conversations are never lost

Checked on Claude Code 2.1.300: `claude --resume UUID` in the terminal looks
only in the project directory of its own working directory ("No conversation
found" otherwise). A transcript copied into the new directory's project folder
resumes with its full history. A headless `claude -p --resume` found a
transcript in another directory and appended to the original file, so do not
rely on the interactive behavior matching it.

Rules for the migration:

- Copy, never move. The original transcript and the original box record stay
  where they are.
- Before changing anything, write a backup of the record to
  `~/.claude-box/backup/` and list every transcript that will be copied.
- A conversation is marked converted only after its copy exists and has the
  same size as the original. Until then the old record keeps working.
- Nothing in the migration deletes a transcript. Purge stays the only action
  that does, and it asks first.
- The conversation picker also offers transcripts found under the old project
  root for the project's worktrees, so a conversation that was not converted
  can still be adopted.

## Stages

Inheritance of ports and mounts needs a shared container. With one container
per box, two conversations in a project would publish the same host ports.
So the stages are ordered to keep each one shippable.

1. Project layer. Add a project record under `~/.claude-box/projects/`. Group
   existing boxes by `target_dir` in the table and the details panel. Migrate
   by deriving projects from the existing records. No runtime change, and
   ports and mounts stay per box.
2. Worktrees and migration (built on `feature/projects-2-worktrees`). For a project with a primary repository, "New conversation"
   creates `git worktree add` on a new branch and runs Claude with the
   worktree as its working directory. This also fixes resuming, because the
   transcript is then keyed to the directory Claude runs in
   (`ConversationCatalog::projectDirFor`). Removal refuses when the worktree
   has uncommitted work or an unmerged branch. Includes the per-conversation
   notes and the "Convert to worktree" action described above.
3. Shared container. One container per project, with the pool of 10 ports.
   Each conversation is a `docker exec -w WORKTREE` into its own tmux
   session. Running state comes from the tmux sessions and
   `~/.claude/sessions`, not `docker ps`. Ports and mounts move to the
   project, so changing them restarts every conversation in it.

## Open questions

- Multiple repositories per project: the per-issue subfolders so far clone
  one shared repository, so stage 2 supports one primary repository per
  project. Several can be added later.
- Where worktrees live: `.claude/worktrees/` (what `claude --worktree` uses)
  or a directory the app owns. Decide in stage 2.
- Whether to drive worktrees with `claude --worktree` or with `git worktree`
  directly. Direct control is likely better, because the app needs to know
  the path before it launches.
- Behavior when a project's primary repository has uncommitted changes at
  worktree creation time.
