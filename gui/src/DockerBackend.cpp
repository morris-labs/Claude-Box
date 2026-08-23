#include "DockerBackend.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QUuid>

namespace {

// Mirrors the old claude-box.bash box-name sanitizer:
//   tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9_.-' '-'
// ASCII-only on purpose, to match tr's behavior exactly rather than
// QChar's unicode-aware classification.
QString sanitizeBoxBase(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar &qc : raw.toLower()) {
        const char c = qc.unicode() < 128 ? qc.toLatin1() : 0;
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                        || c == '_' || c == '.' || c == '-';
        out += keep ? qc : QChar('-');
    }
    return out;
}

// Mirrors the old issue-name slugifier:
//   tr -cs '[:alnum:]' '_' | sed 's/^_*//; s/_*$//'
QString slugifyIssueName(const QString &issueName)
{
    QString squeezed;
    squeezed.reserve(issueName.size());
    bool lastWasUnderscore = false;
    for (const QChar &c : issueName) {
        const bool alnum = c.unicode() < 128 && c.isLetterOrNumber();
        if (alnum) {
            squeezed += c;
            lastWasUnderscore = false;
        } else if (!lastWasUnderscore) {
            squeezed += '_';
            lastWasUnderscore = true;
        }
    }
    while (squeezed.startsWith('_'))
        squeezed.remove(0, 1);
    while (squeezed.endsWith('_'))
        squeezed.chop(1);
    return squeezed;
}

} // namespace

bool DockerBackend::runDocker(const QStringList &args, QString *stdoutOut, QString *errorOut, int timeoutMs) const
{
    QProcess proc;
    proc.start("docker", args);
    if (!proc.waitForStarted(5000)) {
        if (errorOut)
            *errorOut = "failed to start docker (is it on PATH?)";
        return false;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        if (errorOut)
            *errorOut = "docker command timed out: docker " + args.join(' ');
        return false;
    }
    if (stdoutOut)
        *stdoutOut = QString::fromUtf8(proc.readAllStandardOutput());
    if (proc.exitCode() != 0) {
        if (errorOut) {
            const QString stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            *errorOut = stderrText.isEmpty()
                ? ("docker " + args.join(' ') + " exited " + QString::number(proc.exitCode()))
                : stderrText;
        }
        return false;
    }
    return true;
}

QString DockerBackend::uniqueBoxName(const QString &base) const
{
    const QString sanitizedBase = sanitizeBoxBase(base);
    QString candidate = "claude-agent-" + sanitizedBase;

    QString out, err;
    runDocker({"ps", "-a", "--format", "{{.Names}}"}, &out, &err, 5000);
    const QStringList existing = out.split('\n', Qt::SkipEmptyParts);

    int n = 1;
    while (existing.contains(candidate)) {
        ++n;
        candidate = QString("claude-agent-%1-%2").arg(sanitizedBase).arg(n);
    }
    return candidate;
}

QList<BoxInfo> DockerBackend::listBoxes() const
{
    QList<BoxInfo> result;
    QSet<QString> seen;
    QString out, err;

    // Running.
    if (runDocker({"ps", "--filter", "name=^claude-agent-", "--format", "{{.Names}}\t{{.Status}}"},
                   &out, &err, 5000)) {
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split('\t');
            if (parts.size() < 2)
                continue;

            BoxInfo info;
            info.status = BoxInfo::Status::Running;
            info.name = parts.at(0);
            info.detail = parts.at(1);

            const BoxRecord rec = BoxRecord::load(info.name);
            if (rec.isValid()) {
                info.conversationName = rec.conversationName;
                info.targetDir = rec.targetDir;
            } else {
                info.conversationName = info.name;
            }

            QString statOut, statErr;
            if (runDocker({"stats", "--no-stream", "--format", "{{.CPUPerc}}\t{{.MemUsage}}", info.name},
                           &statOut, &statErr, 5000)) {
                const QStringList statParts = statOut.trimmed().split('\t');
                if (statParts.size() == 2)
                    info.detail += QString(" · cpu %1 · mem %2").arg(statParts.at(0), statParts.at(1));
            }

            result.append(info);
            seen.insert(info.name);
        }
    }

    // Stopped but not yet removed -- rare, since containers run with
    // --rm, but can happen if dockerd restarted mid-cleanup.
    if (runDocker({"ps", "-a", "-s", "--filter", "name=^claude-agent-", "--filter", "status=exited",
                    "--format", "{{.Names}}\t{{.Status}}\t{{.Size}}"},
                   &out, &err, 5000)) {
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split('\t');
            if (parts.size() < 3 || seen.contains(parts.at(0)))
                continue;

            BoxInfo info;
            info.status = BoxInfo::Status::Stopped;
            info.name = parts.at(0);
            info.detail = parts.at(1) + " · " + parts.at(2);
            result.append(info);
            seen.insert(info.name);
        }
    }

    // Known (reopenable): every tracked record not already accounted for.
    const QList<BoxRecord> known = BoxRecord::loadAll();
    for (const BoxRecord &rec : known) {
        if (seen.contains(rec.name))
            continue;

        BoxInfo info;
        info.status = BoxInfo::Status::Known;
        info.name = rec.name;
        info.conversationName = rec.conversationName;
        info.targetDir = rec.targetDir;
        info.detail = "not running";
        result.append(info);
        seen.insert(rec.name);
    }

    return result;
}

