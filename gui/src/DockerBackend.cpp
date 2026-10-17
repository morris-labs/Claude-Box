#include "DockerBackend.h"

#include "ContainerPaths.h"
#include "ConversationCatalog.h"
#include "DockerApi.h"
#include "ProjectRecord.h"
#include "PromptTemplates.h"
#include "PortAllocator.h"
#include "SharedContainer.h"
#include "Worktrees.h"

#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>
#include <QUuid>

namespace {

// Shell-quote a single token for embedding in a bash -c string.
// Uses single-quoting, which is safe for any byte value (handles
// spaces, special chars, unicode). The only character that must be
// escaped inside single quotes is a single quote itself.
static QString shellQuote(const QString &s)
{
    return QLatin1Char('\'')
         + QString(s).replace(QLatin1Char('\''), QStringLiteral("'\\''"))
         + QLatin1Char('\'');
}

// Build the tmux container entrypoint command. claudeArgs are shell-quoted
// and embedded inline so tmux's re-shelling (it joins tokens after -- and
// runs $SHELL -c on the result) cannot word-split multi-word arguments.
//
// The keeper-loop pattern makes bash the container's PID 1 rather than the
// tmux client. Ctrl+B D in any attached terminal detaches that client
// (exits the docker-exec session) without killing PID 1, so the container
// stays alive. The loop exits when claude's tmux session ends (tmux's
// exit-empty is on by default), which is the normal shutdown path.
// A host path as the agent should be told about it: the container-side
// location (they differ on Windows), with the container user's home written
// as "~". A full path rather than a folder name, so the agent doesn't have
// to guess where it is relative to.
static QString promptPath(const QString &hostPath)
{
    return PromptTemplates::displayPath(ContainerPaths::hostToContainer(hostPath),
                                        QStringLiteral("/home/") + DockerBackend::containerUsername());
}

// Extra bind mounts needed when ~/.claude/projects is a symlink to somewhere
// outside ~/.claude (a transcript store shared between setups, say). Only
// ~/.claude itself is mounted, so inside the container that link would point
// at nothing, and `claude --resume` would answer "No conversation found" for
// a transcript that is sitting on the host. Binding the target at the same
// absolute path makes the link resolve. Empty when projects/ is not a
// symlink, or points inside ~/.claude.
static QStringList projectsSymlinkBinds()
{
    QStringList binds;
    const QFileInfo projectsInfo(QDir::homePath() + QStringLiteral("/.claude/projects"));
    if (!projectsInfo.isSymLink())
        return binds;
    const QString target = projectsInfo.symLinkTarget();
    if (target.isEmpty())
        return binds;

    // ~/.claude may itself be a symlink; compare against where it really is,
    // and fall back to the logical path when it does not exist.
    QString resolvedClaude = QFileInfo(QDir::homePath() + QStringLiteral("/.claude")).canonicalFilePath();
    if (resolvedClaude.isEmpty())
        resolvedClaude = QDir::homePath() + QStringLiteral("/.claude");
    if (target == resolvedClaude || target.startsWith(resolvedClaude + QLatin1Char('/')))
        return binds;

    binds << (target + QLatin1Char(':') + target);
    return binds;
}

// The per-box sessions directory, mounted over the container's
// ~/.claude/sessions. Claude Code keeps one <pid>.json there per running
// process, and every container numbers its pids from the same small values,
// so with a shared directory boxes overwrite each other's files and the
// result cannot say which box a file belongs to. A directory per box makes
// the owner certain. It is emptied at each launch so a file left by a
// container that was killed cannot be mistaken for the new process.
static QString prepareSessionsMount(const BoxRecord &rec, const QString &containerHome)
{
    // The mountpoint must already exist inside the mounted ~/.claude, or
    // docker creates it owned by root and the host's own claude cannot write
    // there.
    QDir().mkpath(QDir::homePath() + QStringLiteral("/.claude/sessions"));
    const QString dir = BoxRecord::sessionsDir(rec.name);
    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir))
        return QString();
    return dir + QLatin1Char(':') + containerHome + QStringLiteral("/.claude/sessions");
}

// The host directory the shared container's keeper writes its session list
// into, mounted at SharedContainer::kStateMount. A stale list from an earlier
// container of the same name is removed so it cannot pass for a live one.
static QString sharedStateBind(const QString &container)
{
    const QString dir = SharedContainer::stateDir(container);
    QDir().mkpath(dir);
    QFile::remove(SharedContainer::stateFile(container));
    return dir + QLatin1Char(':') + SharedContainer::kStateMount;
}

// The keeper command for a shared container: trust every directory for git
// (it is a sandbox, and worktrees and sibling repositories are all in it),
// then run the keeper. `bash -c SCRIPT _ STATE_DIR LINGER_SECONDS`.
static QStringList sharedKeeperCommand()
{
    return {QStringLiteral("bash"), QStringLiteral("-c"),
            QStringLiteral("git config --global --add safe.directory '*' 2>/dev/null; ")
                + SharedContainer::keeperScript(),
            QStringLiteral("_"), SharedContainer::kStateMount,
            // CLAUDE_BOX_SHARED_GRACE lets a test shorten the linger.
            QString::number(qEnvironmentVariableIsSet("CLAUDE_BOX_SHARED_GRACE")
                                ? qEnvironmentVariableIntValue("CLAUDE_BOX_SHARED_GRACE")
                                : SharedContainer::kStartupGraceSec)};
}

// What the container runs to let git work in the mounted tree. A plain box
// trusts its one directory. A worktree box also touches the main repository's
// .git (which may be a different directory than the project root) and its
// sibling worktrees, and safe.directory entries are not hierarchical, so it
// trusts everything -- this is a sandbox, and the same thing a project's own
// gitconfig does with `safe.directory = *`.
static QString gitTrustCommand(const BoxRecord &rec)
{
    return rec.worktreeDir.isEmpty()
        ? QStringLiteral("git config --global --add safe.directory \"$1\"")
        : QStringLiteral("git config --global --add safe.directory '*'");
}

static QString buildContainerCmd(const QString &gitSetup, const QStringList &claudeArgs)
{
    QStringList quotedParts;
    quotedParts.reserve(claudeArgs.size() + 1);
    quotedParts << QStringLiteral("claude");
    for (const QString &a : claudeArgs)
        quotedParts << shellQuote(a);
    const QString claudeCmd = quotedParts.join(QLatin1Char(' '));

    // gitSetup uses $1 (the container dir, passed as the positional arg after "_").
    // claudeArgs are embedded in claudeCmd rather than passed via "$@" to
    // avoid tmux's re-shelling mangling them.
    //
    // TSTART / LAUNCH_OK / RUNTIME: if the session ends within 15 seconds of
    // the tmux session being created, append a note to $1/.claude-box.log
    // (which is in the bind-mounted workspace and persists after --rm removes
    // the container). This is the only trace a fast startup failure leaves,
    // since --rm discards the container's own log on exit.
    return QStringLiteral("TSTART=$SECONDS; ")
         + gitSetup
         + QStringLiteral(" && tmux new-session -d -s main -- ") + claudeCmd
         + QStringLiteral(" && LAUNCH_OK=1;"
                          " while tmux has-session -t main 2>/dev/null; do sleep 1; done;"
                          " RUNTIME=$((SECONDS-TSTART));"
                          " [ \"${LAUNCH_OK:-0}\" -eq 1 ] && [ \"$RUNTIME\" -le 15 ]"
                          " && echo \"[$(date -u '+%Y-%m-%dT%H:%M:%SZ')]"
                          " claude-box: session ended after ${RUNTIME}s --"
                          " check API key and model settings\" >> \"$1/.claude-box.log\";"
                          " true");
}

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

