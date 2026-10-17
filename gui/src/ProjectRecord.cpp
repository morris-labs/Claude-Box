#include "ProjectRecord.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {
// Same ASCII-only collapsing as the box-name sanitizers, so an id is stable
// across locales and always a legal filename.
QString slug(const QString &s)
{
    QString out;
    bool lastDash = true;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        const bool alnum = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
        if (alnum) {
            out += c;
            lastDash = false;
        } else if (!lastDash) {
            out += QLatin1Char('-');
            lastDash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}
} // namespace

QString ProjectRecord::projectsDir()
{
    return QDir::homePath() + QStringLiteral("/.claude-box/projects");
}

QString ProjectRecord::normalizeRoot(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(dir.trimmed());
}

QString ProjectRecord::idForRoot(const QString &root)
{
    return slug(normalizeRoot(root));
}

QString ProjectRecord::name() const
{
    if (!displayName.isEmpty())
        return displayName;
    const QString base = QFileInfo(root).fileName();
    return base.isEmpty() ? root : base;
}

QString ProjectRecord::detectPrimaryRepo(const QString &root)
{
    // .git is a directory in a normal clone and a file in a worktree or a
    // submodule; either one marks a working tree.
    return QFileInfo::exists(root + QStringLiteral("/.git")) ? root : QString();
}

ProjectRecord ProjectRecord::forRoot(const QString &rootIn)
{
    ProjectRecord rec;
    const QString root = normalizeRoot(rootIn);
    if (root.isEmpty())
        return rec;

    rec.id = idForRoot(root);
    rec.root = root;
    rec.primaryRepo = detectPrimaryRepo(root);

    QFile file(projectsDir() + QLatin1Char('/') + rec.id);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rec;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq);
        const QString value = line.mid(eq + 1);
        if (key == QLatin1String("name"))
            rec.displayName = value;
        else if (key == QLatin1String("primary_repo"))
            rec.primaryRepo = value;
    }
    return rec;
}

QList<ProjectRecord> ProjectRecord::loadAll()
{
    QList<ProjectRecord> out;
    const QStringList ids = QDir(projectsDir()).entryList(QDir::Files, QDir::Name);
    for (const QString &id : ids) {
        QFile file(projectsDir() + QLatin1Char('/') + id);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QString root;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(QLatin1String("root=")))
                root = line.mid(5);
        }
        if (root.isEmpty())
            continue;
        ProjectRecord rec = forRoot(root);
        if (rec.id == id)
            out.append(rec);
    }
    return out;
}

bool ProjectRecord::save() const
{
    if (!isValid())
        return false;
    QDir().mkpath(projectsDir());
    QFile file(projectsDir() + QLatin1Char('/') + id);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream out(&file);
    out << "root=" << root << '\n';
    if (!displayName.isEmpty())
        out << "name=" << displayName << '\n';
    if (!primaryRepo.isEmpty())
        out << "primary_repo=" << primaryRepo << '\n';
    return true;
}
