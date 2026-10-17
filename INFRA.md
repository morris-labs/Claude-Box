# Infrastructure

## Environment

- Runs in the `claude-code` sandbox container as user `prime`. Docker and a
  display are not available here, so the GUI builds but does not run end to
  end.
- Mapped host ports: 20165 to 20169. This work needs none of them.

## Build tools

Installed in the container with `apt`: `qt6-base-dev`, `libvterm-dev`,
`cmake`, `g++`, `pkg-config`. These do not survive a container rebuild.
Reinstall them with:

```bash
sudo apt-get install -y qt6-base-dev libvterm-dev cmake build-essential pkg-config
```

## Build and check

Build out of tree so the checkout stays clean:

```bash
cd gui
cmake -S . -B /tmp/claude-1000/build-chk
cmake --build /tmp/claude-1000/build-chk -j8
```

## Repository

- Remote `origin` is GitHub (`morris-labs/Claude-Box`). Do not push without
  explicit instruction.
- Work branches: `fix/link-existing-conversations`, then `feature/projects-1-project-layer`
  and `feature/projects-2-worktrees` stacked on it. Each contains the one before.
