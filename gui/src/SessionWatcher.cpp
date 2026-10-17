#include "SessionWatcher.h"

#include "BoxRecord.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
constexpr int kDebounceMs = 250;
}

SessionWatcher::SessionWatcher(QObject *parent)
    : QObject(parent)
{
    // Events arrive in bursts (a rewrite is often several writes, and
    // updatedAt changes on every status flip), so they only schedule a scan.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &SessionWatcher::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
}

QString SessionWatcher::sharedSessionsDir()
{
    return QDir::homePath() + QStringLiteral("/.claude/sessions");
}

void SessionWatcher::setRunning(const QStringList &names)
{
    m_running = QSet<QString>(names.begin(), names.end());
    scan();
}

QHash<QString, SessionWatcher::Entry> SessionWatcher::readEntries(const QString &dir, QStringList *filesOut)
{
    QHash<QString, Entry> entries;
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        if (filesOut)
            *filesOut << fi.absoluteFilePath();

        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        // A half-written file fails to parse; it is read again on the next event.
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        if (obj.isEmpty())
            continue;

        Entry e;
        e.pid = obj.value("pid").toInt();
        e.pidDomain = obj.value("pidDomain").toString();
        e.procStart = obj.value("procStart").toString();
        e.sessionId = obj.value("sessionId").toString();
        e.name = obj.value("name").toString();
        e.entrypoint = obj.value("entrypoint").toString();
        e.updatedAt = static_cast<qint64>(obj.value("updatedAt").toDouble());
        e.nameIsUser = obj.value("nameSource").toString() == QLatin1String("user");
        if (e.pid > 0 && !e.sessionId.isEmpty())
            entries.insert(e.key(), e);
    }
    return entries;
}

bool SessionWatcher::heldByOtherBox(const QString &sessionId, const QString &boxName)
{
    for (const BoxRecord &rec : BoxRecord::loadAll()) {
        if (rec.name != boxName && rec.sessionUuid == sessionId)
            return true;
    }
    return false;
}

// QFileSystemWatcher reports a change to a file's contents only for paths
// added individually, and an atomic replace (write new file, rename over the
// old) drops the path, so the set is topped up after every scan.
void SessionWatcher::watchPaths(const QStringList &dirs, const QStringList &files)
{
    QStringList addDirs;
    const QStringList haveDirs = m_watcher.directories();
    for (const QString &d : dirs) {
        if (QFileInfo(d).isDir() && !haveDirs.contains(d))
            addDirs << d;
    }
    if (!addDirs.isEmpty())
        m_watcher.addPaths(addDirs);

    const QStringList have = m_watcher.files();
    QStringList add;
    for (const QString &f : files) {
        if (!have.contains(f))
            add << f;
    }
    if (!add.isEmpty())
        m_watcher.addPaths(add);
}