// docker reports names as a list, each with a leading slash.
QString containerName(const QJsonObject &container)
{
    const QJsonArray names = container.value("Names").toArray();
    if (names.isEmpty())
        return QString();
    QString name = names.first().toString();
    if (name.startsWith('/'))
        name.remove(0, 1);
    return name;
}

// Builds the sentence added to the opening prompt when a new box has mapped
// ports, so the agent knows which ports it can use for services that need to
// be reached on the host via localhost. rec.ports entries are "HOST:CONTAINER"
// -- the agent must bind the container-side number, and host and container
// numbers only coincide for auto-allocated ports, not a manually entered
// mapping like 9000:3000, so an asymmetric pair spells out both sides.
QString portHint(const QStringList &ports)
{
    QStringList mappings;
    for (const QString &p : ports) {
        const int colon = p.indexOf(':');
        if (colon < 0)
            continue;
        const QString hostPort = p.left(colon);
        const QString containerPort = p.mid(colon + 1);
        if (hostPort.isEmpty() || containerPort.isEmpty())
            continue;
        mappings << (hostPort == containerPort
                         ? containerPort
                         : QStringLiteral("%1 (reachable on the host at localhost:%2)")
                               .arg(containerPort, hostPort));
    }
    if (mappings.isEmpty())
        return QString();

    return PromptTemplates::portHint().arg(mappings.join(QStringLiteral(", ")));
}

} // namespace

// docker's own sizing: binary units, four significant digits, so these
// read identically to `docker stats` / `docker ps -s` output.
QString DockerBackend::humanBytes(quint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'g', 4) + units[unit];
}

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
    QStringList existing = out.split('\n', Qt::SkipEmptyParts);
    // A box in a shared container has no container of its own to collide
    // with, so its name must not match another record (a stopped box) or a
    // live tmux session (which has to be unique within its container).
    for (const BoxRecord &rec : BoxRecord::loadAll())
        existing << rec.name;
    existing += SharedContainer::allLiveSessions();

    int n = 1;
    while (existing.contains(candidate)) {
        ++n;
        candidate = QString("claude-agent-%1-%2").arg(sanitizedBase).arg(n);
    }
    return candidate;
}

namespace {
// How long a stats sample stays usable. Longer than the refresh interval
// on purpose -- cpu/mem readouts are ambient information, and paying a
// second of worker time for them on every tick is not worth it.
constexpr qint64 kStatsMaxAgeMs = 10000;

// Parses one side of docker stats' MemUsage column, e.g. "123.4MiB" or
// "512B", into a byte count. Returns 0 if the token isn't in that format.
quint64 parseDockerStatsBytes(const QString &token)
{
    static const QRegularExpression re(QStringLiteral("^([0-9.]+)\\s*([KMGTP]?i?B)$"));
    const QRegularExpressionMatch m = re.match(token.trimmed());
    if (!m.hasMatch())
        return 0;

    quint64 multiplier = 1;
    const QString unit = m.captured(2);
    if (unit == QStringLiteral("KiB"))
        multiplier = 1024ULL;
    else if (unit == QStringLiteral("MiB"))
        multiplier = 1024ULL * 1024;
    else if (unit == QStringLiteral("GiB"))
        multiplier = 1024ULL * 1024 * 1024;
    else if (unit == QStringLiteral("TiB"))
        multiplier = 1024ULL * 1024 * 1024 * 1024;
    else if (unit == QStringLiteral("PiB"))
        multiplier = 1024ULL * 1024 * 1024 * 1024 * 1024;
    // "B" alone leaves multiplier at 1.

    return quint64(m.captured(1).toDouble() * double(multiplier));
}
}

void DockerBackend::invalidateStats()
{
    m_statsSampled = false;
}

void DockerBackend::refreshStatsCache() const
{
    if (m_statsSampled && m_statsAge.isValid() && m_statsAge.elapsed() < kStatsMaxAgeMs)
        return;

    QString out, err;
    m_statsCache.clear();
    m_statsNumericCache.clear();
    // One call for every running container, rather than one call each.
    if (runDocker({"stats", "--no-stream", "--format", "{{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}"},
                   &out, &err, 30000)) {
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split('\t');
            if (parts.size() != 3)
                continue;
            const QString &name = parts.at(0);
            const QString &cpuText = parts.at(1);
            const QString &memText = parts.at(2);
            m_statsCache.insert(name, QString("cpu %1 · mem %2").arg(cpuText, memText));

            // MemUsage looks like "123.4MiB / 1.951GiB".
            const QStringList memParts = memText.split(QStringLiteral(" / "));
            if (memParts.size() == 2) {
                CliStats stats;
                QString cpuDigits = cpuText;
                cpuDigits.remove('%');
                bool cpuOk = false;
                const double cpuValue = cpuDigits.trimmed().toDouble(&cpuOk);
                if (cpuOk)
                    stats.cpuPct = float(cpuValue);
                stats.memUsedBytes  = parseDockerStatsBytes(memParts.at(0));
                stats.memLimitBytes = parseDockerStatsBytes(memParts.at(1));
                m_statsNumericCache.insert(name, stats);
            }
        }
    }

    m_statsSampled = true;
    m_statsAge.restart();
}

QList<BoxInfo> DockerBackend::listBoxes(bool sampleStats) const
{
    QList<BoxInfo> result;
    QSet<QString> seen;
    m_lastPollDaemonUnreachable = false;

    // The API first, the CLI if it isn't reachable. Both fill the same two
    // containers, so the "merge in the known records" tail below doesn't
    // care which one ran.
    if (!listViaApi(result, seen, sampleStats))
        listViaCli(result, seen, sampleStats);
    expandSharedContainers(result, seen);

    // Stopped: every tracked record not already accounted for (no container).
    const QList<BoxRecord> known = BoxRecord::loadAll();
    for (const BoxRecord &rec : known) {
        if (seen.contains(rec.name))
            continue;

        BoxInfo info;
        info.status = BoxInfo::Status::Stopped;
        info.name = rec.name;
        info.conversationName = rec.conversationName;
        info.targetDir = rec.targetDir;
        info.detail = "not running";
        info.wasRunning = rec.wasRunning;
        result.append(info);
        seen.insert(rec.name);
    }

    return result;
}

