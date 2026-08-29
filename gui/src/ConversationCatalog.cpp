#include "ConversationCatalog.h"

#include "BoxRecord.h"
#include "ContainerPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

// The transcripts are newline-delimited compact JSON, one record per
// line, and the interesting records are a handful out of hundreds. Every
// line is therefore byte-matched for a marker first and only handed to
// QJsonDocument if it matches -- parsing all 1000+ lines of a multi-MB
// transcript to pull out a title would make the dialog visibly stall.
bool mentions(const QByteArray &line, const char *marker)
{
    return line.contains(marker);
}

QString textOfUserMessage(const QJsonObject &obj)
{
    // Skip the entries that aren't something the user typed: subagent
    // traffic (isSidechain), injected context (isMeta), and tool results
    // (content arrays whose parts are all tool_result).
    if (obj.value("isSidechain").toBool() || obj.value("isMeta").toBool())
        return QString();

    const QJsonValue content = obj.value("message").toObject().value("content");
    if (content.isString())
        return content.toString();

    if (content.isArray()) {
        for (const QJsonValue &part : content.toArray()) {
            const QJsonObject partObj = part.toObject();
            if (partObj.value("type").toString() == "text")
                return partObj.value("text").toString();
        }
    }
    return QString();
}

// Slash commands arrive as an XML-ish blob ("<command-name>/model</...>")
// and local command output as <local-command-stdout>. Neither makes a
// usable title, so they're passed over when looking for the opening
// prompt.
bool isUsableAsTitle(const QString &text)
{
    return !text.isEmpty() && !text.startsWith('<');
}

// Conversations this app provisioned for an issue open with a generated
// prompt ("Issue: <name>. Your workspace folder <slug>/ already exists,
// ..."). When such a transcript has no title record to fall back on, the
// issue name is the only part of that worth showing.
QString openingPromptTitle(const QString &prompt)
{
    if (prompt.startsWith("Issue: ")) {
        const int cut = prompt.indexOf(". Your workspace folder ");
        if (cut > 0)
            return prompt.mid(7, cut - 7);
    }
    return prompt;
}

QString condense(QString text, int maxChars)
{
    text.replace('\n', ' ');
    text = text.simplified();
    if (text.size() > maxChars)
        text = text.left(maxChars - 1).trimmed() + QChar(0x2026); // ellipsis
    return text;
}

} // namespace

QString ConversationCatalog::projectDirFor(const QString &targetDir)
{
    if (targetDir.isEmpty())
        return QString();

    // Claude Code names this directory after whatever `cwd` it sees --
    // which is the path *inside* the container, not the host path that
    // was mounted there. Those are the same string on Linux (hence no
    // visible difference before ContainerPaths existed), but on Windows a
    // host path like C:\Users\... is never what Claude Code's own process
    // reports as its cwd, since it never runs there directly.
    QString encoded = ContainerPaths::hostToContainer(targetDir);
    for (QChar &c : encoded) {
        // ASCII-only on purpose, matching the sanitizers in
        // DockerBackend.cpp: this has to byte-match what Claude Code
        // itself wrote, not what Qt's unicode tables consider a letter.
        const char16_t u = c.unicode();
        const bool alnum = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
        if (!alnum)
            c = QChar('-');
    }
    return QDir::homePath() + "/.claude/projects/" + encoded;
}

QList<ConversationInfo> ConversationCatalog::forDirectory(const QString &targetDir)
{
    QList<ConversationInfo> result;

    const QString projectDir = projectDirFor(targetDir);
    if (projectDir.isEmpty() || !QDir(projectDir).exists())
        return result;

    // uuid -> container name, so conversations an existing box already
    // owns can be labeled instead of silently offered twice.
    QHash<QString, QString> trackedBy;
    for (const BoxRecord &rec : BoxRecord::loadAll()) {
        if (!rec.sessionUuid.isEmpty())
            trackedBy.insert(rec.sessionUuid, rec.name);
    }

    const QFileInfoList files = QDir(projectDir).entryInfoList({"*.jsonl"}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;

        ConversationInfo info;
        info.sessionUuid = fi.completeBaseName();
        info.lastActive = fi.lastModified();

        QString customTitle;   // `claude --name`, i.e. what the user called it
        QString aiTitle;       // Claude's own generated title
        QString openingPrompt; // last resort
        int openingAttempts = 0;

        while (!f.atEnd()) {
            const QByteArray line = f.readLine();

            // Both title records are rewritten as a conversation moves on
            // (a new line is appended each time), so the last one of each
            // wins -- these deliberately keep overwriting.
            if (mentions(line, "\"custom-title\"")) {
                const QJsonObject obj = QJsonDocument::fromJson(line).object();
                if (obj.value("type").toString() == "custom-title")
                    customTitle = obj.value("customTitle").toString();
                continue;
            }

            if (mentions(line, "\"ai-title\"")) {
                const QJsonObject obj = QJsonDocument::fromJson(line).object();
                if (obj.value("type").toString() == "ai-title")
                    aiTitle = obj.value("aiTitle").toString();
                continue;
            }

            if (mentions(line, "\"last-prompt\"")) {
                const QJsonObject obj = QJsonDocument::fromJson(line).object();
                if (obj.value("type").toString() == "last-prompt")
                    info.lastPrompt = obj.value("lastPrompt").toString();
                continue;
            }

            // Turn counting stays at the byte level. A busy transcript is
            // tens of MB of mostly user/assistant lines, and running each
            // one through QJsonDocument just to increment a counter costs
            // half a second per directory -- the two markers together are
            // specific enough for a count that's only ever shown as a
            // rough "how much is in here" hint.
            if (!mentions(line, "\"type\":\"user\"") || !mentions(line, "\"message\":{\"role\":\"user\""))
                continue;
            if (mentions(line, "\"isSidechain\":true") || mentions(line, "\"isMeta\":true")
                || mentions(line, "\"tool_result\""))
                continue;

            ++info.userTurns;

            // The opening prompt is only a fallback title, so parse at
            // most a handful of lines looking for one.
            if (openingPrompt.isEmpty() && openingAttempts < 20) {
                ++openingAttempts;
                const QString text = textOfUserMessage(QJsonDocument::fromJson(line).object());
                if (isUsableAsTitle(text))
                    openingPrompt = text;
            }
        }

        // A transcript with nothing the user said is a stub, not a
        // conversation worth resuming.
        if (info.userTurns == 0)
            continue;

        info.title = !customTitle.isEmpty() ? customTitle
                   : !aiTitle.isEmpty()     ? aiTitle
                                            : condense(openingPromptTitle(openingPrompt), 70);
        if (info.title.isEmpty())
            info.title = info.sessionUuid.left(8);
        info.lastPrompt = condense(info.lastPrompt, 160);
        info.trackedBy = trackedBy.value(info.sessionUuid);

        result.append(info);
    }

    std::sort(result.begin(), result.end(), [](const ConversationInfo &a, const ConversationInfo &b) {
        return a.lastActive > b.lastActive;
    });
    return result;
}

QString ConversationCatalog::relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return QStringLiteral("unknown");

    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QStringLiteral("just now");
    if (secs < 3600)
        return QStringLiteral("%1m ago").arg(secs / 60);
    if (secs < 86400)
        return QStringLiteral("%1h ago").arg(secs / 3600);
    if (secs < 7 * 86400)
        return QStringLiteral("%1d ago").arg(secs / 86400);
    return when.toString(QStringLiteral("yyyy-MM-dd"));
}