bool DockerBackend::isRunning(const QString &name) const
{
    QString out, err;
    if (!runDocker({"ps", "--filter", "name=^claude-agent-", "--format", "{{.Names}}"}, &out, &err, 5000))
        return false;
    return out.split('\n', Qt::SkipEmptyParts).contains(name);
}

bool DockerBackend::runContainer(const BoxRecord &rec, const QStringList &claudeArgs, QString *errorOut) const
{
    QString gitSetup = "git config --global --add safe.directory \"$1\"";

    QStringList args;
    args << "run" << "-d" << "-t" << "--rm"
         << "--name" << rec.name
         << "--user" << "user"
         << "-e" << "IS_SANDBOX=1"
         << "-w" << rec.targetDir
         << "-v" << (rec.targetDir + ":" + rec.targetDir)
         << "-v" << (QDir::homePath() + "/.claude:/home/user/.claude")
         << "-v" << (QDir::homePath() + "/.claude.json:/home/user/.claude.json");

    const QString gitconfigPath = rec.targetDir + "/gitconfig";
    if (QFileInfo::exists(gitconfigPath)) {
        args << "-e" << ("GIT_CONFIG_GLOBAL=" + gitconfigPath);
        gitSetup = "true"; // that file already sets safe.directory = *
    }

    const QString agentEnvPath = rec.targetDir + "/agent.env";
    if (QFileInfo::exists(agentEnvPath))
        args << "--env-file" << agentEnvPath;

    for (const QString &p : rec.ports)
        args << "-p" << p;

    for (const QString &d : rec.dirs) {
        const int colon = d.indexOf(':');
        const QString hostRaw = colon < 0 ? d : d.left(colon);
        const QString containerPath = colon < 0 ? d : d.mid(colon + 1);
        const QString hostPath = QFileInfo(hostRaw).absoluteFilePath();
        args << "-v" << (hostPath + ":" + containerPath);
    }

    args << "claude-code" << "bash" << "-c" << (gitSetup + " && shift && exec claude \"$@\"")
         << "_" << rec.targetDir;
    args += claudeArgs;

    return runDocker(args, nullptr, errorOut, 30000);
}

bool DockerBackend::createNew(BoxRecord &rec, QString *errorOut) const
{
    QString slug;
    QString openingPrompt;
    const bool provisionIssue = !rec.conversationName.isEmpty()
        && QFileInfo(rec.targetDir + "/new-issue.sh").isExecutable();

    if (provisionIssue) {
        slug = slugifyIssueName(rec.conversationName);
        if (slug.isEmpty()) {
            if (errorOut)
                *errorOut = "conversation name has no usable characters";
            return false;
        }

        const QString issueDir = rec.targetDir + "/" + slug;
        if (!QDir(issueDir).exists()) {
            QProcess p;
            p.setWorkingDirectory(rec.targetDir);
            p.start(rec.targetDir + "/new-issue.sh", {rec.conversationName});
            if (!p.waitForFinished(30000) || p.exitCode() != 0) {
                if (errorOut) {
                    const QString stderrText = QString::fromUtf8(p.readAllStandardError()).trimmed();
                    *errorOut = "new-issue.sh failed" + (stderrText.isEmpty() ? QString() : (": " + stderrText));
                }
                return false;
            }
        }

        openingPrompt = QString(
            "Issue: %1. Your workspace folder %2/ already exists, so do not run "
            "new-issue.sh. Read %2/CLAUDE.md and work there. Wait for the ticket "
            "details before changing anything.").arg(rec.conversationName, slug);
    }

    const QString base = slug.isEmpty() ? QFileInfo(rec.targetDir).fileName() : slug;
    rec.name = uniqueBoxName(base);
    rec.sessionUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (rec.conversationName.isEmpty())
        rec.conversationName = rec.name;

    QStringList claudeArgs;
    if (rec.rc)
        claudeArgs << "--remote-control";
    if (rec.yolo)
        claudeArgs << "--dangerously-skip-permissions";
    if (provisionIssue)
        claudeArgs << "--name" << rec.conversationName << openingPrompt;
    claudeArgs << "--session-id" << rec.sessionUuid;

    if (!runContainer(rec, claudeArgs, errorOut))
        return false;

    rec.save();
    return true;
}

bool DockerBackend::reopen(const BoxRecord &rec, QString *errorOut) const
{
    QStringList claudeArgs;
    if (rec.rc)
        claudeArgs << "--remote-control";
    if (rec.yolo)
        claudeArgs << "--dangerously-skip-permissions";
    claudeArgs << "--resume" << rec.sessionUuid;

    return runContainer(rec, claudeArgs, errorOut);
}

bool DockerBackend::stop(const QString &name, QString *errorOut) const
{
    return runDocker({"stop", name}, nullptr, errorOut, 15000);
}

bool DockerBackend::remove(const QString &name, QString *errorOut) const
{
    return runDocker({"rm", name}, nullptr, errorOut, 5000);
}