// Containers as the daemon reports them, plus a stats sample per running
// box. The name filter is the same regex the CLI used; the daemon matches
// it against names with their leading slash stripped.
bool DockerBackend::listViaApi(QList<BoxInfo> &result, QSet<QString> &seen, bool sampleStats) const
{
    if (!DockerApi::isAvailable())
        return false;

    const QString base = QStringLiteral("/") + DockerApi::kApiVersion + "/containers/json";
    const QString nameFilter = QUrl::toPercentEncoding("{\"name\":[\"^claude-agent-\"]}");

    QString error;
    const QJsonDocument running = DockerApi::get(base + "?filters=" + nameFilter, &error);
    if (running.isNull() || !running.isArray()) {
        // isAvailable() only latches true once the socket has answered at
        // least once, so getting here means a daemon that was reachable a
        // moment ago just stopped answering -- a real "went away", not the
        // routine "this DOCKER_HOST needs the CLI fallback" case.
        m_lastPollDaemonUnreachable = true;
        return false; // fall back rather than show an empty dashboard
    }

    for (const QJsonValue &value : running.array()) {
        const QJsonObject container = value.toObject();

        BoxInfo info;
        info.status = BoxInfo::Status::Running;
        info.name = containerName(container);
        info.detail = container.value("Status").toString();

        const BoxRecord rec = BoxRecord::load(info.name);
        if (rec.isValid()) {
            info.conversationName = rec.conversationName;
            info.targetDir = rec.targetDir;
        } else {
            info.conversationName = info.name;
        }

        if (sampleStats)
            info.stats = statsDetail(container.value("Id").toString(), info);

        result.append(info);
        seen.insert(info.name);
    }

    // Stopped but not yet removed -- rare, since containers run with
    // --rm, but can happen if dockerd restarted mid-cleanup.
    const QString exitedFilter =
        QUrl::toPercentEncoding("{\"name\":[\"^claude-agent-\"],\"status\":[\"exited\"]}");
    const QJsonDocument exited = DockerApi::get(base + "?all=1&size=1&filters=" + exitedFilter, &error);
    for (const QJsonValue &value : exited.array()) {
        const QJsonObject container = value.toObject();
        const QString name = containerName(container);
        if (seen.contains(name))
            continue;

        BoxInfo info;
        info.status = BoxInfo::Status::Exited;
        info.name = name;
        info.detail = container.value("Status").toString() + " · "
            + humanBytes(quint64(container.value("SizeRw").toDouble()))
            + " (virtual " + humanBytes(quint64(container.value("SizeRootFs").toDouble())) + ")";
        result.append(info);
        seen.insert(name);
    }

    return true;
}

// One sample of a container's counters, turned into the same
// "cpu N% · mem A / B" string the CLI path produces. CPU is a rate, so it
// needs the previous tick's sample; memory is absolute and always shown.
QString DockerBackend::statsDetail(const QString &id, BoxInfo &out) const
{
    if (id.isEmpty())
        return QString();

    // one-shot skips the daemon's own second sample -- the whole reason
    // this path exists, since that wait is what costs the CLI ~2 seconds.
    const QJsonDocument doc = DockerApi::get(
        QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + id
            + "/stats?stream=false&one-shot=true",
        nullptr, 5000);
    if (doc.isNull()) {
        out.cpuPct = -1.0f;
        // Remove the stale prior-sample so the next successful call doesn't
        // compute a delta across a double-length interval and return a bogus %.
        m_prevCpu.remove(id);
        return QString();
    }

    const QJsonObject stats = doc.object();
    const QJsonObject cpu = stats.value("cpu_stats").toObject();
    const QJsonObject memory = stats.value("memory_stats").toObject();

    CpuSample sample;
    sample.containerUsage = quint64(cpu.value("cpu_usage").toObject().value("total_usage").toDouble());
    sample.systemUsage = quint64(cpu.value("system_cpu_usage").toDouble());
    sample.onlineCpus = cpu.value("online_cpus").toInt(1);

    QString cpuText;
    double cpuPctValue = -1.0;
    const auto previous = m_prevCpu.constFind(id);
    if (previous != m_prevCpu.constEnd() && sample.systemUsage > previous->systemUsage
        && sample.containerUsage >= previous->containerUsage) {
        const double containerDelta = double(sample.containerUsage - previous->containerUsage);
        const double systemDelta = double(sample.systemUsage - previous->systemUsage);
        cpuPctValue = containerDelta / systemDelta * sample.onlineCpus * 100.0;
        cpuText = QString("cpu %1% · ").arg(cpuPctValue, 0, 'f', 2);
    }
    m_prevCpu.insert(id, sample);

    // `usage` includes reclaimable page cache; docker stats subtracts
    // inactive_file before reporting, and matching it to the byte matters
    // more than being technically arguable.
    const quint64 usage = quint64(memory.value("usage").toDouble());
    const quint64 inactiveFile =
        quint64(memory.value("stats").toObject().value("inactive_file").toDouble());
    const quint64 used = usage > inactiveFile ? usage - inactiveFile : usage;
    const quint64 limit = quint64(memory.value("limit").toDouble());

    // Disk usage via container inspect with ?size=true. Works on both
    // cgroupsv1 and cgroupsv2, unlike blkio_stats.io_service_bytes_recursive
    // which is null on cgroupsv2 (Linux 5.8+ default, Ubuntu 22.04+).
    // Cost: a second sequential HTTP call per container per poll cycle.
    // Both calls run on a background thread (QtConcurrent), so the GUI
    // stays responsive, but the refresh watcher's isRunning() guard means
    // a slow daemon can stall the next poll for up to 2*N*5s (N containers).
    // Do not add more sequential calls here without measuring the impact.
    quint64 diskRw = 0, diskTotal = 0;
    const QJsonDocument sizeDoc = DockerApi::get(
        QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + id
            + "/json?size=true",
        nullptr, 5000);
    if (!sizeDoc.isNull()) {
        const QJsonObject sizeObj = sizeDoc.object();
        diskRw    = quint64(sizeObj.value(QStringLiteral("SizeRw")).toDouble());
        diskTotal = quint64(sizeObj.value(QStringLiteral("SizeRootFs")).toDouble());
    }

    out.cpuPct         = float(cpuPctValue);
    out.memUsedBytes   = used;
    out.memLimitBytes  = limit;
    out.diskRwBytes    = diskRw;
    out.diskTotalBytes = diskTotal;

    return cpuText + "mem " + humanBytes(used) + " / " + humanBytes(limit);
}

void DockerBackend::listViaCli(QList<BoxInfo> &result, QSet<QString> &seen, bool sampleStats) const
{
    if (sampleStats)
        refreshStatsCache();

    QString out, err;

    // Running.
    if (!runDocker({"ps", "--filter", "name=^claude-agent-", "--format", "{{.Names}}\t{{.Status}}"},
                    &out, &err, 5000)) {
        // The command itself failed to run rather than running and finding
        // nothing -- the daemon this CLI talks to is unreachable right now.
        m_lastPollDaemonUnreachable = true;
    } else {
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

            info.stats = m_statsCache.value(info.name);
            if (const auto it = m_statsNumericCache.constFind(info.name);
                it != m_statsNumericCache.constEnd()) {
                info.cpuPct        = it->cpuPct;
                info.memUsedBytes  = it->memUsedBytes;
                info.memLimitBytes = it->memLimitBytes;
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
            info.status = BoxInfo::Status::Exited;
            info.name = parts.at(0);
            info.detail = parts.at(1) + " · " + parts.at(2);
            result.append(info);
            seen.insert(info.name);
        }
    }
}

