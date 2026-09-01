#include "BoxRecord.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>

QString BoxRecord::knownDir()
{
    return QDir::homePath() + "/.claude-box/known";
}

QList<BoxRecord> BoxRecord::loadAll()
{
    QList<BoxRecord> records;
    QDir dir(knownDir());
    if (!dir.exists())
        return records;

    const QStringList names = dir.entryList(QDir::Files, QDir::Name);
    for (const QString &name : names) {
        BoxRecord rec = load(name);
        if (rec.isValid())
            records.append(rec);
    }
    return records;
}

BoxRecord BoxRecord::load(const QString &name)
{
    BoxRecord rec;
    QFile file(knownDir() + "/" + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rec; // invalid: name left empty

    // Keyed by remote index while scanning (lines for one remote needn't
    // be contiguous), turned into rec.sshRemotes in index order once the
    // whole file's been read -- QMap already iterates that way. A
    // pre-multi-remote record has no index at all (ssh_host=/ssh_forward=
    // with no "N|" prefix); that's read as remote 0, same as if it had
    // been written "0|..." all along.
    QMap<int, SshRemote> remotesByIndex;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq);
        const QString value = line.mid(eq + 1);

        if (key == "target_dir")
            rec.targetDir = value;
        else if (key == "session_uuid")
            rec.sessionUuid = value;
        else if (key == "conversation_name")
            rec.conversationName = value;
        else if (key == "yolo")
            rec.skipPermissions = (value == "1");
        else if (key == "model")
            rec.model = value;
        else if (key == "workspace")
            rec.workspaceDir = value;
        else if (key == "effort")
            rec.effort = value;
        else if (key == "port")
            rec.ports.append(value);
        else if (key == "dir")
            rec.dirs.append(value);
        else if (key == "ssh_remote") {
            // "N|host" -- no legacy form of this key exists (a
            // pre-multi-remote record never had ssh_remote= at all).
            const int bar = value.indexOf('|');
            if (bar >= 0)
                remotesByIndex[value.left(bar).toInt()].host = value.mid(bar + 1);
        } else if (key == "ssh_identity") {
            const int bar = value.indexOf('|');
            if (bar >= 0)
                remotesByIndex[value.left(bar).toInt()].identity = value.mid(bar + 1);
            else
                remotesByIndex[0].identity = value; // legacy: flat ssh_identity=, always remote 0
        } else if (key == "ssh_forward") {
            const int bar = value.indexOf('|');
            if (bar >= 0)
                remotesByIndex[value.left(bar).toInt()].forwards.append(value.mid(bar + 1));
            else
                remotesByIndex[0].forwards.append(value); // legacy: flat ssh_forward=, always remote 0
        } else if (key == "ssh_host") {
            remotesByIndex[0].host = value; // legacy: flat ssh_host=, always remote 0
        }
    }

    rec.sshRemotes = remotesByIndex.values(); // QMap::values() is already key-ordered
    rec.name = name; // only set once the file was actually readable
    return rec;
}

bool BoxRecord::save() const
{
    if (name.isEmpty())
        return false;

    QDir().mkpath(knownDir());

    QFile file(knownDir() + "/" + name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QTextStream out(&file);
    out << "target_dir=" << targetDir << '\n';
    out << "session_uuid=" << sessionUuid << '\n';
    out << "conversation_name=" << conversationName << '\n';
    out << "yolo=" << (skipPermissions ? "1" : "0") << '\n';
    if (!model.isEmpty())
        out << "model=" << model << '\n';
    if (!workspaceDir.isEmpty())
        out << "workspace=" << workspaceDir << '\n';
    if (!effort.isEmpty())
        out << "effort=" << effort << '\n';
    for (const QString &p : ports)
        out << "port=" << p << '\n';
    for (const QString &d : dirs)
        out << "dir=" << d << '\n';
    for (int i = 0; i < sshRemotes.size(); ++i) {
        const SshRemote &r = sshRemotes.at(i);
        out << "ssh_remote=" << i << "|" << r.host << '\n';
        if (!r.identity.isEmpty())
            out << "ssh_identity=" << i << "|" << r.identity << '\n';
        for (const QString &f : r.forwards)
            out << "ssh_forward=" << i << "|" << f << '\n';
    }

    return true;
}

bool BoxRecord::remove() const
{
    if (name.isEmpty())
        return true;
    QFile file(knownDir() + "/" + name);
    if (!file.exists())
        return true;
    return file.remove();
}
