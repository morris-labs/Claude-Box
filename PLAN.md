# Plan: link existing conversations

Branch: `fix/link-existing-conversations`

## Goals

1. Drop the account-switch feature set.
2. Fix New Box so that picking an existing conversation resumes it.
3. Let the Edit Box dialog re-select a box's conversation.

## Findings

- Cause of goal 2: selecting a conversation fills in the name field. The name
  change called `NewBoxDialog::updateWorkspaceHint()`, which reset the
  conversation combo to "Start a new conversation" whenever a folder named
  after the conversation already existed. That is common, because boxes
  created with a workspace folder use the slug as the `--name`.
- Cause of the "old conversation returns" report: `/clear` in Claude Code
  starts a new session id. The box record keeps the old `session_uuid`, so a
  restart runs `claude --resume OLD_UUID`.

## Steps

1. [done] Revert the account-switch commits and the `projects/` symlink binds
   (commit d320555). Later unrelated work stays.
2. [done] Make `updateWorkspaceHint()` write only the hint label.
3. [done] Enable the conversation combo in edit mode, with a restart prompt
   and the adoption check in `MainWindow::onEdit`.
4. [todo] Run the dialog on a machine with a display and verify both flows.
5. [done] Fix the opening prompts: they named a folder, or the conversation
   name, instead of a path. `QString::arg(name, dir)` on a template that only
   used `%2` put the name in the `%2` slot. `PromptTemplates::expand()` now
   fills `%N` by number and prompts carry `~/...` container paths.
6. [done] Watch `~/.claude/sessions/<pid>.json` (it holds `sessionId` and
   `name`, and changes on `/clear` and `/rename`; checked on Claude Code
   2.1.300) and update the box record. Match a file to a box by `cwd` and the
   recorded `sessionId`, then track the pid and `pidDomain`.
7. [planned] Projects, worktrees and one container per project. See
   `plan-projects.md`. This branch ends at step 6.

## Projects (see plan-projects.md)

- Stage 1, project layer: done on `feature/projects-1-project-layer`.
- Stage 2, worktrees and migration: next.
- Stage 3, shared container: after stage 2.
