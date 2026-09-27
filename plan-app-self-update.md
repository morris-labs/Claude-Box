# Plan: App self-update via GitHub releases

Branch: `feature/app-self-update`

## Goal

On startup (and via Help > Check for Updates), the app silently checks GitHub releases
for a newer version of claude-box-gui. If one exists, a non-intrusive info bar
appears below the menu bar with the new version and a link to the release page.
No auto-install: the user downloads and installs the appropriate package (.AppImage,
.deb, .rpm, .exe) themselves.

## What already exists

- Qt6::Network is already linked (`find_package Qt6 ... Network`, line 14 of CMakeLists.txt).
- `CLAUDE_BOX_VERSION` macro (set from `PROJECT_VERSION`) is available in C++ via
  `QApplication::applicationVersion()`.
- GitHub repo: `https://github.com/morris-labs/Claude-Box`.

## Implementation steps

### Step 1 -- `UpdateChecker` class (`gui/src/UpdateChecker.{h,cpp}`)

A lightweight `QObject` that:
- Accepts a `QNetworkAccessManager*` (shared with any other HTTP use) or owns one.
- Exposes `checkInBackground()`: fires a GET to
  `https://api.github.com/repos/morris-labs/Claude-Box/releases/latest`
  with `User-Agent: claude-box-gui/<version>` (required by GitHub API policy).
- Parses the JSON response: extracts `tag_name` (e.g. `v3.3.0`) and `html_url`.
- Compares against `QApplication::applicationVersion()` with `QVersionNumber`.
  Tags follow `vX.Y.Z`; strip the leading `v` before parsing.
- Emits `updateAvailable(QString newVersion, QUrl releaseUrl)` if the remote
  version is newer, nothing otherwise.
- Handles network errors and malformed responses silently (log to `qDebug`,
  never pop a dialog on a background check).
- Respects a 24-hour cooldown stored in `QSettings` (`updateChecker/lastCheck`)
  so repeated launches don't hammer GitHub.

### Step 2 -- `UpdateBar` widget (`gui/src/UpdateBar.{h,cpp}`)

A slim `QWidget` (styled to match `Theme`, distinct color so it reads as
informational, not an error) that:
- Shows: "Version X.Y.Z is available. [View release]"
- "[View release]" opens `releaseUrl` in the system browser via `QDesktopServices::openUrl`.
- Has a dismiss button (×) that hides the bar and records the dismissed version
  in `QSettings` (`updateChecker/dismissedVersion`) so it doesn't reappear for
  the same release.
- Is initially hidden; `show()` is called only when `UpdateChecker` emits
  `updateAvailable`.

### Step 3 -- Wire into `MainWindow`

- Add `UpdateBar` to the main layout, between the menu bar and the central widget.
  `QMainWindow` does not have a direct slot for this; use a wrapper `QWidget`
  as `centralWidget`, containing the bar plus the existing splitter in a
  `QVBoxLayout`, or insert into the existing layout above the table.
- In `MainWindow` constructor: create `UpdateChecker`, connect
  `updateAvailable` → `UpdateBar::show(version, url)`, call
  `checkInBackground()` after a short `QTimer::singleShot` delay (e.g., 3 s)
  so the first paint lands before the network request goes out.
- Add a **Help > Check for Updates** menu action that calls `checkInBackground()`
  without the cooldown, and if no update is found shows a brief status-bar message
  ("You're up to date.") rather than a dialog.

### Step 4 -- Tests / manual verification

No automated tests (network calls in tests are pain). Manual checklist:
- Build with a fake lower version number and confirm the bar appears.
- Dismiss the bar and relaunch -- confirm it does not reappear for the same version.
- Set the network to offline and confirm no dialog or crash.
- Run Help > Check for Updates while up to date and confirm status-bar message.

## Out of scope

- Auto-download or auto-install. Package format differences (AppImage, .deb, .rpm, .exe,
  .dmg) make silent install fragile; a link to the release page is the right boundary.
- macOS Sparkle or Windows MSI update plumbing. Those are follow-on work if needed.
- Checking pre-release tags (`prerelease: true` in the API response should be skipped
  unless we add an opt-in setting later).

## Files to create / modify

| File | Action |
|---|---|
| `gui/src/UpdateChecker.h` | Create |
| `gui/src/UpdateChecker.cpp` | Create |
| `gui/src/UpdateBar.h` | Create |
| `gui/src/UpdateBar.cpp` | Create |
| `gui/CMakeLists.txt` | Add the four new files to the source list |
| `gui/src/MainWindow.h` | Add `UpdateChecker*`, `UpdateBar*` members |
| `gui/src/MainWindow.cpp` | Wire construction, Help menu action |
