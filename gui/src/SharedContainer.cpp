#include "SharedContainer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace SharedContainer {

QString stateDir(const QString &container)
{
    return QDir::homePath() + QStringLiteral("/.claude-box/state/") + container;
}

QString stateFile(const QString &container)
{
    return stateDir(container) + QLatin1Char('/') + kStateFileName;
}

QString keeperScript()
{
    // The file is written to a temporary name and renamed, so the host never
    // reads a half-written list. `tmux list-sessions` fails when no server is
    // running, which just yields an empty list.
    return QStringLiteral(
        "STATE=\"$1\"; GRACE=\"$2\"; START=$SECONDS\n"
        "while :; do\n"
        "  tmux list-sessions -F '#S' > \"$STATE/sessions.tmp\" 2>/dev/null\n"
        "  mv -f \"$STATE/sessions.tmp\" \"$STATE/sessions\"\n"
        "  if [ ! -s \"$STATE/sessions\" ] && [ $((SECONDS-START)) -gt \"$GRACE\" ]; then break; fi\n"
        "  sleep 1\n"
        "done\n");
}

QString sessionStartScript()
{
    return QStringLiteral(
        "S=\"$1\"; D=\"$2\"; shift 2\n"
        "tmux has-session -t \"=$S\" 2>/dev/null && { echo \"session $S is already running\" >&2; exit 3; }\n"
        "tmux new-session -d -s \"$S\" -c \"$D\" -- \"$@\" || exit 1\n"
        "sleep 2\n"
        "tmux has-session -t \"=$S\" 2>/dev/null || { echo \"$S ended right after starting\" >&2; exit 2; }\n");
}

QStringList parseSessions(const QByteArray &fileContents)
{
    QStringList names;
    for (const QString &line : QString::fromUtf8(fileContents).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString name = line.trimmed();
        if (!name.isEmpty())
            names << name;
    }
    return names;
}

QStringList liveSessions(const QString &container)
{
    const QFileInfo fi(stateFile(container));
    if (!fi.exists())
        return {};
    if (fi.lastModified().secsTo(QDateTime::currentDateTime()) > kStateMaxAgeSec)
        return {};
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseSessions(f.readAll());
}

} // namespace SharedContainer
