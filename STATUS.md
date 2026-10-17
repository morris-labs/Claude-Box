# Status

## Active plan
`PLAN.md`: link existing conversations, then `plan-projects.md` (branch
`feature/projects-2-worktrees`, which contains the earlier branches).

## Current step
On branch `feature/projects-2-worktrees` (stages 1 and 2 built; stage 3, the shared
container, not started). Needs a run on the host with Docker.
Step 4 of 7: verify the picker flows in the running GUI. Needs a display and Docker,
which this container lacks.

## Done since the last checkpoint
- Stage 2: git worktrees per conversation (New Box checkbox, default on for git
  projects), notes in `.claude-box/conversations/SLUG/`, Remove/Purge handling,
  and Box > Convert to Worktree. Worktree and migration logic tested against real
  git repositories (uncommitted, untracked, ignored files, transcripts, backup).
- Stage 1 of `plan-projects.md`: project list beside the table, filtering by project,
  New Box opens in the selected project. Tested with an offscreen harness; the app
  starts offscreen. Not yet looked at on a real display.
- Removed the account-switch feature (commit d320555). Kept the unrelated work
  that followed it: async open and close, Remove fix, CI, update buttons.
- Fixed adoption in `NewBoxDialog`: `updateWorkspaceHint()` no longer resets the
  conversation combo, so a picked conversation now reaches `createNew()`.
- Edit Box can re-select the conversation. A change asks for a restart, runs the
  adoption check, and "Start a new conversation on next restart" clears the link.
- `SessionWatcher` follows /clear and /rename in running boxes (tested against a
  real session: record's session_uuid and name updated). Not yet run in the GUI.
- Prompts now give `~/...` paths and fill placeholders by number (fixes the
  starter prompt that said "working in <conversation name>").
- Branches kept for earlier work: feature/account-switch, async-open-close,
  release-workflow, update-claude-code, update-packages, fix/remove-stopped-boxes.
- `CLAUDE.md` documents both. `INFRA.md` and `PLAN.md` are new.
- The GUI compiles (out-of-tree build).

## Next
- Test on the host: adopt a conversation whose title matches an existing folder,
  then edit a running box and switch its conversation.
- Run stages 1 and 2 on a host with Docker and a display; then decide on stage 3
  (shared container), which is not started.
- Code review: all 29 files reviewed twice over; findings fixed. The Worktrees and
  WorktreeMigration reviews led to safer Remove (detached HEAD, ignored files) and migration rollback.
- Fork/rewind button in the Claude desktop app fails: remote-control limitation of
  Claude Code, not claude-box. Nothing to do here.
- (old) Decide on auto-detecting `/clear` (new session id) on restart. See `PLAN.md`.
- Merge the branch when the user approves. No push until told.

## Blockers and pending decisions
- Not tested at runtime. No Docker or display here.
- Earlier pending items: PR #3 prompt templates, Windows phase 4 fold-in, macOS
  DMG is x86_64 only.
- The v3.3.1 to v3.3.4 tags and releases still contain the account-switch code.
