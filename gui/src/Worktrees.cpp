#include "Worktrees.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTextStream>

namespace Worktrees {

Result git(const QString &dir, const QStringList &args, int timeoutMs, bool trim)
{
    QProcess p;
    // Never prompt (a credential or merge-editor prompt would hang a GUI
    // thread), and keep the output in one language.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p.setProcessEnvironment(env);

    QStringList full;
    if (!dir.isEmpty())
        full << QStringLiteral("-C") << dir;
    full << args;
    p.start(QStringLiteral("git"), full);
    Result r;
    if (!p.waitForStarted(5000)) {
        r.output = QStringLiteral("could not run git");
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        r.output = QStringLiteral("git timed out");
        return r;
    }
    r.ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    r.output = QString::fromUtf8(r.ok ? p.readAllStandardOutput() : p.readAllStandardError());
    if (trim)
        r.output = r.output.trimmed();
    return r;
}

bool isRepo(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo(dir).isDir()
        && git(dir, {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")}).ok;
}

QString topLevel(const QString &dir)
{
    const Result r = git(dir, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    return r.ok ? QDir::cleanPath(r.output) : QString();
}

QString currentBranch(const QString &dir)
{
    const Result r = git(dir, {QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("-q"), QStringLiteral("HEAD")});
    return r.ok ? r.output : QString();
}

QString originUrl(const QString &dir)
{
    const Result r = git(dir, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
    return r.ok ? r.output : QString();
}

QString worktreePath(const QString &repo, const QString &slug)
{
    return QDir::cleanPath(repo + QLatin1Char('/') + kDirName + QLatin1Char('/') + slug);
}

QString branchFor(const QString &slug)
{
    return kBranchPrefix + slug;
}

QString notesDir(const QString &projectRoot, const QString &slug)
{
    return QDir::cleanPath(projectRoot + QLatin1Char('/') + kNotesDirName
                           + QStringLiteral("/conversations/") + slug);
}

QString infraFile(const QString &projectRoot)
{
    return QDir::cleanPath(projectRoot + QLatin1Char('/') + kNotesDirName + QStringLiteral("/INFRA.md"));
}

QString ensureNotesDir(const QString &projectRoot, const QString &slug)
{
    const QString dir = notesDir(projectRoot, slug);
    QDir().mkpath(dir);
    return dir;
}

bool ensureExcluded(const QString &repo, QString *error)
{
    const Result r = git(repo, {QStringLiteral("rev-parse"), QStringLiteral("--git-common-dir")});
    if (!r.ok) {
        if (error)
            *error = r.output;
        return false;
    }
    QString common = r.output;
    if (QDir::isRelativePath(common))
        common = QDir(repo).absoluteFilePath(common);

    const QString file = QDir::cleanPath(common + QStringLiteral("/info/exclude"));
    QStringList have;
    {
        QFile in(file);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&in);
            while (!ts.atEnd()) {
                // Git ignores trailing spaces but a leading space is part of the
                // pattern, so only the end is trimmed.
                QString line = ts.readLine();
                while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))
                    line.chop(1);
                have << line;
            }
        }
    }
    const QStringList want = {kDirName + QLatin1Char('/'), kNotesDirName + QLatin1Char('/')};
    QStringList missing;
    for (const QString &w : want) {
        if (!have.contains(w) && !have.contains(QLatin1Char('/') + w))
            missing << w;
    }
    if (missing.isEmpty())
        return true;

    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile out(file);
    if (!out.open(QIODevice::Append | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write ") + file;
        return false;
    }
    QTextStream ts(&out);
    // The file may not end in a newline.
    if (out.size() > 0) {
        QFile probe(file);
        if (probe.open(QIODevice::ReadOnly)) {
            probe.seek(probe.size() - 1);
            if (probe.read(1) != "\n")
                ts << '\n';
        }
    }
    ts << "# claude-box\n";
    for (const QString &m : missing)
        ts << m << '\n';
    return true;
}

bool add(const QString &repo, const QString &slug, QString *pathOut, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    const QString path = worktreePath(repo, slug);
    const QString branch = branchFor(slug);

    QString exErr;
    if (!ensureExcluded(repo, &exErr))
        return fail(QStringLiteral("could not update info/exclude: ") + exErr);

    // Already there: a conversation being resumed or re-created.
    if (QFileInfo(path + QStringLiteral("/.git")).exists()) {
        if (pathOut)
            *pathOut = path;
        return true;
    }
    if (QFileInfo::exists(path))
        return fail(path + QStringLiteral(" exists but is not a git worktree"));

#ifdef Q_OS_WIN
    // See the header: the absolute path in a worktree's .git file is a
    // Windows path the container cannot follow.
    if (!git(repo, {QStringLiteral("config"), QStringLiteral("worktree.useRelativePaths"), QStringLiteral("true")}).ok)
        return fail(QStringLiteral("could not enable relative worktree paths; this needs git 2.48 or later"));
#endif

    const bool branchExists =
        git(repo, {QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
                   QStringLiteral("refs/heads/") + branch}).ok;
    const QStringList args = branchExists
        ? QStringList{QStringLiteral("worktree"), QStringLiteral("add"), path, branch}
        : QStringList{QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-b"), branch, path};
    const Result r = git(repo, args, 120000);
    if (!r.ok)
        return fail(r.output);
    if (pathOut)
        *pathOut = path;
    return true;
}

bool isBuildOutputName(const QString &name)
{
    static const QSet<QString> names = {
        QStringLiteral("node_modules"), QStringLiteral("build"), QStringLiteral("dist"),
        QStringLiteral("target"), QStringLiteral(".gradle"), QStringLiteral("__pycache__"),
        QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral(".cache"),
        QStringLiteral(".next"), QStringLiteral("out")};
    return names.contains(name);
}

RemoveCheck checkRemovable(const QString &repo, const QString &path)
{
    RemoveCheck c;
    if (!QFileInfo(path + QStringLiteral("/.git")).exists()) {
        c.reason = QStringLiteral("not a worktree");
        return c;
    }

    const Result st = git(path, {QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("--ignored=no")});
    if (!st.ok) {
        c.reason = st.output;
        return c;
    }
    if (!st.output.isEmpty()) {
        c.reason = QStringLiteral("it has uncommitted or untracked changes");
        return c;
    }

    // `git worktree remove` deletes ignored files too, and those are often the
    // only copy of a .env or a credentials file.
    const Result ign = git(path, {QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--others"),
                                  QStringLiteral("--ignored"), QStringLiteral("--exclude-standard"),
                                  QStringLiteral("--directory")}, 30000, /*trim=*/false);
    if (!ign.ok) {
        c.reason = ign.output;
        return c;
    }
    QStringList keep;
    for (const QString &entry : ign.output.split(QChar(0), Qt::SkipEmptyParts)) {
        const QString leaf = QFileInfo(entry.endsWith(QLatin1Char('/')) ? entry.chopped(1) : entry).fileName();
        if (!isBuildOutputName(leaf))
            keep << entry;
    }
    if (!keep.isEmpty()) {
        c.reason = QStringLiteral("it has ignored files that exist nowhere else (%1)").arg(keep.mid(0, 3).join(QStringLiteral(", ")));
        return c;
    }

    // Commits reachable only from here. A detached HEAD has no branch to
    // name, so check HEAD itself; on a branch, leave that branch out of the
    // comparison (it trivially contains its own commits).
    const QString branch = currentBranch(path);
    QStringList revArgs = {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD"),
                           QStringLiteral("--not")};
    if (!branch.isEmpty())
        revArgs << QStringLiteral("--exclude=") + branch; // relative to refs/heads/, not a full ref
    revArgs << QStringLiteral("--branches") << QStringLiteral("--remotes");
    const Result ahead = git(path, revArgs);
    if (!ahead.ok) {
        c.reason = ahead.output;
        return c;
    }
    if (ahead.output.toInt() > 0) {
        c.reason = branch.isEmpty()
            ? QStringLiteral("HEAD is detached and has %1 commit(s) that are not on any branch").arg(ahead.output)
            : QStringLiteral("branch %1 has %2 commit(s) that are not on any other branch").arg(branch, ahead.output);
        return c;
    }
    c.safe = true;
    return c;
}

bool remove(const QString &repo, const QString &path, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const RemoveCheck c = checkRemovable(repo, path);
    if (!c.safe)
        return fail(c.reason);

    const QString branch = currentBranch(path);
    const Result r = git(repo, {QStringLiteral("worktree"), QStringLiteral("remove"), path});
    if (!r.ok)
        return fail(r.output);
    // checkRemovable() proved nothing unique is on the branch, so a plain
    // delete is safe; -D because git's own merged-into-HEAD test is stricter
    // than "reachable from some other branch".
    if (!branch.isEmpty() && branch.startsWith(kBranchPrefix))
        git(repo, {QStringLiteral("branch"), QStringLiteral("-D"), branch});
    return true;
}

} // namespace Worktrees
