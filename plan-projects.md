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

## Stages

Inheritance of ports and mounts needs a shared container. With one container
per box, two conversations in a project would publish the same host ports.
So the stages are ordered to keep each one shippable.

1. Project layer. Add a project record under `~/.claude-box/projects/`. Group
   existing boxes by `target_dir` in the table and the details panel. Migrate
   by deriving projects from the existing records. No runtime change, and
   ports and mounts stay per box.
2. Worktrees. For a project with a primary repository, "New conversation"
   creates `git worktree add` on a new branch and runs Claude with the
   worktree as its working directory. This also fixes resuming, because the
   transcript is then keyed to the directory Claude runs in
   (`ConversationCatalog::projectDirFor`). Removal refuses when the worktree
   has uncommitted work or an unmerged branch.
3. Shared container. One container per project, with the pool of 10 ports.
   Each conversation is a `docker exec -w WORKTREE` into its own tmux
   session. Running state comes from the tmux sessions and
   `~/.claude/sessions`, not `docker ps`. Ports and mounts move to the
   project, so changing them restarts every conversation in it.

## Open questions

- Where worktrees live: `.claude/worktrees/` (what `claude --worktree` uses)
  or a directory the app owns. Decide in stage 2.
- Whether to drive worktrees with `claude --worktree` or with `git worktree`
  directly. Direct control is likely better, because the app needs to know
  the path before it launches.
- Behavior when a project's primary repository has uncommitted changes at
  worktree creation time.
