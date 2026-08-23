# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

This repo defines a sandboxed Docker environment for running Claude Code itself against an arbitrary host project, plus a native desktop GUI (`gui/`, C++/Qt6) for managing those sandboxes: seeing what's running, creating/reopening conversations, and interacting with them in an embedded terminal.

- `Dockerfile` — builds the `claude-code` image (Ubuntu base) with `@anthropic-ai/claude-code` installed globally. The container's built-in uid/gid-1000 user is renamed to `user` so file ownership matches the host user, and gets passwordless sudo. The image's `WORKDIR` is `/home/user/workspace`, but that's only a fallback: every box the GUI launches overrides it with `-w <target_dir>` (see "How a box is actually launched" below). Otherwise unchanged by the GUI work — it's still just the sandbox image definition.
- `gui/` — the app. See its own section below.

There used to be a set of bash launcher/manager scripts (`claude-box.bash`, `claude-box-manager.bash`, `claude-box-lib.bash`, `claude-box-resume-all.bash`, `claude-box-resume.service`, `docker-compose.yml`) doing all of this from the terminal. They're retired in favor of `gui/`, but intentionally left on disk (untracked — see `.gitignore`) as a working fallback until the GUI has fully replaced them in daily use. Don't extend them further; port improvements to `gui/` instead.

## `gui/` — the C++/Qt6 app

A single-window dashboard: a filterable table of boxes plus a details panel on top (refreshed on a 3s timer), a tab strip of embedded terminals below, and menu-bar/toolbar/context-menu actions (New/Open/Close/Remove/Purge…).

**Keyboard convention — load-bearing.** Every application shortcut is a `Ctrl+Shift` chord (plus `F5` and `Alt+1..9`). This is not stylistic: Qt resolves `QAction` accelerators in `QShortcutMap` *before* the focused widget sees the key, so a plain `Ctrl+N` menu shortcut would be stolen out from under Claude Code running in a terminal pane. `TerminalWidget::event()` handles `QEvent::ShortcutOverride` and accepts every key that isn't a reserved chord, which forces Qt to deliver it as an ordinary key press to the terminal instead. Adding a plain-`Ctrl` shortcut anywhere in the menus will silently break that key inside every terminal — add it to `isApplicationChord()` in `TerminalWidget.cpp` or don't add it.