bool DockerBackend::isRunning(const QString &name) const
{
    // A box in a shared container is running when its session is alive in a
    // running container, not when a container of its own name exists.
    const BoxRecord rec = BoxRecord::load(name);
    if (rec.isValid() && rec.isShared())
        return isRunning(rec.containerName()) && SharedContainer::liveSessions(rec.containerName()).contains(rec.tmuxSession());

    if (DockerApi::isAvailable()) {
        const QJsonDocument doc = DockerApi::get(
            QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + name + "/json");
        return doc.object().value("State").toObject().value("Running").toBool();
    }

    QString out, err;
    if (!runDocker({"ps", "--filter", "name=^claude-agent-", "--format", "{{.Names}}"}, &out, &err, 5000))
        return false;
    return out.split('\n', Qt::SkipEmptyParts).contains(name);
}

QString DockerBackend::claudeCodeImageVersion() const
{
    // Reads the version file written into the image at build time. An image
    // built before this file existed returns a non-zero exit code, leaving
    // the output empty -- the caller treats that as "unknown / out of date".
    //
    // This method is intentionally free of QSettings: it runs on a thread-
    // pool thread (called from ClaudeCodeUpdateChecker), and QSettings is not
    // thread-safe. The caller caches the result and writes to QSettings on the
    // GUI thread.
    QString out, err;
    const bool ok = runDocker(
        {QStringLiteral("run"), QStringLiteral("--rm"),
         QStringLiteral("--entrypoint"), QStringLiteral(""),
         QStringLiteral("claude-code"),
         QStringLiteral("cat"), QStringLiteral("/etc/claude-code-version")},
        &out, &err, 15000);
    return ok ? out.trimmed() : QString();
}

void DockerBackend::invalidateClaudeCodeVersionCache()
{
    QSettings s;
    s.remove(QStringLiteral("claudeCode/versionCachedAt"));
}

// Username inside the container, derived from the host at runtime so the
// image can be built with any USER_NAME and still match what the host expects.
// Falls back to "user" if none of $USER (Linux/macOS), $LOGNAME (POSIX),
// or $USERNAME (Windows) is set -- in practice one of them is always present.
QString DockerBackend::containerUsername()
{
    QString u = qEnvironmentVariable("USER");
    if (u.isEmpty())
        u = qEnvironmentVariable("LOGNAME");
    if (u.isEmpty())
        u = qEnvironmentVariable("USERNAME");
    if (u.isEmpty())
        u = QStringLiteral("user");

    // Reject only characters that break docker's --user flag or a bind-mount
    // path: whitespace, ':', and '/'. Dots are valid in Linux usernames (e.g.
    // "first.last") and the Dockerfile bakes the exact host username, so those
    // must pass through unchanged. $USERNAME on Windows may contain spaces --
    // that's the case we guard against here.
    static const QRegularExpression kBadChars(QStringLiteral("[\\s:/]"));
    if (kBadChars.match(u).hasMatch()) {
        qWarning("containerUsername: '%s' contains invalid characters; falling back to 'user'",
                 qUtf8Printable(u));
        u = QStringLiteral("user");
    }
    return u;
}

// Everything a box is launched with, expressed once. runContainer()
// turns it into `docker run` flags and runContainerViaApi() into a
// create-container JSON body -- keeping the two derived from one
// description is what stops them drifting apart.
DockerBackend::LaunchSpec DockerBackend::launchSpec(const BoxRecord &rec,
                                                    const QStringList &claudeArgs,
                                                    const QStringList &commandOverride)
{
    LaunchSpec spec;
    spec.containerUser = containerUsername();
    const QString containerHome = QStringLiteral("/home/") + spec.containerUser;
    // The container-side path: identical to rec.targetDir on Linux/macOS,
    // something real (e.g. /mnt/host/c/Users/...) on Windows, where a host
    // path can't be a container path at all. See ContainerPaths.h.
    const QString containerDir = ContainerPaths::hostToContainer(rec.targetDir);
    // Claude runs in the worktree when the box has one (it is under the
    // mounted project root); everything else is keyed to the root.
    spec.workingDir = ContainerPaths::hostToContainer(rec.workDir());
    spec.env << "IS_SANDBOX=1";
    spec.binds << (rec.targetDir + ":" + containerDir)
               << (QDir::homePath() + "/.claude:" + containerHome + "/.claude")
               << (QDir::homePath() + "/.claude.json:" + containerHome + "/.claude.json");
    for (const QString &b : projectsSymlinkBinds()) {
        if (!spec.binds.contains(b))
            spec.binds << b;
    }
    const QString sessionsBind = prepareSessionsMount(rec, containerHome);
    if (!sessionsBind.isEmpty())
        spec.binds << sessionsBind;
    // Updates arrive by rebuilding the image (Update Claude Code); the
    // in-app updater cannot write to the image's npm prefix and only nags.
    spec.env << "DISABLE_AUTOUPDATER=1";

    QString gitSetup = gitTrustCommand(rec);
    // The file is checked for on the host, but the value handed to the
    // container has to be the *container-side* path -- on Windows the host
    // string ("C:/.../gitconfig") is meaningless inside the Linux container,
    // and git silently falls back to no global config (dubious-ownership
    // errors, none of the file's settings applied).
    if (QFileInfo::exists(rec.targetDir + "/gitconfig")) {
        spec.env << ("GIT_CONFIG_GLOBAL=" + containerDir + "/gitconfig");
        gitSetup = "true"; // that file already sets safe.directory = *
    }

    spec.envFile = rec.targetDir + "/agent.env";
    if (!QFileInfo::exists(spec.envFile))
        spec.envFile.clear();

    spec.ports = rec.ports;
    for (const QString &d : rec.dirs) {
        QString hostRaw, containerPath;
        ContainerPaths::splitMountSpec(d, hostRaw, containerPath);
        const QString hostAbs = QFileInfo(hostRaw).absoluteFilePath();
        // No container side given means "mirror the host path" -- which on
        // Windows has to mean the mapped path, not the literal host string
        // (that can't be a container path at all there).
        if (containerPath.isEmpty())
            containerPath = ContainerPaths::hostToContainer(hostAbs);
        spec.binds << (hostAbs + ":" + containerPath);
    }

    if (!commandOverride.isEmpty()) {
        spec.cmd = commandOverride;
        spec.binds << sharedStateBind(rec.name);
    } else {
        spec.cmd << "bash" << "-c" << buildContainerCmd(gitSetup, claudeArgs)
                 << "_" << containerDir;
    }
    return spec;
}

