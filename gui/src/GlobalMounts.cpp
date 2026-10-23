#include "GlobalMounts.h"

#include "ContainerPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

namespace {
QString containerSide(const QString &spec)
{
    QString hostRaw, containerPath;
    ContainerPaths::splitMountSpec(spec, hostRaw, containerPath);
    if (containerPath.isEmpty())
        containerPath = ContainerPaths::hostToContainer(QFileInfo(hostRaw).absoluteFilePath());
    return QDir::cleanPath(containerPath);
}
} // namespace

QString GlobalMounts::filePath()
{
    return QDir::homePath() + QStringLiteral("/.claude-box/global");
}

QStringList GlobalMounts::load()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList dirs;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QStringLiteral("dir=")) && line.size() > 4)
            dirs << line.mid(4);
    }
    return dirs;
}

bool GlobalMounts::save(const QStringList &dirs)
{
    QDir().mkpath(QFileInfo(filePath()).path());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    for (const QString &d : dirs)
        out << "dir=" << d << '\n';
    out.flush();
    return file.commit();
}

QStringList GlobalMounts::mergedWith(const QStringList &own)
{
    QStringList out = own;
    QSet<QString> used;
    for (const QString &d : own)
        used.insert(containerSide(d));
    for (const QString &d : load()) {
        const QString c = containerSide(d);
        if (used.contains(c))
            continue;
        used.insert(c);
        out << d;
    }
    return out;
}
