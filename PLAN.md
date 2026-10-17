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
5. [todo] Decide whether to detect a `/clear` automatically on restart by
   comparing the newest transcript in the project directory with the record.
