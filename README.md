# Claude Box

Claude Box runs [Claude Code](https://docs.claude.com/claude-code) in a
sandboxed Docker container, and gives you a native desktop GUI for managing
those sandboxes: seeing what's running, creating and reopening conversations,
and interacting with them in an embedded terminal.

Each sandbox is a disposable container built from a single `Dockerfile`. The
GUI mounts your project directory into the container at the identical path,
runs Claude Code with `--dangerously-skip-permissions` (a per-box toggle,
on by default), and keeps the container alive with `docker run --rm` until
you close it, so nothing lingers on disk beyond the container's `--rm`
lifecycle and your project files.

## Features

### Dashboard and terminal

- **Dashboard**: a filterable table of every `claude-agent-*` container, with
  live status, a CPU and memory usage bar per box, and a details panel for the
  selected box. A separate Usage tab shows a live summary across every running
  box.
- **Embedded terminal**: attach to a running box in a tab and interact with
  Claude Code directly, backed by a real VT100/xterm terminal emulator
  ([libvterm](https://www.leonerd.org.uk/code/libvterm/)), with text selection
  and copy and paste (Ctrl+Shift+C and Ctrl+Shift+V).
- **Multi-select actions**: select several boxes at once to open, stop, or
  remove them together.
- **Open in an external terminal**: attach to a running box's `tmux` session
  in your system terminal instead of an in-app tab for either the Claude Code
  session or the docker shell session.
- **Single instance**: launching the app while it's already running raises
  the existing window instead of opening a second one.

### Conversations and boxes

- **Conversation management**: start a new conversation, resume a known one,
  adopt a conversation that Claude Code already created outside the app, or
  fork an existing conversation into a new box.
- **Per-box configuration**: model, reasoning effort, permission bypass, port
  forwards, extra mounts, and SSH remotes - all editable per box.
- **Workspace sub-folders**: point a box at your project directory or repo root,
  then create a working sub-folder from the conversation name so multiple Issues or
  PRs can be worked on side by side without colliding.
- **Working directory management**: move or change a box's project directory
  from within the app, and relink a box whose directory has gone missing due to
  external moves or renames.
- **Automatic port allocation**: each new box gets a block of 5 host ports
  from a shared range, so simultaneous boxes never collide.
- **Resume after reboot**: the app remembers which boxes were running and
  offers to resume them after your app or host restarts.

### SSH remotes

- **Shared remote catalog**: define an SSH remote once and attach any number
  of boxes to it; boxes that share a remote share one tunnel instead of each
  opening a redundant connection.
- **Port forwarding**: editable `-L`/`-R` forwards per remote, with a
  per-remote tunnel status indicator and manual reconnect.
- **Connection testing**: test an SSH connection from within the app, with an
  interactive login prompt when a key needs a passphrase or hasn't been
  authorized yet.

### Setup

- **First-run setup wizard**: checks that Docker and an SSH client are
  reachable, builds the sandbox image from this repository's `Dockerfile`,
  and generates an SSH keypair if you don't already have one.

### Cross-platform

A native build for Linux, Windows, and Mac have been released, as well as
build scripts for building it yourself.

## How it works

A box is a container started with `docker run -d -i -t --rm --name
claude-agent-<name> ...`. The container's entrypoint starts a `tmux` session
running Claude Code, then waits in a keeper loop while that session is alive.
The GUI opens a tab with `docker exec -it ... tmux attach -t main`, wires that
process to a PTY, and renders it in a terminal tab. Closing a tab detaches the
`tmux` client — `bash` is PID 1 in the container, not the client, so the
container keeps running until Claude Code exits and `tmux` ends the session.
Because of `--rm`, `docker ps` is always the source of truth for whether a box
is running: nothing else needs to track container state across a restart or a
crash.

The container mounts your project directory at the same path inside the
container as it has on the host, plus your `~/.claude` and `~/.claude.json`
files, so Claude Code's own session and settings data works the same way it
does outside the sandbox.

## Prerequisites

- Docker, with the daemon reachable from your user account.

## Build and run

The build scripts in `scripts/` install all missing prerequisites automatically.

**Linux** (Ubuntu 22.04+, Fedora 36+, Arch):

```bash
./scripts/build-linux.sh
```

Produces an AppImage, `.deb`, and `.rpm` under `installer/`.

**macOS**:

```bash
./scripts/build-mac.sh
```

Requires Homebrew. Produces a `.dmg` under `installer/`.

**Windows** (Windows 10 1809+ or Windows 11):

```powershell
.\scripts\build-windows.ps1
```

Run in an elevated PowerShell session for the initial prerequisite install.
Produces an `.exe` installer under `installer/`.

The first run of the app walks you through a setup wizard that builds the
sandbox image from the repository's `Dockerfile`.

### Install a desktop entry (Linux)

To add Claude Box to your application launcher:

```bash
cmake --build gui/build --target desktop-install
```

This installs a `.desktop` file and icon under
`~/.local/share/{applications,icons}`. Launch the app by name from your
desktop environment, rather than by double-clicking the built binary or the
`.desktop` file directly.

## Repository layout

- `Dockerfile`: builds the `claude-code` sandbox image.
- `gui/`: the C++/Qt6 desktop application.
- `scripts/`: platform build scripts (`build-linux.sh`, `build-mac.sh`, `build-windows.ps1`).
- `CLAUDE.md`: detailed architecture and development notes for contributors.

## Development

For architecture details, platform-specific implementation notes, and
conventions for this codebase, see `CLAUDE.md`.
