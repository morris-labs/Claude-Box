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
    // running, which just yields an empty list. IDLE restarts the countdown
    // whenever a session is alive, so the container lingers for GRACE seconds
    // after its last session ends: a conversation restarted right away finds
    // the container still there instead of racing its shutdown.
    return QStringLiteral(
        "STATE=\"$1\"; GRACE=\"$2\"; IDLE=$SECONDS\n"
        "while :; do\n"
        "  tmux list-sessions -F '#S' > \"$STATE/sessions.tmp\" 2>/dev/null\n"
        "  mv -f \"$STATE/sessions.tmp\" \"$STATE/sessions\"\n"
        "  if [ -s \"$STATE/sessions\" ]; then IDLE=$SECONDS\n"
        "  elif [ $((SECONDS-IDLE)) -gt \"$GRACE\" ]; then break; fi\n"
        "  sleep 1\n"
        "done\n");
}

QString sessionStartScript()
{
    // $3 is the whole command as ONE shell-quoted string: tmux joins several
    // arguments with spaces and re-runs them through the shell, which would
    // re-split any multi-word argument (a conversation name, an opening
    // prompt). A single string is handed to the shell as it is.
    return QStringLiteral(
        "S=\"$1\"; D=\"$2\"; CMD=\"$3\"\n"
        "tmux has-session -t \"=$S\" 2>/dev/null && { echo \"session $S is already running\" >&2; exit 3; }\n"
        "tmux new-session -d -s \"$S\" -c \"$D\" -- \"$CMD\" || exit 1\n"
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

QStringList allLiveSessions()
{
    QStringList all;
    const QDir states(QDir::homePath() + QStringLiteral("/.claude-box/state"));
    for (const QString &container : states.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        all += liveSessions(container);
    return all;
}

} // namespace SharedContainer