bool DockerBackend::runContainer(const BoxRecord &rec, const QStringList &claudeArgs, QString *errorOut,
                                 const QStringList &commandOverride) const
{
    if (runContainerViaApi(rec, claudeArgs, errorOut, commandOverride))
        return true;
    if (DockerApi::isAvailable())
        return false; // a real failure, already reported -- don't retry differently

    QString gitSetup = gitTrustCommand(rec);
    const QString containerDir = ContainerPaths::hostToContainer(rec.targetDir);

    const QString cliUser = containerUsername();
    const QString cliContainerHome = QStringLiteral("/home/") + cliUser;

    QStringList args;
    // -i is as load-bearing as -t: without OpenStdin the container's stdin is
    // never wired to a stream, so `docker attach` can render output but has
    // nowhere to put keystrokes -- the terminal tab looks alive and silently
    // swallows everything you type (no trust prompt answer, no messages).
    args << "run" << "-d" << "-i" << "-t" << "--rm"
         << "--name" << rec.name
         << "--user" << cliUser
         << "-e" << "IS_SANDBOX=1"
         << "-w" << ContainerPaths::hostToContainer(rec.workDir())
         << "-v" << (rec.targetDir + ":" + containerDir)
         << "-v" << (QDir::homePath() + "/.claude:" + cliContainerHome + "/.claude")
         << "-v" << (QDir::homePath() + "/.claude.json:" + cliContainerHome + "/.claude.json");
    for (const QString &b : projectsSymlinkBinds())
        args << "-v" << b;
    const QString cliSessionsBind = prepareSessionsMount(rec, cliContainerHome);
    if (!cliSessionsBind.isEmpty())
        args << "-v" << cliSessionsBind;
    args << "-e" << "DISABLE_AUTOUPDATER=1";

    // Container-side path, not the host one -- see launchSpec() for why.
    if (QFileInfo::exists(rec.targetDir + "/gitconfig")) {
        args << "-e" << ("GIT_CONFIG_GLOBAL=" + containerDir + "/gitconfig");
        gitSetup = "true"; // that file already sets safe.directory = *
    }

    const QString agentEnvPath = rec.targetDir + "/agent.env";
    if (QFileInfo::exists(agentEnvPath))
        args << "--env-file" << agentEnvPath;

    for (const QString &p : rec.ports)
        args << "-p" << p;

    for (const QString &d : rec.dirs) {
        QString hostRaw, containerPath;
        ContainerPaths::splitMountSpec(d, hostRaw, containerPath);
        const QString hostPath = QFileInfo(hostRaw).absoluteFilePath();
        // No container side given means "mirror the host path" -- which on
        // Windows has to mean the mapped path, not the literal host string
        // (that can't be a container path at all there).
        if (containerPath.isEmpty())
            containerPath = ContainerPaths::hostToContainer(hostPath);
        args << "-v" << (hostPath + ":" + containerPath);
    }

    if (!commandOverride.isEmpty()) {
        args << "-v" << sharedStateBind(rec.name) << "claude-code" << commandOverride;
    } else {
        args << "claude-code" << "bash" << "-c"
             << buildContainerCmd(gitSetup, claudeArgs)
             << "_" << containerDir;
    }

    return runDocker(args, nullptr, errorOut, 30000);
}

bool DockerBackend::runContainerViaApi(const BoxRecord &rec, const QStringList &claudeArgs,
                                       QString *errorOut, const QStringList &commandOverride) const
{
    if (!DockerApi::isAvailable())
        return false;

    const LaunchSpec spec = launchSpec(rec, claudeArgs, commandOverride);

    QJsonArray cmd;
    for (const QString &arg : spec.cmd)
        cmd.append(arg);

    QJsonArray env;
    for (const QString &e : spec.env)
        env.append(e);
    // --env-file is a CLI convenience, not an API feature, so the file is
    // read here: KEY=VALUE per line, # comments and blanks skipped, and a
    // bare KEY meaning "inherit that one from this process".
    if (!spec.envFile.isEmpty()) {
        QFile file(spec.envFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                if (line.contains('='))
                    env.append(line);
                else if (qEnvironmentVariableIsSet(line.toUtf8()))
                    env.append(line + "=" + qEnvironmentVariable(line.toUtf8()));
            }
        }
    }

    QJsonArray binds;
    for (const QString &bind : spec.binds)
        binds.append(bind);

    // "8080:3000" becomes an exposed 3000/tcp plus a host binding on 8080.
    QJsonObject exposedPorts;
    QJsonObject portBindings;
    for (const QString &mapping : spec.ports) {
        const int colon = mapping.indexOf(':');
        if (colon < 0)
            continue;
        const QString hostPort = mapping.left(colon);
        const QString containerPort = mapping.mid(colon + 1) + "/tcp";
        exposedPorts.insert(containerPort, QJsonObject());
        QJsonObject binding;
        binding.insert("HostIp", QString());
        binding.insert("HostPort", hostPort);
        portBindings.insert(containerPort, QJsonArray{binding});
    }

    QJsonObject hostConfig;
    hostConfig.insert("Binds", binds);
    hostConfig.insert("AutoRemove", true); // --rm
    if (!portBindings.isEmpty())
        hostConfig.insert("PortBindings", portBindings);

    QJsonObject body;
    body.insert("HostConfig", hostConfig);
    body.insert("Image", QStringLiteral("claude-code"));
    body.insert("Cmd", cmd);
    body.insert("Env", env);
    body.insert("User", spec.containerUser);
    body.insert("WorkingDir", spec.workingDir);
    // -t and -i: a tty for claude's TUI to render into, and an open stdin
    // so `docker attach` has somewhere to deliver keystrokes.
    body.insert("Tty", true);
    body.insert("OpenStdin", true);
    if (!exposedPorts.isEmpty())
        body.insert("ExposedPorts", exposedPorts);

    QString error;
    const QJsonDocument created = DockerApi::post(
        QStringLiteral("/") + DockerApi::kApiVersion + "/containers/create?name=" + rec.name,
        body, &error);
    const QString id = created.object().value("Id").toString();
    if (id.isEmpty()) {
        if (errorOut)
            *errorOut = error.isEmpty() ? QStringLiteral("container create returned no id") : error;
        return false;
    }

    if (!DockerApi::post(QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + id + "/start",
                         {}, &error)
             .isNull()
        || error.isEmpty()) {
        return true;
    }

    // Started nothing, so don't leave the half-made container behind.
    DockerApi::del(QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + id + "?force=1");
    if (errorOut)
        *errorOut = error;
    return false;
}

// Creates the project's shared container and starts its keeper. The record
// handed to the launch code is a stand-in for the container itself: its name
// is the container's, it mounts the project root, and it publishes the
// project's port pool and extra mounts.
bool DockerBackend::runSharedContainer(const ProjectRecord &project, QString *errorOut) const
{
    BoxRecord stand;
    stand.name = project.containerName();
    stand.targetDir = project.root;
    stand.ports = project.ports;
    stand.dirs = project.dirs;
    stand.skipPermissions = false;
    return runContainer(stand, {}, errorOut, sharedKeeperCommand());
}

