# Mac release plan

Port `gui/` to macOS so it builds, runs, and launches as a proper `.app` bundle.
The Linux codebase is the reference; the Windows port (phases 1-4 in `main`) is the
prior art for structure. Cross-platform parity is a standing requirement: every
shared-code change must be verified clean on Linux too before the step is done.

## Phase 1 — Build fixes (make it compile and run)

**Step 1.1 — `PtySessionUnix.cpp` header**
macOS ships `forkpty()` in `<util.h>`; Linux uses `<pty.h>`. Add a
`Q_OS_DARWIN` guard on the include so the same file compiles on both.

**Step 1.2 — `DockerApi::socketPath()` macOS probe**
Docker Desktop for Mac creates `~/.docker/run/docker.sock`. The existing
`#else` branch returns `/var/run/docker.sock`, which may be absent or
require elevated permissions. Add a `Q_OS_DARWIN` branch that probes
`~/.docker/run/docker.sock` first, falls back to `/var/run/docker.sock`.

**Step 1.3 — CMakeLists: `.app` bundle**
Add `set_target_properties(MACOSX_BUNDLE TRUE)` and supply a minimal
`Info.plist.in` template under `packaging/`. Without this the build
produces a bare binary; macOS won't treat it as a launchable app.

**Step 1.4 — App icon for macOS**
`QIcon::fromTheme()` returns nothing on macOS (no XDG system). The
existing fallback in `main.cpp` reads an installed SVG path that won't
be inside the bundle. Generate an `icns` from the existing SVG via
`rsvg-convert` + `iconutil`, embed it in the bundle via
`MACOSX_BUNDLE_ICON_FILE`, and add a `Q_OS_DARWIN` fallback path in
`main.cpp` that reads the icon from `QCoreApplication::applicationDirPath()`.

## Phase 2 — macOS UX

**Step 2.1 — `onOpenExternal()` terminal launch**
The Linux `#else` branch tries `gnome-terminal`, `xterm`, etc. — none
exist on macOS. Add a `Q_OS_DARWIN` branch: try `$TERMINAL`, then
iTerm2, then `Terminal.app` via `open -a Terminal`.

**Step 2.2 — macOS menu bar smoke-test**
Qt auto-generates the app menu (Cmd+Q, Cmd+,) on macOS. No code change
expected; verify behavior on the test machine and document the result.

## Phase 3 — Packaging and distribution

**Step 3.1 — `macdeployqt` CMake target**
Add an `app-bundle` custom target that runs `macdeployqt` to copy Qt
frameworks into the `.app`, making it self-contained.

**Step 3.2 — Ad-hoc code signing**
`codesign --deep -s -` for local use and CI. Needed for Gatekeeper
to allow running the app at all on recent macOS.

**Step 3.3 — DMG for distribution**
`hdiutil create` target that packages the signed `.app` into a `.dmg`.
Notarization via `notarytool` is deferred until there is a Developer ID
certificate; document the command sequence for when that time comes.

## Out of scope for this release

- Mac App Store submission
- Universal binary (Intel + Apple Silicon) — build for the test
  machine's native arch first; a fat binary is a later CMake tweak
- Automated CI for macOS — depends on having a macOS runner
