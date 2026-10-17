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

QString SessionWatcher::sessionsDir()
{
    return QDir::homePath() + QStringLiteral("/.claude/sessions");
}

void SessionWatcher::setRunning(const QStringList &names)
{
    m_running = QSet<QString>(names.begin(), names.end());
    scan();
}

QHash<QString, SessionWatcher::Entry> SessionWatcher::readEntries(QStringList *filesOut)
{
    QHash<QString, Entry> entries;
    const QFileInfoList files = QDir(sessionsDir()).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
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
void SessionWatcher::watchFiles(const QStringList &files)
{
    const QString dir = sessionsDir();
    if (QFileInfo(dir).isDir() && !m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);

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
    QStringList files;
    const QHash<QString, Entry> entries = readEntries(&files);
    watchFiles(files);

    // A box that stopped has no process to follow; it is matched afresh when
    // it runs again.
    for (auto it = m_bindings.begin(); it != m_bindings.end();) {
        it = m_running.contains(it.key()) ? std::next(it) : m_bindings.erase(it);
    }

    QSet<QString> claimed;
    for (const Binding &b : std::as_const(m_bindings))
        claimed.insert(b.key);

    for (const QString &boxName : std::as_const(m_running)) {
        BoxRecord rec = BoxRecord::load(boxName);
        if (!rec.isValid())
            continue;

        auto bound = m_bindings.find(boxName);
        if (bound == m_bindings.end()) {
            // Session ids are unique, so exactly one unclaimed process with the
            // recorded id is the box's claude. Anything else stays unbound
            // until it is unambiguous.
            if (rec.sessionUuid.isEmpty())
                continue;
            const Entry *match = nullptr;
            int count = 0;
            for (const Entry &e : entries) {
                if (e.sessionId == rec.sessionUuid && !claimed.contains(e.key())) {
                    match = &e;
                    ++count;
                }
            }
            if (count != 1)
                continue;

            Binding b;
            b.key = match->key();
            b.lastSession = match->sessionId;
            b.lastName = match->nameIsUser ? match->name : rec.conversationName;
            m_bindings.insert(boxName, b);
            claimed.insert(b.key);
            continue;
        }

        const auto found = entries.constFind(bound->key);
        if (found == entries.constEnd()) {
            m_bindings.erase(bound); // the process is gone; rebind on the next scan
            continue;
        }
        const Entry &e = found.value();

        bool changed = false;
        // What the binding should remember once the record is safely saved.
        // Advancing it earlier would make a failed save permanent: the next
        // scan would see no change and never try again.
        QString seenSession = bound->lastSession;
        QString seenName = bound->lastName;

        if (e.sessionId != bound->lastSession) {
            seenSession = e.sessionId;
            // Another box holding the new id would mean two records for one
            // transcript; leave this record alone rather than pick a side.
            if (e.sessionId != rec.sessionUuid && !heldByOtherBox(e.sessionId, boxName)) {
                rec.sessionUuid = e.sessionId;
                changed = true;
            }
        }

        // An auto-generated name (no nameSource) is not something the user
        // chose, so it never replaces the one they did.
        if (e.nameIsUser && !e.name.isEmpty() && e.name != bound->lastName) {
            seenName = e.name;
            if (e.name != rec.conversationName) {
                rec.conversationName = e.name;
                // The transcript carries this name now, so startBox() should
                // treat the two as in step.
                rec.transcriptSyncedName = e.name;
                changed = true;
            }
        }

        if (changed && !rec.save())
            continue; // keep the old binding state so the next scan retries
        bound->lastSession = seenSession;
        bound->lastName = seenName;
        if (changed)
            emit boxChanged(boxName);
    }
}