void SessionWatcher::scan()
{
    QStringList watchDirs = {sharedSessionsDir()};
    QStringList watchFiles;

    // Directories of files shared by several boxes (the host's own sessions
    // directory, or a shared container's), read lazily and once per scan.
    QHash<QString, QHash<QString, Entry>> sharedDirs;
    auto readShared = [&](const QString &dir) -> const QHash<QString, Entry> & {
        auto it = sharedDirs.find(dir);
        if (it == sharedDirs.end()) {
            watchDirs << dir;
            it = sharedDirs.insert(dir, readEntries(dir, &watchFiles));
        }
        return it.value();
    };

    // A box that stopped has nothing to follow; it is read afresh when it
    // runs again.
    for (auto it = m_bindings.begin(); it != m_bindings.end();) {
        it = m_running.contains(it.key()) ? std::next(it) : m_bindings.erase(it);
    }

    QSet<QString> claimed; // shared-directory processes already taken by a box
    for (const Binding &b : std::as_const(m_bindings)) {
        if (!b.key.isEmpty())
            claimed.insert(b.key);
    }

    for (const QString &boxName : std::as_const(m_running)) {
        BoxRecord rec = BoxRecord::load(boxName);
        if (!rec.isValid())
            continue;

        Binding &binding = m_bindings[boxName];
        const Entry *live = nullptr;
        bool ownDir = false;

        // The box's own directory: whatever is in it is this box's, and the
        // newest file is the live process.
        const QString boxDir = BoxRecord::sessionsDir(boxName);
        watchDirs << boxDir;
        const QHash<QString, Entry> mine = readEntries(boxDir, &watchFiles);
        if (!mine.isEmpty()) {
            ownDir = true;
            for (const Entry &e : mine) {
                if (!live || e.updatedAt > live->updatedAt)
                    live = &e;
            }
        } else {
            // Started before boxes had their own directory: find the process
            // by the recorded session id, but only when exactly one live-looking
            // candidate has it. Desktop-app entries share ids with the sessions
            // they display, and a killed container leaves stale files, so
            // several matches are expected and settled in favour of the newest
            // command-line process.
            // Boxes in a shared container have one directory for the whole
            // container, where pids are unique; everything else shares the
            // host's.
            const QHash<QString, Entry> &all =
                readShared(rec.isShared() ? BoxRecord::sessionsDir(rec.containerName()) : sharedSessionsDir());
            if (!binding.key.isEmpty()) {
                const auto found = all.constFind(binding.key);
                if (found != all.constEnd())
                    live = &found.value();
                else
                    binding = Binding(); // the process is gone; find it again
            }
            if (!live && !rec.sessionUuid.isEmpty()) {
                const Entry *best = nullptr;
                for (const Entry &e : all) {
                    if (e.sessionId != rec.sessionUuid || claimed.contains(e.key())
                        || e.entrypoint == QLatin1String("claude-desktop"))
                        continue;
                    if (!best || e.updatedAt > best->updatedAt)
                        best = &e;
                }
                if (best) {
                    live = best;
                    binding.key = best->key();
                    binding.seen = true;
                    binding.lastSession = best->sessionId;
                    binding.lastName = best->nameIsUser ? best->name : rec.conversationName;
                    claimed.insert(best->key());
                    continue; // matched by the record itself, nothing to change yet
                }
            }
        }
        if (!live)
            continue;

        // Only changes since the previous reading are applied, so an Edit Box
        // change that is waiting for a restart is not reverted by the
        // still-running old process. A box's own directory is certain, so its
        // first reading is taken as the truth (a /clear may have happened
        // while this app was closed).
        const bool first = !binding.seen;
        bool changed = false;
        QString seenSession = binding.lastSession;
        QString seenName = binding.lastName;

        if (first ? ownDir : live->sessionId != binding.lastSession) {
            seenSession = live->sessionId;
            // Another box holding the new id would mean two records for one
            // transcript; leave this record alone rather than pick a side.
            if (live->sessionId != rec.sessionUuid && !heldByOtherBox(live->sessionId, boxName)) {
                rec.sessionUuid = live->sessionId;
                changed = true;
            }
        } else if (first) {
            seenSession = live->sessionId;
        }

        // An auto-generated name (no nameSource) is not something the user
        // chose, so it never replaces the one they did.
        if (live->nameIsUser && !live->name.isEmpty()
            && (first ? ownDir : live->name != binding.lastName)) {
            seenName = live->name;
            if (live->name != rec.conversationName) {
                rec.conversationName = live->name;
                // The transcript carries this name now, so startBox() should
                // treat the two as in step.
                rec.transcriptSyncedName = live->name;
                changed = true;
            }
        } else if (first) {
            seenName = rec.conversationName;
        }

        if (changed && !rec.save())
            continue; // keep the old state so the next scan retries
        binding.seen = true;
        binding.lastSession = seenSession;
        binding.lastName = seenName;
        if (changed)
            emit boxChanged(boxName);
    }

    watchPaths(watchDirs, watchFiles);
}
