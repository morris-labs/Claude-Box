#include "BoxRecord.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
        else if (key == "effort")
            rec.effort = value;
        else if (key == "port")
            rec.ports.append(value);
        else if (key == "dir")
            rec.dirs.append(value);
    }

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
    if (!effort.isEmpty())
        out << "effort=" << effort << '\n';
    for (const QString &p : ports)
        out << "port=" << p << '\n';
    for (const QString &d : dirs)
        out << "dir=" << d << '\n';

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
