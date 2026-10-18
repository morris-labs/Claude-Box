#pragma once

#include <QString>
#include <QStringList>

// The model IDs offered in the New Box and Edit Box dialogs.
//
// There is no list to download without an API key, so the list comes from the
// one place that always matches what the boxes run: the Claude Code inside the
// `claude-code` image. Its program file contains every model ID it knows, and
// scanning it takes a fraction of a second. The result is cached against the
// image's ID, so rebuilding the image (which is how Claude Code is updated
// here) refreshes the list the next time the app starts.
//
// known() merges three sources, newest first: that scan (or, before the first
// scan has finished, a built-in baseline), and the model options Claude Code
// has recorded for the signed-in account in ~/.claude.json.
namespace ModelCatalog {

// The models in the dialogs, grouped by family and newest first, no duplicates.
QStringList known();

// Rescans the image if its ID differs from the cached scan, or the cache is
// older than 30 days. Runs the scan on a worker thread and returns at once;
// `done` is not provided because the dialogs read known() when they open.
void refreshIfStale();

// Turns the output of the scan (one match per line, possibly repeated) into
// the list to show: current models only, newest first.
QStringList parseScan(const QString &scanOutput);

// The shell command run in the image that prints candidate IDs.
QString scanCommand();

} // namespace ModelCatalog
