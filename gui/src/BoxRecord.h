#pragma once

#include <QString>
#include <QStringList>
#include <QList>

// Persistent record for one claude-box container, stored as a simple
// key=value file under ~/.claude-box/known/<name>. This is the only
// file-based state the app keeps -- docker itself is the live "is it
// running" ground truth (see DockerBackend), so there's no separate
// crash-signal file the way the old bash tooling had one.
struct BoxRecord {
    QString name;             // container name, also the record's filename
    QString targetDir;
    QString sessionUuid;
    QString conversationName;
    bool yolo = false;
    bool rc = false;
    QStringList ports;        // "HOST:CONTAINER", repeatable
    QStringList dirs;         // "HOSTPATH:CONTAINERPATH", repeatable

    bool isValid() const { return !name.isEmpty(); }

    // Base ~/.claude-box/known directory (created on demand by save()).
    static QString knownDir();

    // Every record currently on disk under knownDir().
    static QList<BoxRecord> loadAll();

    // A single record by container name. Returns an invalid (isValid()
    // == false) BoxRecord if none exists.
    static BoxRecord load(const QString &name);

    // Writes this record to knownDir()/name, creating the directory if
    // needed. Returns false on I/O failure.
    bool save() const;

    // Deletes the on-disk record (used by Purge/Remove). Returns true if
    // the file didn't exist or was removed successfully.
    bool remove() const;
};
