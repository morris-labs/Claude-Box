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
        "Your workspace for this conversation is the %1/ folder. It already exists.\n"
        "\n"
        "Before making any changes, read CLAUDE.md if it exists, then read and maintain "
        "INFRA.md, PLAN.md, and %1/STATUS.md — creating any that do not exist based on "
        "the project and the conversation.\n"
        "\n"
        "Do your work inside %1/ rather than in the directory above it.\n"
        "\n"
        "Use git for versioning. Create local branches and commit your work as you go. "
        "Do not push to any remote without explicit instruction.\n"
        "\n"
        "This container is a sandbox. Install any missing build tools freely without "
        "asking. Any files that must survive a container rebuild must be saved inside "
        "the project directory.\n"
        "\n"
        "When you stop work or are asked to checkpoint, update %1/STATUS.md with: the "
        "active plan, your current step, what you have done since the last checkpoint, "
        "what comes next, and any blockers. Keep STATUS.md under 60 lines.\n"
        "\n"
        "The conversation was opened as \"%2\". Wait for details before changing anything.");
}

QString PromptTemplates::defaultPortHint()
{
    return QStringLiteral(
        "The following container ports are mapped to the host: %1. Bind any web "
        "server, API, or other network service that the host needs to reach to one of "
        "these container-side ports.");
}

QString PromptTemplates::defaultStarter()
{
    return QStringLiteral(
        "You are working in %2. Before doing anything else, read CLAUDE.md if it exists, "
        "then read and maintain INFRA.md, PLAN.md, and STATUS.md — creating any that do "
        "not exist based on the project and the conversation.\n"
        "\n"
        "Use git for versioning. Create local branches and commit your work as you go. "
        "Do not push to any remote without explicit instruction.\n"
        "\n"
        "This container is a sandbox. Install any missing build tools freely without "
        "asking. Any files that must survive a container rebuild must be saved inside %2.\n"
        "\n"
        "When you stop work or are asked to checkpoint, update STATUS.md with: the active "
        "plan, your current step, what you have done since the last checkpoint, what comes "
        "next, and any blockers. Keep STATUS.md under 60 lines.");
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
