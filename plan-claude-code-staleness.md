# Plan: Claude Code staleness notification

Branch: `feature/claude-code-staleness`

## Goal

When the `claude-code` Docker image contains a version of `@anthropic-ai/claude-code`
that is behind the latest release on npm, show a non-intrusive info bar prompting
the user to rebuild the image. The check happens in the background; no dialog, no
blocking, no crash on network failure.

## Two-part problem

**Part A -- know what version is in the image.**
The image must advertise its claude-code version as a Docker label so the app
can read it via the Docker API without running a container. The label is stamped
at image build time from the npm package's actual installed version.

**Part B -- know what the latest version is.**
Query the npm registry (`https://registry.npmjs.org/@anthropic-ai/claude-code/latest`)
for the `version` field. Same Qt6::Network stack as the app-self-update branch.
The two checkers can share a `QNetworkAccessManager` instance held by `MainWindow`.

## Implementation steps

### Step 1 -- Stamp the image with a label (`Dockerfile`)

After the `npm install -g @anthropic-ai/claude-code` line, add:

```dockerfile
# Record the installed claude-code version as an image label so the GUI
# can compare it against the npm registry without spawning a container.
RUN npm list -g --depth=0 --json @anthropic-ai/claude-code \
    | python3 -c "import sys,json; d=json.load(sys.stdin); \
      print(next(iter(d['dependencies'].values()))['version'])" \
    > /tmp/cc_version.txt
LABEL claude-box.claude-code-version=$(cat /tmp/cc_version.txt)
```

`python3` is already present (Ubuntu base). This produces a label such as
`claude-box.claude-code-version=1.0.33` baked into the image at build time.

### Step 2 -- Read the label from the image (`DockerApi` / `DockerBackend`)

Add `DockerApi::imageLabel(const QString &image, const QString &label, QString *err) -> QString`.

Uses the existing `GET /v1.44/images/claude-code/json` call (SetupWizard already
does an availability check against this endpoint). Parse `Config.Labels` from the
JSON response and return the named label's value, or empty string if absent or
the image doesn't exist.

Add `DockerBackend::claudeCodeImageVersion() -> QString` as a thin wrapper that
calls `imageLabel("claude-code", "claude-box.claude-code-version")`.

### Step 3 -- `ClaudeCodeUpdateChecker` class (`gui/src/ClaudeCodeUpdateChecker.{h,cpp}`)

A `QObject` that:
- Exposes `checkInBackground()`.
- Calls `DockerBackend::claudeCodeImageVersion()` to get the installed version.
  If empty (image not yet built or pre-label image), emits nothing -- silent skip.
- Fires a GET to `https://registry.npmjs.org/@anthropic-ai/claude-code/latest`
  with `User-Agent: claude-box-gui/<appVersion>`.
- Parses `version` from the JSON response.
- Compares with `QVersionNumber`; emits `updateAvailable(QString installed,
  QString latest)` if the registry version is newer.
- 24-hour cooldown in `QSettings` (`claudeCodeUpdateChecker/lastCheck`), same
  pattern as `UpdateChecker`.

### Step 4 -- Reuse `UpdateBar` (from app-self-update branch)

`UpdateBar` is general enough to serve both cases. For the claude-code case,
the message reads:
"Claude Code X.Y.Z is available (image has A.B.C). [Rebuild image]"

"[Rebuild image]" calls `MainWindow::onSetup()` (which opens the Setup wizard,
whose Build Image button does the rebuild). Alternatively it calls `SetupWizard`
directly on the image check row -- design the hook once the bar wires are in place.

If `UpdateBar` is not yet merged from the app-self-update branch, implement
a minimal version here and mark it for reconciliation at merge time.

### Step 5 -- Wire into `MainWindow`

- Create `ClaudeCodeUpdateChecker`, pass it the shared `QNetworkAccessManager`.
- Connect `updateAvailable` → `UpdateBar` (second instance, or one bar that
  handles both messages with a priority queue if both fire; simplest is two
  separate bar widgets stacked).
- Call `checkInBackground()` on the same 3 s delay as the app version check.
- Add a **Help > Check for Claude Code update** menu action (no cooldown,
  status-bar "Claude Code is up to date." if already current).

### Step 6 -- Handle pre-label images gracefully

Images built before this label existed have no `claude-box.claude-code-version`
label. In that case `claudeCodeImageVersion()` returns an empty string.
`ClaudeCodeUpdateChecker` skips the npm check entirely when the installed version
is unknown -- it can't compute a delta. The Setup wizard's image-rebuild flow
already exists; the user can trigger it manually. No extra nag.

## Out of scope

- Rebuilding the image automatically. The rebuild takes minutes and may need
  network access to pull the base image; it must be user-initiated.
- Checking the version inside *running* containers. Running containers were
  started from the image; the label is the ground truth for what's installed.
- Per-container claude-code version tracking. All containers use the single
  `claude-code` image, so one label covers all.

## Files to create / modify

| File | Action |
|---|---|
| `Dockerfile` | Add `RUN`+`LABEL` to stamp claude-code version |
| `gui/src/DockerApi.h` | Add `imageLabel()` declaration |
| `gui/src/DockerApi.cpp` | Implement `imageLabel()` |
| `gui/src/DockerBackend.h` | Add `claudeCodeImageVersion()` declaration |
| `gui/src/DockerBackend.cpp` | Implement wrapper |
| `gui/src/ClaudeCodeUpdateChecker.h` | Create |
| `gui/src/ClaudeCodeUpdateChecker.cpp` | Create |
| `gui/CMakeLists.txt` | Add new source files |
| `gui/src/UpdateBar.h` / `UpdateBar.cpp` | Create (or reconcile from other branch) |
| `gui/src/MainWindow.h` | Add `ClaudeCodeUpdateChecker*`, second `UpdateBar*` member |
| `gui/src/MainWindow.cpp` | Wire construction, Help menu action |

## Merge note

Both branches add `UpdateBar` and wire `MainWindow`. Merge the app-self-update
branch first; this branch's `UpdateBar` additions are additive (second instance,
second help action). The `QNetworkAccessManager` should be a single instance
owned by `MainWindow` and passed to both checkers.
