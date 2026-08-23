# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

This repo defines a sandboxed Docker environment for running Claude Code itself against an arbitrary host project, plus a native desktop GUI (`gui/`, C++/Qt6) for managing those sandboxes: seeing what's running, creating/reopening conversations, and interacting with them in an embedded terminal.

- `Dockerfile` — builds the `claude-code` image (Ubuntu base) with `@anthropic-ai/claude-code` installed globally. The container's built-in uid/gid-1000 user is renamed to `user` so file ownership matches the host user. The container runs as `user` with working directory `/home/user/workspace`. Unchanged by the GUI work below — it's still just the sandbox image definition.
- `gui/` — the app. See its own section below.

There used to be a set of bash launcher/manager scripts (`claude-box.bash`, `claude-box-manager.bash`, `claude-box-lib.bash`, `claude-box-resume-all.bash`, `claude-box-resume.service`, `docker-compose.yml`) doing all of this from the terminal. They're retired in favor of `gui/`, but intentionally left on disk (untracked — see `.gitignore`) as a working fallback until the GUI has fully replaced them in daily use. Don't extend them further; port improvements to `gui/` instead.

## `gui/` — the C++/Qt6 app

A single-window dashboard: a live-refreshing table of boxes on top, a tab strip of embedded terminals below, toolbar/context-menu actions (New/Open/Close/Remove/Purge…).

**Container lifecycle — no tmux.** A box is created with `docker run -d -t --rm --name <box> ...` (detached, but with a tty allocated so `claude`'s interactive UI has something to render into). Viewing/interacting with it means spawning `docker attach <name>` wired to an app-owned PTY and rendering that in a tab. Closing a tab only kills the local `docker attach` client (`docker attach` detaching doesn't stop what it's attached to) — the container keeps running until an explicit `docker stop` (the **Close** action). This is why there's no tmux dependency and no separate crash-signal file: `docker ps` is always the live "is it running" ground truth, so a box that isn't running just shows up as `Known` (reopenable), whether that's because you closed it or the host rebooted.

**Data model.** `~/.claude-box/known/<container-name>` — one `key=value` file per box (`target_dir`, `session_uuid`, `conversation_name`, `yolo`, `rc`, repeatable `port=`/`dir=`), permanent until an explicit Purge/Remove. `session_uuid` is minted via `claude --session-id` on first creation and used to resume that exact conversation later via `claude --resume <uuid>` — deliberately not `--continue`, which only grabs "the most recent conversation in this directory" and gets ambiguous once a directory has several tracked boxes. See `gui/src/BoxRecord.{h,cpp}`.

**Components (`gui/src/`):**
- `BoxRecord` — the record above: load/save/loadAll.
- `DockerBackend` — every `docker` interaction (`ps`/`stats`/`run -d -t --rm`/`stop`/`rm`), synchronous `QProcess` calls (deliberate: these are short local commands invoked from user actions or a timer, not worth the complexity of a fully async design here). `createNew()` is where the old bash launcher's per-project conventions got reimplemented natively: an executable `new-issue.sh` in the target dir plus a non-empty conversation name triggers the issue-provisioning workflow (folder + opening prompt, mirroring the old `--name` behavior); a `gitconfig` file becomes `GIT_CONFIG_GLOBAL`; an `agent.env` file becomes `--env-file`.
- `PtySession` — the one genuinely event-driven piece: allocates a PTY via `forkpty()`, execs a program (`docker attach <name>`) with it, and exposes `dataReady`/`write`/`resize` around a `QSocketNotifier` on the master fd.
- `TerminalWidget` — libvterm-backed rendering (`vterm_input_write` fed from `PtySession::dataReady`, cell grid painted with `QPainter`, keystrokes turned into PTY input via `vterm_keyboard_key`/`vterm_keyboard_unichar`). Leaning on libvterm for VT100/xterm correctness rather than hand-rolling ANSI parsing is the load-bearing decision here — Claude Code's own TUI (color, alternate screen, live-updating status lines) is exactly what a partial terminal emulator renders as garbage. v1 scope note: no scrollback buffer, no text selection.
- `BoxTableModel` — thin `QAbstractTableModel` over `QList<BoxInfo>` for the dashboard.
- `NewBoxDialog` — the creation form (dir/name/yolo/rc/repeatable port+dir-mount rows); `MainWindow` turns its output into a `BoxRecord` and calls `DockerBackend::createNew`.
- `MainWindow` — wires all of the above together: the `QTimer`-driven refresh, the tab strip, action enablement by row status (`Open` only for `Known`, `Close` only for `Running`, `Remove` only for `Stopped`-but-not-yet-removed, `Purge` for any row with a directory), and `Purge`'s logic (removes `~/.claude/projects/<path-with-/-replaced-by-->` plus matching `known/` records for that directory only — never the directory's own contents, never `~/.claude.json`'s live per-project entry, and refuses while a matching box is running).

**Build (host, not this container — see below):**

```bash
sudo apt install qt6-base-dev cmake build-essential libvterm-dev pkg-config
cd gui
cmake -B build
cmake --build build
./build/claude-box-gui
```

If `libvterm-dev` isn't packaged/workable on your host, `QTermWidget` is the documented fallback terminal-widget choice, but would mean reworking `TerminalWidget`.

## Working in this repo

- `Dockerfile` changes: keep the uid/gid remapping (`user` = 1000:1000) in mind — it exists specifically to avoid file-permission mismatches between host and container.
- `gui/` changes: this was written and iterated on inside a sandboxed Claude Code container with no `cmake`/`g++`/Qt6/`libvterm`/display available, so it has never actually been compiled. The first real build is likely to surface real errors, especially in `TerminalWidget.cpp` (the libvterm C API surface — exact struct/enum names can vary slightly by version) and `PtySession.cpp` (PTY/fork plumbing). That's expected; paste compiler errors back to Claude Code to fix rather than debugging the libvterm API from scratch.