QStringList DockerBackend::staleSharedMounts(const ProjectRecord &project) const
{
    const QString container = project.containerName();
    if (project.dirs.isEmpty() || !isRunning(container))
        return {};

    QStringList have;
    if (DockerApi::isAvailable()) {
        const QJsonDocument doc = DockerApi::get(
            QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + container + "/json");
        for (const QJsonValue &b : doc.object().value("HostConfig").toObject().value("Binds").toArray())
            have << b.toString();
    } else {
        QString out;
        if (runDocker({"inspect", "-f", "{{range .HostConfig.Binds}}{{println .}}{{end}}", container},
                      &out, nullptr, 5000))
            have = out.split('\n', Qt::SkipEmptyParts);
    }

    QStringList missing;
    for (const QString &d : project.dirs) {
        QString hostRaw, containerPath;
        ContainerPaths::splitMountSpec(d, hostRaw, containerPath);
        const QString hostPath = QFileInfo(hostRaw).absoluteFilePath();
        if (containerPath.isEmpty())
            containerPath = ContainerPaths::hostToContainer(hostPath);
        if (!have.contains(hostPath + QLatin1Char(':') + containerPath))
            missing << d;
    }
    return missing;
}

bool DockerBackend::startShared(const BoxRecord &rec, const QStringList &claudeArgs, QString *errorOut) const
{
    QMutexLocker lock(&m_sharedMutex);

    ProjectRecord project = ProjectRecord::forRoot(rec.targetDir);
    PortAllocator::ensurePool(project);
    const QString container = rec.containerName();
    const QString workDir = ContainerPaths::hostToContainer(rec.workDir());

    // claude and its arguments as one shell-quoted string, so tmux does not
    // re-split a multi-word argument (see SharedContainer::sessionStartScript).
    QStringList parts;
    parts << QStringLiteral("claude");
    for (const QString &a : claudeArgs)
        parts << shellQuote(a);
    const QString command = parts.join(QLatin1Char(' '));

    // A container that is only lingering after its last session, with mounts
    // the project has since changed, is replaced rather than reused: nothing is
    // running in it, and reusing it would silently ignore the change.
    if (isRunning(container) && SharedContainer::liveSessions(container).isEmpty()
        && !staleSharedMounts(project).isEmpty()) {
        stopMany({container}, nullptr);
        waitForRemoval(container);
    }

    QString lastError;
    // Twice: a container whose last session ended moments ago can be on its
    // way out just as we reach it.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!isRunning(container)) {
            if (!runSharedContainer(project, &lastError)) {
                if (errorOut)
                    *errorOut = lastError;
                return false;
            }
        }

        QString out, err;
        const QStringList args = {QStringLiteral("exec"), QStringLiteral("--user"), containerUsername(),
                                  container, QStringLiteral("bash"), QStringLiteral("-c"),
                                  SharedContainer::sessionStartScript(), QStringLiteral("_"),
                                  rec.tmuxSession(), workDir, command};
        if (runDocker(args, &out, &err, 60000))
            return true;
        lastError = err.isEmpty() ? out : err;
        if (isRunning(container))
            break; // it is up, so this is a real failure, not the shutdown race
    }
    if (errorOut)
        *errorOut = lastError;
    return false;
}

// The container of a running shared project is not itself a box. Swap its row
// for one row per box in it, Running or not according to the container's
// session list, and let each carry the container's resource figures.
void DockerBackend::expandSharedContainers(QList<BoxInfo> &result, QSet<QString> &seen) const
{
    const QList<BoxRecord> known = BoxRecord::loadAll();
    QHash<QString, QList<BoxRecord>> byContainer;
    for (const BoxRecord &rec : known) {
        if (rec.isShared())
            byContainer[rec.containerName()].append(rec);
    }
    if (byContainer.isEmpty())
        return;

    for (int i = result.size() - 1; i >= 0; --i) {
        const BoxInfo container = result.at(i);
        const auto boxes = byContainer.constFind(container.name);
        if (boxes == byContainer.constEnd())
            continue;
        result.removeAt(i);

        const bool up = container.status == BoxInfo::Status::Running;
        const QStringList live = up ? SharedContainer::liveSessions(container.name) : QStringList();
        for (const BoxRecord &rec : boxes.value()) {
            BoxInfo info = container; // resource figures and detail text
            info.name = rec.name;
            info.conversationName = rec.conversationName;
            info.targetDir = rec.targetDir;
            info.wasRunning = rec.wasRunning;
            info.status = live.contains(rec.tmuxSession()) ? BoxInfo::Status::Running
                                                           : BoxInfo::Status::Stopped;
            if (info.status != BoxInfo::Status::Running) {
                info.stats.clear();
                info.cpuPct = -1.0f;
                info.memUsedBytes = 0;
                info.memLimitBytes = 0;
                info.detail = QStringLiteral("not running");
            }
            result.insert(i, info);
            seen.insert(rec.name);
        }
    }
}

