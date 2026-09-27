#pragma once
#include <QString>

// Loads and saves the three prompt templates that DockerBackend sends to
// newly created boxes. Templates are stored in QSettings under the
// "prompts/" group and fall back to built-in defaults when no custom
// value has been saved.
//
// Placeholders use Qt's %N arg() syntax:
//   Workspace: %1 = folder slug, %2 = conversation name
//   Port hint: %1 = comma-separated mapped-port list
//   Starter:   %1 = conversation name, %2 = target directory
class PromptTemplates {
public:
    static QString workspace();
    static QString portHint();
    static QString starter();

    static void setWorkspace(const QString &t);
    static void setPortHint(const QString &t);
    static void setStarter(const QString &t);

    static QString defaultWorkspace();
    static QString defaultPortHint();
    static QString defaultStarter();
};
