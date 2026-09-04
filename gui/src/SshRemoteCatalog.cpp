#include "SshRemoteCatalog.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

QString SshRemoteCatalog::catalogDir()
{
    return QDir::homePath() + "/.claude-box/ssh_remotes";
}

QString SshRemoteCatalog::sanitizeName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    bool lastWasDash = false;
    for (const QChar &c : name.toLower()) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok) {
            out += c;
            lastWasDash = (c == '-');
        } else if (!lastWasDash) {
            out += '-';
            lastWasDash = true;
        }
    }
    while (out.endsWith('-'))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("remote") : out;
}

QList<SshRemote> SshRemoteCatalog::loadAll()
{
    QList<SshRemote> remotes;
    QDir dir(catalogDir());
    if (!dir.exists())
        return remotes;

    const QStringList names = dir.entryList(QDir::Files, QDir::Name);
    for (const QString &fileName : names) {
        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        SshRemote remote;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int eq = line.indexOf('=');
            if (eq < 0)
                continue;
            const QString key = line.left(eq);
            const QString value = line.mid(eq + 1);
            if (key == "name")
                remote.name = value;
            else if (key == "host")
                remote.host = value;
            else if (key == "identity")
                remote.identity = value;
            else if (key == "forward")
                remote.forwards.append(value);
        }
        if (!remote.name.isEmpty())
            remotes.append(remote);
    }
    return remotes;
}

SshRemote SshRemoteCatalog::load(const QString &name)
{
    for (const SshRemote &remote : loadAll()) {
        if (remote.name == name)
            return remote;
    }
    return SshRemote();
}

QString SshRemoteCatalog::collidingName(const QString &name, const QString &oldName)
{
    const QString wanted = sanitizeName(name);
    for (const SshRemote &remote : loadAll()) {
        if (remote.name == name || remote.name == oldName)
            continue;
        if (sanitizeName(remote.name) == wanted)
            return remote.name;
    }
    return QString();
}

bool SshRemoteCatalog::save(const SshRemote &remote, const QString &oldName)
{
    if (remote.name.trimmed().isEmpty())
        return false;

    QDir().mkpath(catalogDir());

    if (!oldName.isEmpty() && oldName != remote.name)
        remove(oldName); // renamed -- don't leave the old file behind

    QFile file(catalogDir() + "/" + sanitizeName(remote.name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QTextStream out(&file);
    out << "name=" << remote.name << '\n';
    out << "host=" << remote.host << '\n';
    if (!remote.identity.isEmpty())
        out << "identity=" << remote.identity << '\n';
    for (const QString &f : remote.forwards)
        out << "forward=" << f << '\n';

    return true;
}

bool SshRemoteCatalog::remove(const QString &name)
{
    QFile file(catalogDir() + "/" + sanitizeName(name));
    return !file.exists() || file.remove();
}