bool DockerBackend::createNew(BoxRecord &rec, bool workspaceSubdir, QString *errorOut,
                              const QString &forkFromUuid, bool useWorktree,
                              const QString &forkSourceWorkDir) const
{
    // A caller may hand us the uuid of a conversation that already exists
    // on disk (see ConversationCatalog / NewBoxDialog) -- typically one
    // started outside this app. Then the box adopts it with --resume
    // instead of minting a session, and the issue-provisioning workflow
    // is skipped: that workflow exists to *open* a new conversation with
    // a scaffolded folder and an opening prompt, which is meaningless
    // when the conversation is already underway.
    //
    // forkFromUuid (mutually exclusive with rec.sessionUuid) is the other
    // caller of --resume: MainWindow::onFork asks to start a *new* session
    // that's preloaded with an existing one's history. That's the "brand
    // new conversation" case as far as provisioning and naming are
    // concerned -- a uuid still gets minted below, and a workspace folder
    // and opening prompt still make sense -- so it doesn't count as
    // resumeExisting here; only the claudeArgs built near the bottom
    // differ.
    const bool resumeExisting = !rec.sessionUuid.isEmpty();

    QString slug;
    QString openingPrompt;
    QString worktreeRepo;           // set once a worktree is provisioned
    bool worktreeWasThere = false;  // it already existed: not ours to remove on failure
    // The container still mounts and runs in the *base* directory either
    // way -- the workspace is a subfolder the agent is pointed at, not a
    // different -w. That's deliberate, and inherited from the old bash
    // launcher: the sibling folders (and the tooling beside them) stay
    // visible, and every box for a tree keeps the same mount and the same
    // ~/.claude/projects encoding.
    const bool provisionWorktree = useWorktree && !resumeExisting && !rec.conversationName.isEmpty();
    const bool provisionWorkspace = workspaceSubdir && !provisionWorktree && !resumeExisting
                                    && !rec.conversationName.isEmpty();

    if (provisionWorktree || provisionWorkspace) {
        slug = slugifyIssueName(rec.conversationName);
        if (slug.isEmpty()) {
            if (errorOut)
                *errorOut = "conversation name has no usable characters";
            return false;
        }

        QString workPath; // what the agent is told to work in
        if (provisionWorktree) {
            // A worktree of the project's primary repository. Claude runs
            // there (see BoxRecord::workDir), so its transcripts and the
            // conversation picker are keyed to that directory.
            const QString repo = ProjectRecord::forRoot(rec.targetDir).primaryRepo;
            if (repo.isEmpty()) {
                if (errorOut)
                    *errorOut = "this project has no git repository to make a worktree from";
                return false;
            }
            QString error;
            worktreeRepo = repo;
            worktreeWasThere = QFileInfo::exists(Worktrees::worktreePath(repo, slug));
            if (!Worktrees::add(repo, slug, &workPath, &error)) {
                if (errorOut)
                    *errorOut = "could not create the worktree: " + error;
                return false;
            }
            rec.worktreeDir = workPath;
            rec.branch = Worktrees::branchFor(slug);
        } else {
            workPath = rec.targetDir + "/" + slug;
            // Creating the folder is all this does. Anything a particular tree
            // needs inside it -- a repo clone, an .env, a seeded CLAUDE.md --
            // is the agent's job, described in that directory's own CLAUDE.md.
            // Scripting it here instead (this used to shell out to a project's
            // new-issue.sh) put per-project knowledge in the launcher, where
            // it can't be read by the agent that has to live with it.
            //
            // An existing folder is left exactly as it is: that means you're
            // resuming prior work, not starting fresh.
            if (!QDir(workPath).exists() && !QDir().mkpath(workPath)) {
                if (errorOut)
                    *errorOut = "could not create workspace folder " + workPath;
                return false;
            }
            rec.workspaceDir = slug;
        }

        // The notes for this conversation live in the project directory, not
        // in the workspace, so they are easy to find and are not touched
        // when a worktree goes away.
        const QString notes = Worktrees::ensureNotesDir(rec.targetDir, slug);
        const QString repoForExclude = ProjectRecord::forRoot(rec.targetDir).primaryRepo;
        if (!repoForExclude.isEmpty())
            Worktrees::ensureExcluded(repoForExclude);

        const QString notesPath = promptPath(notes);
        const QString infraPath = promptPath(Worktrees::infraFile(rec.targetDir));
        if (provisionWorktree) {
            openingPrompt = PromptTemplates::expand(
                PromptTemplates::worktree(),
                {slug, rec.conversationName, promptPath(workPath), notesPath, infraPath,
                 promptPath(repoForExclude), rec.branch});
        } else {
            openingPrompt = PromptTemplates::expand(
                PromptTemplates::workspace(),
                {slug, rec.conversationName, promptPath(workPath), notesPath, infraPath});
        }

        // One name for everything: the folder, the box, the record and
        // claude's own session name all become the slug, so a row in the
        // dashboard, a container in `docker ps` and a directory listing
        // all say the same word.
        rec.conversationName = slug;
    }

    // Starter prompt: sent to new boxes that have no workspace subfolder.
    // Empty by default; configured in File > Edit Prompts.
    if (!resumeExisting && !provisionWorkspace && !provisionWorktree) {
        const QString s = PromptTemplates::starter();
        if (!s.isEmpty())
            openingPrompt = PromptTemplates::expand(
                s, {rec.conversationName, promptPath(rec.targetDir)});
    }

    // Tell the agent which ports it can use for services the host needs to
    // reach. Skipped when resuming an existing session: the agent was already
    // told at creation time and the ports haven't changed.
    if (!resumeExisting) {
        const QString hint = portHint(rec.ports);
        if (!hint.isEmpty()) {
            if (openingPrompt.isEmpty())
                openingPrompt = hint;
            else
                openingPrompt += "\n\n" + hint;
        }
    }

    const QString base = slug.isEmpty() ? QFileInfo(rec.targetDir).fileName() : slug;
    rec.name = uniqueBoxName(base);
    if (!resumeExisting)
        rec.sessionUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (rec.conversationName.isEmpty())
        rec.conversationName = rec.name;

    QStringList claudeArgs = baseClaudeArgs(rec);
    // --name is claude's own session display name, and it applies to any
    // named conversation -- the old launcher always forwarded it, and only
    // the folder provisioning was conditional. The opening prompt is the
    // part that depends on there being a workspace to point at.
    if (!rec.conversationName.isEmpty()) {
        claudeArgs << "--name" << rec.conversationName;
        rec.transcriptSyncedName = rec.conversationName;
    }
    if (!openingPrompt.isEmpty())
        claudeArgs << openingPrompt;
    if (!forkFromUuid.isEmpty() && !forkSourceWorkDir.isEmpty()) {
        // Claude finds the source transcript only under its own working
        // directory, which a worktree box does not share with its source.
        const QString from = ConversationCatalog::projectDirFor(forkSourceWorkDir);
        const QString to = ConversationCatalog::projectDirFor(rec.workDir());
        if (from != to) {
            const QString src = from + '/' + forkFromUuid + ".jsonl";
            const QString dst = to + '/' + forkFromUuid + ".jsonl";
            QDir().mkpath(to);
            if (!QFileInfo::exists(dst)) {
                // Without it `claude --resume` finds nothing and the fork
                // would quietly start empty.
                if (!QFileInfo::exists(src)) {
                    if (errorOut)
                        *errorOut = "the conversation being forked has no transcript at " + src;
                    return false;
                }
                if (!QFile::copy(src, dst)) {
                    if (errorOut)
                        *errorOut = "could not copy the source transcript to " + dst;
                    return false;
                }
            }
        }
    }
    if (!forkFromUuid.isEmpty())
        claudeArgs << "--resume" << forkFromUuid << "--fork-session" << "--session-id" << rec.sessionUuid;
    else
        claudeArgs << (resumeExisting ? "--resume" : "--session-id") << rec.sessionUuid;

    // A project that shares one container starts the conversation as a tmux
    // session in it; the box's record names the container and the session.
    const ProjectRecord project = ProjectRecord::forRoot(rec.targetDir);
    const bool shared = project.sharedContainer;
    if (shared) {
        rec.container = project.containerName();
        rec.session = rec.name;
    }

    if (!(shared ? startShared(rec, claudeArgs, errorOut) : runContainer(rec, claudeArgs, errorOut))) {
        // A worktree made for this attempt has no box to belong to now. It is
        // brand new, so the safety checks in remove() pass; a worktree that
        // was already there is left alone.
        if (!worktreeRepo.isEmpty() && !worktreeWasThere)
            Worktrees::remove(worktreeRepo, rec.worktreeDir, nullptr);
        return false;
    }

    rec.wasRunning = true;
    rec.save();
    return true;
}

// The per-box `claude` flags that are the same whether a box is being
// created or reopened. Kept in one place so the two paths can't drift --
// a box that reopened with different flags than it started with is a
// confusing bug to chase.
QStringList DockerBackend::baseClaudeArgs(const BoxRecord &rec)
{
    QStringList args;

    // Unconditional, and not recorded per box: remote control is how you
    // pick a conversation up on another device, and there is no reason to
    // run a sandbox without that available.
    args << "--remote-control";

    if (rec.skipPermissions)
        args << "--dangerously-skip-permissions";
    if (!rec.model.isEmpty())
        args << "--model" << rec.model;
    if (!rec.effort.isEmpty())
        args << "--effort" << rec.effort;

    // A worktree sits inside the project root, but when the primary
    // repository is a subdirectory of it (reference repositories beside it,
    // say), the rest of the root is outside Claude's working directory.
    if (!rec.worktreeDir.isEmpty()) {
        const ProjectRecord project = ProjectRecord::forRoot(rec.targetDir);
        if (!project.primaryRepo.isEmpty()
            && ProjectRecord::normalizeRoot(project.primaryRepo) != project.root)
            args << "--add-dir" << ContainerPaths::hostToContainer(rec.targetDir);
    }

    return args;
}

