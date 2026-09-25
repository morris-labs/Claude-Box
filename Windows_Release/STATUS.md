## Active plan

Windows release: bring the GUI to a shippable state on Windows.

## Current step

All functional tests passed. Windows release is in a shippable state.

## What's done

- Parity sweep of all code added since phase 4
- Committed core Windows parity fixes (WIN32 flag, QDir::separator, NOMINMAX, stale comment)
- SetupWizard: "Rebuild Image" button always enabled (stale image mismatch now recoverable)
- Windows external terminal launch fixed: both onOpenExternal and onOpenExternalClaude
  now route through launchInWindowsTerminal() (wt.exe/conhost.exe); direct docker.exe
  from a console-less GUI silently fails
- detach-keys changed to ctrl-q,q (two-key sequence, avoids accidental detach)
- All three functional tests confirmed on real Docker Desktop (WSL2):
  1. SetupWizard Build Image (CommandTerminalDialog): PASS
  2. Open External Terminal / Open Claude in Terminal: PASS
  3. End-to-end with actual claude-code image (USER_NAME=claude): PASS
- Inno Setup installer script + icon added (gui/packaging/claude-box-gui.iss,
  claude-box.ico); installer/ output gitignored

## What's next

- Update CLAUDE.md "Windows port (in progress)" section to reflect clean test status
- Decide on installer distribution approach (Inno Setup exe vs other)
- Optional: confirm CLAUDE.md review history entry

## Blockers / decisions pending

None. All known blockers resolved.
