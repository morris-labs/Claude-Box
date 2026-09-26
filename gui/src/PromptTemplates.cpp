#include "PromptTemplates.h"
#include <QSettings>

namespace {
const QString kWorkspaceKey = QStringLiteral("prompts/workspace");
const QString kPortHintKey  = QStringLiteral("prompts/portHint");
const QString kStarterKey   = QStringLiteral("prompts/starter");
} // namespace

QString PromptTemplates::defaultWorkspace()
{
    return QStringLiteral(
        "Your workspace for this conversation is the %1/ folder in this directory, and it "
        "already exists. Read this directory's CLAUDE.md for how a workspace here is set up, "
        "do that setup inside %1/, and work there rather than in the directory above it. The "
        "conversation was opened as \"%2\". Wait for details before changing anything.");
}

QString PromptTemplates::defaultPortHint()
{
    return QStringLiteral(
        "The following container ports are mapped to the host: %1. Bind any web "
        "server, API, or other network service the host needs to reach to one of "
        "these container-side ports.");
}

QString PromptTemplates::defaultStarter()
{
    return QString();
}

QString PromptTemplates::workspace()
{
    QSettings s;
    return s.value(kWorkspaceKey, defaultWorkspace()).toString();
}

QString PromptTemplates::portHint()
{
    QSettings s;
    return s.value(kPortHintKey, defaultPortHint()).toString();
}

QString PromptTemplates::starter()
{
    QSettings s;
    return s.value(kStarterKey, defaultStarter()).toString();
}

void PromptTemplates::setWorkspace(const QString &t)
{
    QSettings s;
    if (t == defaultWorkspace())
        s.remove(kWorkspaceKey);
    else
        s.setValue(kWorkspaceKey, t);
}

void PromptTemplates::setPortHint(const QString &t)
{
    QSettings s;
    if (t == defaultPortHint())
        s.remove(kPortHintKey);
    else
        s.setValue(kPortHintKey, t);
}

void PromptTemplates::setStarter(const QString &t)
{
    QSettings s;
    if (t.isEmpty() || t == defaultStarter())
        s.remove(kStarterKey);
    else
        s.setValue(kStarterKey, t);
}
