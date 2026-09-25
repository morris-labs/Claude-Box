## Active plan

plan-mac-release.md

## Current step

mac-release branch fully smoke-tested; merge into main deferred

## What's done

- Workspace initialized, repo cloned to Mac_Release/claude-box
- plan-mac-release.md written
- Phase 1 (515975b): forkpty header, Docker socket path, MACOSX_BUNDLE,
  Info.plist.in, generate-icns target, icon fallback in main.cpp
- Phase 2+3 (eb3851b): onOpenExternal() macOS branch, macdeployqt + DMG targets
- Code review by fresh Sonnet 4.6 (high effort): 3 real bugs fixed (048605e)
- Smoke-test by claude-box-mac session -- all 6 items green:
  - Build: PASS
  - Launch + dark theme + macOS menu bar: PASS
  - Dock icon (generate-icns): PASS
  - Keyboard passthrough in terminal tab: PASS
  - Docker API / Build Image: PASS (zip fix: 6e4caa6)
  - Open External Terminal: PASS (open -a Terminal script.sh: a391771)
- Additional fixes found during smoke-test (all on mac-release / GitHub):
  - 1bdc62c: PATH augmentation in PtySessionUnix for docker binary
  - 56a54eb: Add Docker.app bundle bin to PATH candidates
  - 6e4caa6: Add zip to Dockerfile (SDKMAN dep)
  - dc25a65: Resolve full docker path for AppleScript command
  - 2d68945: Add ~/.docker/bin to docker search paths
  - 2bee78f: Add activate to iTerm2 AppleScript
  - a391771: Replace osascript with open -a Terminal script.sh
  - e5057d8: Two external-terminal actions (Open Shell + Attach to Claude);
    tmux wrapping in DockerBackend both launch paths
  - 4ca76c1: Dockerfile CMD wraps claude in tmux new-session

## What's next

- Merge mac-release into main (deferred; user coordinating with other sessions)

## Blockers / decisions pending

- Merge timing is user's call
- Developer ID certificate decision for distribution (Phase 3)
