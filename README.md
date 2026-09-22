# Claude Box

Claude Box runs [Claude Code](https://docs.claude.com/claude-code) in a
sandboxed Docker container, and gives you a native desktop GUI for managing
those sandboxes: seeing what's running, creating and reopening conversations,
and interacting with them in an embedded terminal.

Each sandbox is a disposable container built from a single `Dockerfile`. The
GUI mounts your project directory into the container at the identical path,
runs Claude Code with `--dangerously-skip-permissions` by default, and keeps
the container alive with `docker run --rm` until you close it, so nothing
lingers on disk beyond the container's `--rm` lifecycle and your project
files.

## Features

- **Dashboard**: a filterable table of every `claude-agent-*` container, with
  live status, CPU and memory stats, and a details panel for the selected box.
- **Embedded terminal**: attach to a running box in a tab and interact with
  Claude Code directly, backed by a real VT100/xterm terminal emulator
  ([libvterm](https://www.leonerd.org.uk/code/libvterm/)).
- **Conversation management**: start a new conversation, resume a known one,
  adopt a conversation that Claude Code already created outside the app, or
  fork an existing conversation into a new box.
- **Per-box configuration**: model, reasoning effort, permission bypass, port
  forwards, extra mounts, and SSH remotes, all editable per box.
- **Workspace folders**: point a box at a named subfolder of your project
  instead of the tree root, so several conversations can work side by side
  without colliding.
- **Cross-platform**: a native build for Linux, with a Windows port in
  progress (see [Windows support](#windows-support)).

## How it works

A box is a container started with `docker run -d -i -t --rm --name
claude-agent-<name> ...`. The GUI attaches to it with `docker attach`, wires
the attach process to a PTY, and renders that PTY in a terminal tab. Closing a
tab only detaches the local `docker attach` client — the container keeps
running until you explicitly close the box. Because of `--rm`, `docker ps` is
always the source of truth for whether a box is running: nothing else needs to
track container state across a restart or a crash.

The container mounts your project directory at the same path inside the
container as it has on the host, plus your `~/.claude` and `~/.claude.json`
files, so Claude Code's own session and settings data works the same way it
does outside the sandbox.

## Prerequisites

- Docker, with the daemon reachable from your user account.
- Qt 6 and `libvterm` development packages, to build the GUI.

## Build and run

```bash
sudo apt install qt6-base-dev cmake build-essential libvterm-dev pkg-config
cd gui
cmake -B build
cmake --build build
./build/claude-box-gui
```

The first run walks you through a setup wizard that builds the sandbox image
from the repository's `Dockerfile`.

### Install a desktop entry

To add Claude Box to your application launcher:

```bash
cmake --build build --target desktop-install
```

This installs a `.desktop` file and icon under
`~/.local/share/{applications,icons}`. Launch the app by name from your
desktop environment, rather than by double-clicking the built binary or the
`.desktop` file directly.

## Windows support

A native Windows build is in progress on the `main` branch. The GUI compiles
and runs against Docker Desktop's WSL2 backend, including container attach,
terminal rendering, and live stats over the Docker Engine API's named pipe.
End-to-end validation against the `claude-code` image built from this
repository's `Dockerfile` is still pending. See `CLAUDE.md` for implementation
details.

## Repository layout

- `Dockerfile`: builds the `claude-code` sandbox image.
- `gui/`: the C++/Qt6 desktop application.
- `CLAUDE.md`: detailed architecture and development notes for contributors.

## Development

For architecture details, platform-specific implementation notes, and
conventions for this codebase, see `CLAUDE.md`.
