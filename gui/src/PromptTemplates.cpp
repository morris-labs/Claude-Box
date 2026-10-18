#include "PromptTemplates.h"
#include <QRegularExpression>
#include <QSettings>

namespace {
const QString kWorkspaceKey = QStringLiteral("prompts/workspace");
const QString kPortHintKey  = QStringLiteral("prompts/portHint");
const QString kStarterKey   = QStringLiteral("prompts/starter");
const QString kWorktreeKey  = QStringLiteral("prompts/worktree");
} // namespace

QString PromptTemplates::defaultWorkspace()
{
    return QStringLiteral(
        "Your workspace for this conversation is %3. It already exists.\n"
        "\n"
        "Before making any changes, read CLAUDE.md if it exists. Then read and maintain "
        "%5 (notes on the shared environment) and PLAN.md and STATUS.md in %4 (notes "
        "for this conversation only), creating any that do not exist based on the "
        "project and the conversation.\n"
        "\n"
        "Do your work inside %3 rather than in the directory above it.\n"
        "\n"
        "Use git for versioning. Create local branches and commit your work as you go. "
        "Do not push to any remote without explicit instruction.\n"
        "\n"
        "This container is a sandbox. Install any missing build tools freely without "
        "asking. Any files that must survive a container rebuild must be saved inside "
        "the project directory.\n"
        "\n"
        "When you stop work or are asked to checkpoint, update %4/STATUS.md with: the "
        "active plan, your current step, what you have done since the last checkpoint, "
        "what comes next, and any blockers. Keep STATUS.md under 60 lines.\n"
        "\n"
        "The conversation was opened as \"%2\". Wait for details before changing anything.");
}

QString PromptTemplates::defaultWorktree()
{
    return QStringLiteral(
        "Your workspace for this conversation is %3, a git worktree of %6 on the branch %7. "
        "It already exists.\n"
        "\n"
        "Before making any changes, read CLAUDE.md if it exists. Then read and maintain "
        "%5 (notes on the shared environment) and PLAN.md and STATUS.md in %4 (notes "
        "for this conversation only; git ignores that folder), creating any that do not "
        "exist based on the project and the conversation.\n"
        "\n"
        "Do your work inside %3 and commit it to %7. Do not switch branches. The main "
        "checkout %6 is the reference copy and a local cache: read it freely, but do not "
        "edit files there. Other conversations work in sibling worktrees of the same "
        "repository at the same time. Keep every file for this work, code and notes, "
        "inside %3.\n"
        "\n"
        "Use git for versioning and commit your work as you go. Do not push to any remote "
        "and do not merge into other branches without explicit instruction.\n"
        "\n"
        "This container is a sandbox. Install any missing build tools freely without "
        "asking. Any files that must survive a container rebuild must be saved inside "
        "the project directory.\n"
        "\n"
        "When you stop work or are asked to checkpoint, update %4/STATUS.md with: the "
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
        "Your working directory is %2. Before doing anything else, read CLAUDE.md if it exists, "
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

QString PromptTemplates::worktree()
{
    QSettings s;
    return s.value(kWorktreeKey, defaultWorktree()).toString();
}

void PromptTemplates::setWorktree(const QString &t)
{
    QSettings s;
    if (t.isEmpty() || t == defaultWorktree())
        s.remove(kWorktreeKey);
    else
        s.setValue(kWorktreeKey, t);
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

QString PromptTemplates::expand(const QString &tmpl, const QStringList &values)
{
    static const QRegularExpression marker(QStringLiteral("%(\\d+)"));
    QString out;
    qsizetype last = 0;
    auto it = marker.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int n = m.captured(1).toInt();
        if (n < 1 || n > values.size())
            continue; // leave an unknown marker as written
        out += QStringView(tmpl).mid(last, m.capturedStart() - last);
        out += values.at(n - 1);
        last = m.capturedEnd();
    }
    out += QStringView(tmpl).mid(last);
    return out;
}

QString PromptTemplates::displayPath(const QString &containerPath, const QString &containerHome)
{
    if (containerPath == containerHome)
        return QStringLiteral("~");
    if (containerPath.startsWith(containerHome + QLatin1Char('/')))
        return QStringLiteral("~") + containerPath.mid(containerHome.size());
    return containerPath;
}
