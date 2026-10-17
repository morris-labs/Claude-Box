# Status

## Active plan
`PLAN.md`: link existing conversations (branch `fix/link-existing-conversations`).

## Current step
Step 4 of 5: verify both flows in the running GUI. Needs a display and Docker,
which this container lacks.

## Done since the last checkpoint
- Removed the account-switch feature (commit d320555). Kept the unrelated work
  that followed it: async open and close, Remove fix, CI, update buttons.
- Fixed adoption in `NewBoxDialog`: `updateWorkspaceHint()` no longer resets the
  conversation combo, so a picked conversation now reaches `createNew()`.
- Edit Box can re-select the conversation. A change asks for a restart, runs the
  adoption check, and "Start a new conversation on next restart" clears the link.
- `CLAUDE.md` documents both. `INFRA.md` and `PLAN.md` are new.
- The GUI compiles (out-of-tree build).

## Next
- Test on the host: adopt a conversation whose title matches an existing folder,
  then edit a running box and switch its conversation.
- Decide on auto-detecting `/clear` (new session id) on restart. See `PLAN.md`.
- Merge the branch when the user approves. No push until told.

## Blockers and pending decisions
- Not tested at runtime. No Docker or display here.
- Earlier pending items: PR #3 prompt templates, Windows phase 4 fold-in, macOS
  DMG is x86_64 only.
- The v3.3.1 to v3.3.4 tags and releases still contain the account-switch code.
