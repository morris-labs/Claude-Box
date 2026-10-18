#include "WorktreeMigration.h"

#include "ConversationCatalog.h"
#include "ProjectRecord.h"
#include "Worktrees.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace WorktreeMigration {

namespace {

// The notes files an agent keeps at a folder's top level; they move to the
// notes directory instead of becoming untracked files in the worktree.
const QStringList &notesNames()
{
    static const QStringList names = {QStringLiteral("PLAN.md"), QStringLiteral("STATUS.md"),
                                      QStringLiteral("INFRA.md")};
    return names;
}

// The same repository is spelled differently over ssh and https
// (git@host:org/repo.git, https://host/org/repo), so compare host and path only.
QString normalizeUrl(QString url)
{
    url = url.trimmed();
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
    url.remove(scheme);
    const int at = url.indexOf(QLatin1Char('@'));
    if (at >= 0 && at < url.indexOf(QLatin1Char('/')) + (url.contains(QLatin1Char('/')) ? 0 : url.size()))
        url = url.mid(at + 1);
    // scp-style "host:path"
    static const QRegularExpression scp(QStringLiteral("^([^/:]+):(?!//)"));
    url.replace(scp, QStringLiteral("\\1/"));
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    if (url.endsWith(QLatin1String(".git")))
        url.chop(4);
    return url.toLower();
}

// `git status --porcelain=v1 -z` entries as (XY, path, origPath).
struct StatusEntry {
    QString xy;
    QString path;
};
QList<StatusEntry> parseStatus(const QString &raw)
{
    QList<StatusEntry> out;
    const QStringList parts = raw.split(QChar(0), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString &p = parts.at(i);
        if (p.size() < 4)
            continue;
        StatusEntry e;
        e.xy = p.left(2);
        e.path = p.mid(3);
        // A rename or copy is followed by its source path as a separate field.
        if (e.xy.contains(QLatin1Char('R')) || e.xy.contains(QLatin1Char('C')))
            ++i;
        out << e;
    }
    return out;
}

bool copyTree(const QString &from, const QString &to, QString *error)
{
    const QFileInfo fi(from);
    if (fi.isSymLink()) {
        QFile::remove(to);
        // The link's own text, not the absolute path it resolves to: a
        // relative link must stay relative, or it breaks when the old folder
        // is renamed.
        if (!QFile::link(fi.readSymLink(), to)) {
            if (error)
                *error = QStringLiteral("could not copy link ") + from;
            return false;
        }
        return true;
    }
    if (fi.isDir()) {
        QDir().mkpath(to);
        QDirIterator it(from, QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        while (it.hasNext()) {
            it.next();
            if (!copyTree(it.filePath(), to + QLatin1Char('/') + it.fileName(), error))
                return false;
        }
        return true;
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    if (!QFile::copy(from, to)) {
        if (error)
            *error = QStringLiteral("could not copy ") + from;
        return false;
    }
    return true;
}

} // namespace

QStringList Plan::report() const
{
    QStringList r;
    if (!ok) {
        r << QStringLiteral("Cannot convert: ") + problem;
        return r;
    }
    r << QStringLiteral("Folder:      %1 (branch %2)").arg(oldDir, cloneBranch);
    r << QStringLiteral("Worktree:    %1").arg(worktree);
    r << QStringLiteral("New branch:  %1 (the clone's commits, including any not pushed)").arg(branch);
    r << QStringLiteral("Changes:     %1 file(s) copied over, %2 deletion(s) applied")
             .arg(copyFiles.size()).arg(removeFiles.size());
    r << QStringLiteral("Notes:       %1 copied to %2").arg(notesFiles.isEmpty() ? QStringLiteral("nothing") : notesFiles.join(QStringLiteral(", ")), notesDir);
    if (!skippedIgnored.isEmpty())
        r << QStringLiteral("Left behind (build output, still in the old folder): %1").arg(skippedIgnored.join(QStringLiteral(", ")));
    r << QStringLiteral("Transcripts: %1 file(s) copied to %2").arg(transcripts.size()).arg(transcriptTo);
    if (!ownTranscriptFound)
        r << QStringLiteral("Warning:     this box's own transcript was not found, so it will start a new session.");
    r << QStringLiteral("Afterwards the old folder is renamed to %1.pre-worktree. Nothing is deleted.").arg(slug);
    r << QStringLiteral("Staged changes become unstaged changes in the worktree.");
    return r;
}

Plan plan(const BoxRecord &rec)
{
    Plan p;
    auto fail = [&p](const QString &why) {
        p.problem = why;
        return p;
    };

    if (rec.workspaceDir.isEmpty())
        return fail(QStringLiteral("this box does not work in a workspace subfolder"));
    if (!rec.worktreeDir.isEmpty())
        return fail(QStringLiteral("this box already works in a worktree"));

    const ProjectRecord project = ProjectRecord::forRoot(rec.targetDir);
    p.repo = project.primaryRepo;
    if (p.repo.isEmpty() || !Worktrees::isRepo(p.repo))
        return fail(QStringLiteral("the project has no git repository to make a worktree from"));

    p.slug = rec.workspaceDir;
    p.oldDir = QDir::cleanPath(rec.targetDir + QLatin1Char('/') + rec.workspaceDir);
    if (!QFileInfo(p.oldDir).isDir())
        return fail(p.oldDir + QStringLiteral(" does not exist"));

    // Must be a repository of its own. If the folder is just a directory
    // inside the project's repository, git reports the project as its top
    // level and there is nothing to convert.
    if (Worktrees::topLevel(p.oldDir) != p.oldDir)
        return fail(QStringLiteral("the folder is not an independent git clone"));

    const QString cloneUrl = normalizeUrl(Worktrees::originUrl(p.oldDir));
    const QString repoUrl = normalizeUrl(Worktrees::originUrl(p.repo));
    if (cloneUrl.isEmpty() || cloneUrl != repoUrl)
        return fail(QStringLiteral("the folder is a clone of a different repository (%1 and %2)")
                        .arg(cloneUrl.isEmpty() ? QStringLiteral("no origin") : cloneUrl,
                             repoUrl.isEmpty() ? QStringLiteral("no origin") : repoUrl));

    p.cloneBranch = Worktrees::currentBranch(p.oldDir);
    if (p.cloneBranch.isEmpty())
        return fail(QStringLiteral("the clone has a detached HEAD; check out a branch first"));
    if (!Worktrees::git(p.oldDir, {QStringLiteral("stash"), QStringLiteral("list")}).output.isEmpty())
        return fail(QStringLiteral("the clone has stashed changes, which would not carry over; apply or drop them first"));

    p.branch = Worktrees::branchFor(p.slug);
    p.worktree = Worktrees::worktreePath(p.repo, p.slug);
    p.notesDir = Worktrees::notesDir(rec.targetDir, p.slug);
    if (QFileInfo::exists(p.worktree))
        return fail(p.worktree + QStringLiteral(" already exists"));
    if (Worktrees::git(p.repo, {QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
                                QStringLiteral("refs/heads/") + p.branch}).ok)
        return fail(QStringLiteral("the branch %1 already exists in the repository").arg(p.branch));

    // Working-tree state to carry over.
    const auto st = Worktrees::git(p.oldDir, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                                              QStringLiteral("-z"), QStringLiteral("--untracked-files=all")},
                                  30000, /*trim=*/false);
    if (!st.ok)
        return fail(QStringLiteral("git status failed: ") + st.output);
    for (const StatusEntry &e : parseStatus(st.output)) {
        const bool topLevelNote = !e.path.contains(QLatin1Char('/')) && notesNames().contains(e.path)
                                  && e.xy == QLatin1String("??");
        if (topLevelNote) {
            p.notesFiles << e.path;
            continue;
        }
        if (e.xy.contains(QLatin1Char('D')) && !QFileInfo::exists(p.oldDir + QLatin1Char('/') + e.path))
            p.removeFiles << e.path;
        else
            p.copyFiles << e.path;
    }

    // A notes file the agent committed is part of the branch and stays in the
    // worktree, but the new prompt points at the notes directory, so it is
    // copied there as well.
    for (const QString &n : notesNames()) {
        if (!p.notesFiles.contains(n) && QFileInfo::exists(p.oldDir + QLatin1Char('/') + n))
            p.notesFiles << n;
    }

    // Ignored files (.env and the like) carry real state; build output does not.
    const auto ig = Worktrees::git(p.oldDir, {QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--others"),
                                              QStringLiteral("--ignored"), QStringLiteral("--exclude-standard"),
                                              QStringLiteral("--directory")},
                                  30000, /*trim=*/false);
    if (ig.ok) {
        for (const QString &path : ig.output.split(QChar(0), Qt::SkipEmptyParts)) {
            const QString leaf = QFileInfo(path.endsWith(QLatin1Char('/')) ? path.chopped(1) : path).fileName();
            if (Worktrees::isBuildOutputName(leaf))
                p.skippedIgnored << path;
            else
                p.copyFiles << path;
        }
    }

    // Tracked top-level notes files stay where git has them; untracked ones
    // were already routed to the notes directory above.

    // Transcripts: the box's own conversation was recorded under the project
    // root (that is where its container ran); anything started by running
    // claude inside the folder is under the folder's own directory.
    p.transcriptFrom = ConversationCatalog::projectDirFor(rec.targetDir);
    p.transcriptTo = ConversationCatalog::projectDirFor(p.worktree);
    QSet<QString> names;
    p.ownTranscriptFound = false; // nothing to resume unless a transcript is found below
    if (!rec.sessionUuid.isEmpty()) {
        const QString own = rec.sessionUuid + QStringLiteral(".jsonl");
        p.ownTranscriptFound = QFileInfo::exists(p.transcriptFrom + QLatin1Char('/') + own);
        if (p.ownTranscriptFound)
            names.insert(own);
    }
    const QString folderProject = ConversationCatalog::projectDirFor(p.oldDir);
    const QStringList own = QDir(folderProject).entryList({QStringLiteral("*.jsonl")}, QDir::Files);
    for (const QString &n : own)
        names.insert(n);
    p.transcripts = names.values();
    p.transcripts.sort();

    p.ok = true;
    return p;
}

bool execute(BoxRecord &rec, const Plan &plan, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (!plan.ok)
        return fail(plan.problem);

    // 1. A backup of the record, so the conversion can be undone by hand.
    const QString backupDir = QDir::homePath() + QStringLiteral("/.claude-box/backup");
    QDir().mkpath(backupDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    if (!QFile::copy(BoxRecord::knownDir() + QLatin1Char('/') + rec.name,
                     backupDir + QLatin1Char('/') + rec.name + QLatin1Char('.') + stamp))
        return fail(QStringLiteral("could not back up the box record"));

    // Undo for a failure before the record is rewritten: remove only what
    // this run created. The branch holds copies of the clone's commits, which
    // stay in the clone.
    bool createdBranch = false;
    bool createdWorktree = false;
    QStringList createdFiles; // notes and transcripts this run copied, removed on rollback
    auto rollback = [&]() {
        for (const QString &f : std::as_const(createdFiles))
            QFile::remove(f);
        if (createdWorktree)
            Worktrees::git(plan.repo, {QStringLiteral("worktree"), QStringLiteral("remove"),
                                       QStringLiteral("--force"), plan.worktree});
        if (createdBranch)
            Worktrees::git(plan.repo, {QStringLiteral("branch"), QStringLiteral("-D"), plan.branch});
    };

    // 2. The clone's commits, as a branch of the primary repository.
    QString exErr;
    if (!Worktrees::ensureExcluded(plan.repo, &exErr))
        return fail(QStringLiteral("could not update info/exclude: ") + exErr);
    const auto fetched = Worktrees::git(plan.repo, {QStringLiteral("fetch"), plan.oldDir,
                                                    plan.cloneBranch + QLatin1Char(':') + plan.branch}, 300000);
    if (!fetched.ok)
        return fail(QStringLiteral("could not fetch the clone's branch: ") + fetched.output);
    createdBranch = true;

    // 3. The worktree on that branch.
    QString wt;
    QString addErr;
    if (!Worktrees::add(plan.repo, plan.slug, &wt, &addErr)) {
        rollback();
        return fail(QStringLiteral("could not create the worktree: ") + addErr);
    }
    createdWorktree = true;

    // 4. Uncommitted work and ignored configuration.
    QString copyErr;
    for (const QString &rel : plan.copyFiles) {
        const QString src = plan.oldDir + QLatin1Char('/') + (rel.endsWith(QLatin1Char('/')) ? rel.chopped(1) : rel);
        const QString dst = plan.worktree + QLatin1Char('/') + (rel.endsWith(QLatin1Char('/')) ? rel.chopped(1) : rel);
        if (!QFileInfo(src).exists() && !QFileInfo(src).isSymLink())
            continue;
        if (!copyTree(src, dst, &copyErr)) {
            rollback();
            return fail(copyErr);
        }
    }
    for (const QString &rel : plan.removeFiles) {
        const QString target = plan.worktree + QLatin1Char('/') + rel;
        if (QFileInfo::exists(target) && !QFile::remove(target)) {
            rollback();
            return fail(QStringLiteral("could not delete ") + target);
        }
    }

    // 5. Notes go to the project directory.
    QDir().mkpath(plan.notesDir);
    for (const QString &n : plan.notesFiles) {
        const QString dst = (n == QLatin1String("INFRA.md")) ? Worktrees::infraFile(rec.targetDir)
                                                              : plan.notesDir + QLatin1Char('/') + n;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (!QFileInfo::exists(dst)) {
            if (!QFile::copy(plan.oldDir + QLatin1Char('/') + n, dst)) {
                rollback();
                return fail(QStringLiteral("could not copy ") + n);
            }
            createdFiles << dst;
        }
    }

    // 6. Transcripts: copied, never moved, then checked.
    QDir().mkpath(plan.transcriptTo);
    const QString folderProject = ConversationCatalog::projectDirFor(plan.oldDir);
    for (const QString &n : plan.transcripts) {
        QString src = plan.transcriptFrom + QLatin1Char('/') + n;
        if (!QFileInfo::exists(src))
            src = folderProject + QLatin1Char('/') + n;
        const QString dst = plan.transcriptTo + QLatin1Char('/') + n;
        if (QFileInfo::exists(dst))
            continue; // already there: keep whichever is larger would be a guess; keep it
        if (!QFile::copy(src, dst) || QFileInfo(src).size() != QFileInfo(dst).size()) {
            QFile::remove(dst);
            rollback();
            return fail(QStringLiteral("could not copy the transcript ") + n);
        }
        createdFiles << dst;
    }

    // 7. The record now points at the worktree.
    const BoxRecord before = rec;
    rec.worktreeDir = plan.worktree;
    rec.branch = plan.branch;
    rec.workspaceDir.clear();
    rec.movedFrom = plan.oldDir;
    if (!rec.save()) {
        rec = before;
        rollback();
        return fail(QStringLiteral("could not save the box record"));
    }

    // 8. Last: the old folder steps aside. The conversion is already complete
    // and recorded, so a failure here is reported but not rolled back.
    QString aside = plan.oldDir + QStringLiteral(".pre-worktree");
    if (QFileInfo::exists(aside))
        aside += QLatin1Char('.') + stamp;
    if (!QDir().rename(plan.oldDir, aside)) {
        if (error)
            *error = QStringLiteral("converted, but could not rename ") + plan.oldDir
                + QStringLiteral("; rename or remove it yourself");
        return true;
    }
    return true;
}

} // namespace WorktreeMigration