**Container lifecycle — no tmux.** A box is created with `docker run -d -t --rm --name <box> ...` (detached, but with a tty allocated so `claude`'s interactive UI has something to render into). Viewing/interacting with it means spawning `docker attach <name>` wired to an app-owned PTY and rendering that in a tab. Closing a tab only kills the local `docker attach` client (`docker attach` detaching doesn't stop what it's attached to) — the container keeps running until an explicit `docker stop` (the **Close** action). This is why there's no tmux dependency and no separate crash-signal file: `docker ps` is always the live "is it running" ground truth, so a box that isn't running just shows up as `Known` (reopenable), whether that's because you closed it or the host rebooted.

**Naming.** Containers are always `claude-agent-<base>`, where `<base>` is the issue slug (when the issue workflow fires) or the target directory's basename, run through a sanitizer; collisions get `-2`, `-3`, … appended. Every `docker ps` call filters on `name=^claude-agent-`, so anything outside that prefix is invisible to the app. The two sanitizers in `DockerBackend.cpp` (`sanitizeBoxBase`, `slugifyIssueName`) exist to byte-match the old bash `tr`/`sed` pipelines exactly — keep them ASCII-only rather than "improving" them with Qt's unicode-aware classification, or existing boxes stop resolving.

**Data model.** `~/.claude-box/known/<container-name>` — one `key=value` file per box (`target_dir`, `session_uuid`, `conversation_name`, `yolo`, `rc`, repeatable `port=`/`dir=`), permanent until an explicit Purge/Remove. `yolo` maps to `claude --dangerously-skip-permissions`, `rc` to `claude --remote-control`. `session_uuid` is minted host-side with `QUuid::createUuid()` and handed to `claude --session-id` on first creation, so the same conversation can be resumed later via `claude --resume <uuid>` — deliberately not `--continue`, which only grabs "the most recent conversation in this directory" and gets ambiguous once a directory has several tracked boxes. See `gui/src/BoxRecord.{h,cpp}`.

**How a box is actually launched.** Beyond `-d -t --rm --name`, every box gets `--user user`, `-e IS_SANDBOX=1`, `-w <target_dir>`, and three bind mounts: `<target_dir>:<target_dir>` (the host path is reproduced *identically* inside the container — Purge's `~/.claude/projects/<path-with-/-replaced-by->` encoding only works because of this), plus the host's `~/.claude` and `~/.claude.json`. That last mount is why Purge deliberately never touches `~/.claude.json`: it's the live host file, not a per-box copy.

The container doesn't exec `claude` directly. It runs `bash -c '<gitSetup> && shift && exec claude "$@"' _ <target_dir> <claude args...>`, where `<gitSetup>` is `git config --global --add safe.directory "$1"` — collapsing to `true` when the target dir has a `gitconfig` file, since that file already sets `safe.directory = *` and becomes `GIT_CONFIG_GLOBAL`.

**Components (`gui/src/`):**
- `Theme` — the dark palette and stylesheet, applied in `main()` before any widget exists. Hand-built rather than inherited from the desktop theme because the terminal panes are unavoidably dark and a light system theme around them read as two different programs. Widgets that paint themselves (`TerminalWidget`, the table's status dots) read `Theme::` accessors instead of hardcoding colors.
- `Icons` — toolbar/menu icons drawn with `QPainter` at several sizes, not loaded from an icon theme or `.qrc`. Keeps the build free of resource plumbing and guarantees the icons match the fixed dark palette on any desktop.
- `BoxDetailsPanel` — properties view for the selected row: container name, directory, session uuid, flags, ports, mounts. Merges the live `BoxInfo` with the on-disk `BoxRecord`; a box started outside this app has no record and shows dashes rather than another box's settings. Captions stack above values in a plain `QVBoxLayout` — a `QFormLayout` keeps its field column too narrow here and silently clips long values instead of wrapping them.
- `BoxRecord` — the record above: load/save/loadAll (plus a `remove()` that's currently unused; Purge deletes record files by path).
- `DockerBackend` — every `docker` interaction (`ps`/`stats`/`run -d -t --rm`/`stop`/`rm`), synchronous `QProcess` calls (deliberate: these are short local commands invoked from user actions or a timer, not worth the complexity of a fully async design here). `createNew()` is where the old bash launcher's per-project conventions got reimplemented natively: an executable `new-issue.sh` in the target dir plus a non-empty conversation name triggers the issue-provisioning workflow (folder + opening prompt, mirroring the old `--name` behavior); a `gitconfig` file becomes `GIT_CONFIG_GLOBAL`; an `agent.env` file becomes `--env-file`.
- `PtySession` — the one genuinely event-driven piece: allocates a PTY via `forkpty()`, execs a program (`docker attach <name>`) with it, and exposes `dataReady`/`write`/`resize` around a `QSocketNotifier` on the master fd.
- `TerminalWidget` — libvterm-backed rendering (`vterm_input_write` fed from `PtySession::dataReady`, cell grid painted with `QPainter`, keystrokes turned into PTY input via `vterm_keyboard_key`/`vterm_keyboard_unichar`). Leaning on libvterm for VT100/xterm correctness rather than hand-rolling ANSI parsing is the load-bearing decision here — Claude Code's own TUI (color, alternate screen, live-updating status lines) is exactly what a partial terminal emulator renders as garbage. v1 scope note: no scrollback buffer, no text selection. When the attached process exits the widget does **not** close its own tab: it keeps the last screen, dims it, and paints a banner (`isDisconnected()`), because a box can die while you're looking at a different tab and silently removing the terminal loses both the fact and the evidence.
- `BoxTableModel` — `QAbstractTableModel` over `QList<BoxInfo>`, supplying status dots (`DecorationRole`), per-column colors, row tooltips, and a `SortRole` that orders Status by urgency (Running, Stopped, Known) rather than alphabetically. `MainWindow` puts a `QSortFilterProxyModel` in front of it for the filter box, so **every row index arriving from the view must be mapped back through the proxy** before it means anything — and Purge's "is anything running against this directory" check deliberately walks the *source* model, since a filtered-out running box is still a running box.
- `NewBoxDialog` — the creation form (dir/name/yolo/rc/repeatable port+dir-mount rows); `MainWindow` turns its output into a `BoxRecord` and calls `DockerBackend::createNew`.
- `MainWindow` — wires all of the above together: the menu bar, toolbar, status-bar counters, filter box, details panel, window/splitter geometry persisted via `QSettings`, the `QTimer`-driven refresh, the tab strip, action enablement by row status (`Open` only for `Known`, `Close` only for `Running`, `Remove` only for `Stopped`-but-not-yet-removed, `Purge` for any row with a directory), and `Purge`'s logic (removes `~/.claude/projects/<path-with-/-replaced-by->` plus matching `known/` records for that directory only — never the directory's own contents, never `~/.claude.json`, and refuses while a matching box is running).

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

- `Dockerfile` changes: keep the uid/gid remapping (`user` = 1000:1000) in mind — it exists specifically to avoid file-permission mismatches between host and container. (The `USER_ID`/`GROUP_ID` build args are currently declared but unused; the remapping is hardcoded to 1000.)
- `gui/` changes: the app now builds clean (no warnings-as-errors configured, no errors) against Qt 6.10.2 and libvterm 0.3.3 on Ubuntu resolute. The libvterm C API surface can shift between versions, so if you're on a different libvterm, `TerminalWidget.cpp` is the first place a build breaks.
- **libvterm gotcha, learned the hard way:** `vterm_screen_set_callbacks()` stores the `VTermScreenCallbacks*` you give it — it does *not* copy the struct. Passing a stack local segfaults on the first byte of container output. `screenCallbacks()` in `TerminalWidget.cpp` exists solely to hand it something with static lifetime; don't "simplify" it back into a local.
- **Also non-obvious:** libvterm only allocates the alternate-screen buffer if you call `vterm_screen_enable_altscreen(screen, 1)`. Without it the altscreen escape is silently ignored and Claude Code's TUI draws over the primary buffer instead of switching — which looks like a rendering bug, not a missing-init bug.