bool DockerBackend::reopen(const BoxRecord &rec, QString *errorOut) const
{
    BoxRecord updated = rec;

    QStringList claudeArgs = baseClaudeArgs(updated);

    if (!updated.conversationName.isEmpty())
        claudeArgs << "--name" << updated.conversationName;

    // If the transcript is missing (box crashed before Claude wrote it,
    // or the session store was relocated),
    // fall back to a fresh session. Claude silently starts a new
    // conversation when --resume gets an unknown UUID, so we detect this
    // explicitly: mint a new UUID and use --session-id, which keeps the
    // record consistent so subsequent reopens find the new transcript.
    const QString transcriptPath =
        ConversationCatalog::projectDirFor(updated.workDir())
        + QLatin1Char('/') + updated.sessionUuid + QStringLiteral(".jsonl");
    const bool hasTranscript = !updated.sessionUuid.isEmpty()
                               && QFile::exists(transcriptPath);

    if (!hasTranscript)
        updated.sessionUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    claudeArgs << (hasTranscript ? QStringLiteral("--resume")
                                 : QStringLiteral("--session-id"))
               << updated.sessionUuid;

    if (!(updated.isShared() ? startShared(updated, claudeArgs, errorOut)
                             : runContainer(updated, claudeArgs, errorOut)))
        return false;

    updated.wasRunning = true;
    updated.save();
    return true;
}

bool DockerBackend::stop(const QString &name, QString *errorOut) const
{
    QHash<QString, QString> errors;
    stopMany({name}, &errors);
    if (errors.contains(name)) {
        if (errorOut)
            *errorOut = errors.value(name);
        return false;
    }
    // Wait for removal so an immediate reopen() (called from onEdit's
    // stop-then-restart flow) does not collide with the still-alive name.
    // stopMany() itself no longer waits, so async callers (onClose,
    // onStopAll) can fire-and-forget without blocking the GUI thread.
    waitForRemoval(name);
    return true;
}

void DockerBackend::stopMany(const QStringList &names, QHash<QString, QString> *errorsOut) const
{
    // Issue every stop request up front. The old version followed each stop
    // with waitForRemovalMany() to ensure names were gone before returning;
    // that call is now in stop() only (for onEdit's synchronous stop-then-
    // reopen flow). Async callers (onClose, onStopAll) run this method on a
    // worker thread and handle the completion via QMetaObject::invokeMethod.
    for (const QString &name : names) {
        bool ok = false;
        QString error;
        const BoxRecord shared = BoxRecord::load(name);
        if (shared.isValid() && shared.isShared()) {
            // Ending the conversation's tmux session is the whole stop; the
            // container itself leaves once its last session has been gone for
            // a while. A container that is already gone means it is stopped.
            if (!isRunning(shared.containerName())) {
                ok = true;
            } else {
                ok = runDocker({"exec", shared.containerName(), "tmux", "kill-session", "-t",
                                "=" + shared.tmuxSession()}, nullptr, &error, 15000);
                // A session that was not there is as stopped as one just killed.
                if (!ok && error.contains(QStringLiteral("can't find session")))
                    ok = true;
            }
        } else if (DockerApi::isAvailable()) {
            DockerApi::post(QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + name + "/stop",
                            {}, &error, 20000);
            ok = error.isEmpty();
        } else {
            ok = runDocker({"stop", name}, nullptr, &error, 15000);
        }

        if (ok) {
            BoxRecord rec = BoxRecord::load(name);
            if (rec.isValid()) {
                rec.wasRunning = false;
                rec.save();
            }
        } else if (errorsOut) {
            errorsOut->insert(name, error);
        }
    }

    // waitForRemovalMany() is NOT called here so async callers (onClose,
    // onStopAll) can run stopMany() on a worker thread without the nested
    // event loop. The synchronous stop() above adds the wait for callers
    // (onEdit) that immediately reopen the same name.
}

bool DockerBackend::waitForRemoval(const QString &name) const
{
    return waitForRemovalMany({name}).isEmpty();
}

QStringList DockerBackend::waitForRemovalMany(const QStringList &names) const
{
    constexpr int kPollMs    = 100;
    constexpr int kTimeoutMs = 5000;

    if (names.isEmpty())
        return {};

    // stop() calls this on the GUI thread (for onEdit's synchronous stop-
    // then-reopen flow), so loop.exec() is a re-entrant nested event loop.
    // ExcludeUserInputEvents keeps the user from clicking menu items while
    // the short wait is in progress; timer signals and posted events can
    // still fire. stopMany() no longer calls this -- async close paths
    // (onClose/onStopAll) do not need to block on removal.
    QSet<QString> remaining(names.begin(), names.end());
    QEventLoop loop;
    QTimer poller;
    QTimer timeout;

    QObject::connect(&poller, &QTimer::timeout, [&]() {
        for (auto it = remaining.begin(); it != remaining.end();) {
            const QString &name = *it;
            // A box in a shared container is gone when its session has left
            // the container's list, not when a container of its name has.
            const BoxRecord sharedRec = BoxRecord::load(name);
            if (sharedRec.isValid() && sharedRec.isShared()) {
                if (!SharedContainer::liveSessions(sharedRec.containerName()).contains(sharedRec.tmuxSession()))
                    it = remaining.erase(it);
                else
                    ++it;
                continue;
            }
            // Only a definitive answer -- confirmed gone, or confirmed still
            // present -- settles `exists`. A transient failure (socket hiccup,
            // docker CLI timeout) must not be read as "removed": that would
            // exit the wait early and risk reopen() colliding with the still-
            // live name.
            bool exists = true;
            bool determined = false;
            if (DockerApi::isAvailable()) {
                QString error;
                DockerApi::get(QStringLiteral("/") + DockerApi::kApiVersion
                               + "/containers/" + name + "/json", &error);
                if (error.isEmpty()) {
                    exists = true;
                    determined = true;
                } else if (error.startsWith(QStringLiteral("HTTP 404"))) {
                    exists = false;
                    determined = true;
                }
                // Any other error (connection issue, timeout) is transient;
                // determined stays false and this name is simply retried.
            } else {
                QString out, err;
                if (runDocker({"ps", "-a", "--filter", "name=^" + name + "$",
                               "--format", "{{.Names}}"}, &out, &err, 2000)) {
                    exists = (out.trimmed() == name);
                    determined = true;
                }
                // runDocker failure is transient for the same reason.
            }
            if (determined && !exists)
                it = remaining.erase(it);
            else
                ++it;
        }
        if (remaining.isEmpty())
            loop.quit();
    });

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.setSingleShot(true);
    timeout.start(kTimeoutMs);
    poller.start(kPollMs);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    return remaining.values();
}

bool DockerBackend::remove(const QString &name, QString *errorOut) const
{
    if (DockerApi::isAvailable())
        return DockerApi::del(QStringLiteral("/") + DockerApi::kApiVersion + "/containers/" + name,
                              errorOut);
    return runDocker({"rm", name}, nullptr, errorOut, 5000);
}
