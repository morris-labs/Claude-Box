# Plan: Claude Code staleness notification

Branch: `feature/claude-code-staleness`

## Status: implemented

This feature is complete. The plan below reflects the final implementation.

## Goal

When the `claude-code` Docker image contains a version of `@anthropic-ai/claude-code`
that is behind the latest release on npm, show a non-intrusive info bar prompting
the user to rebuild the image.

## Implementation

**Dockerfile** stamps the installed version into `/etc/claude-code-version` at build time:

```dockerfile
RUN npm install -g @anthropic-ai/claude-code && \
    node -p "require('$(npm root -g)/@anthropic-ai/claude-code/package.json').version" \
    > /etc/claude-code-version
```

**DockerBackend::claudeCodeImageVersion()** reads the file from a one-shot
`docker run --rm --entrypoint "" claude-code cat /etc/claude-code-version`.
This is a blocking call; it must run on a background thread.

**ClaudeCodeUpdateChecker** runs the docker call off the GUI thread via
`QtConcurrent::run` + `QFutureWatcher`, caches the result in `QSettings`
for 24 hours, then queries the npm registry for the latest version.
An empty installed version (pre-stamp image) is treated as "out of date"
so the user is always prompted to rebuild.

**MainWindow** wires `updateAvailable` to show `UpdateBar` with a
"Rebuild image" action and `upToDate` to hide the bar after a successful rebuild.
`onSetupWizard()` calls `checkNow()` after the wizard completes so the bar
reflects the new image immediately.
