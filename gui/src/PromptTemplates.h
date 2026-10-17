#pragma once
#include <QString>
#include <QStringList>

// Loads and saves the three prompt templates that DockerBackend sends to
// newly created boxes. Templates are stored in QSettings under the
// "prompts/" group and fall back to built-in defaults when no custom
// value has been saved.
//
// Placeholders are %N. The workspace, worktree and starter templates are filled
// in by expand(); the port hint is still filled in with QString::arg():
//   Workspace: %1 = folder slug, %2 = conversation name,
//              %3 = workspace folder path, %4 = this conversation's notes
//              directory, %5 = the shared INFRA.md path
//   Worktree:  a complete variant of the workspace prompt, used instead of it
//              for a box with its own worktree. %1 = folder slug,
//              %2 = conversation name, %3 = worktree path, %4 = notes
//              directory, %5 = shared INFRA.md path, %6 = repository path,
//              %7 = branch
//   Port hint: %1 = comma-separated mapped-port list
//   Starter:   %1 = conversation name, %2 = target directory path
// Paths are container-side and written with a leading ~ for the home
// directory (see displayPath()), so the agent gets a location it can use
// as is rather than a bare folder name.
class PromptTemplates {
public:
    static QString workspace();
    static QString portHint();
    static QString starter();
    static QString worktree();

    static void setWorkspace(const QString &t);
    static void setPortHint(const QString &t);
    static void setStarter(const QString &t);
    static void setWorktree(const QString &t);

    static QString defaultWorkspace();
    static QString defaultPortHint();
    static QString defaultStarter();
    static QString defaultWorktree();

    // Replaces each %N in `tmpl` with values[N-1] in a single pass. Unlike
    // QString::arg(a, b), a template that skips a number or uses only %2
    // still gets the right value in the right place, and a value that
    // itself contains "%2" is not expanded again. A %N with no matching
    // value is left as written.
    static QString expand(const QString &tmpl, const QStringList &values);

    // `containerPath` with the container user's home directory written as
    // "~", for example /home/prime/Bin/app -> ~/Bin/app.
    static QString displayPath(const QString &containerPath, const QString &containerHome);
};
