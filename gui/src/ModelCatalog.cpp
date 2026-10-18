#include "ModelCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent>

#include <algorithm>

namespace ModelCatalog {

namespace {

const char *kCacheKey = "models/scan";
const char *kImageKey = "models/imageId";
const char *kTimeKey = "models/scanTime";
constexpr int kMaxAgeDays = 30;

// Used until a scan has run (no Docker, or the image is not built yet).
const QStringList kBaseline = {
    QStringLiteral("claude-fable-5-1"), QStringLiteral("claude-opus-5-5"),
    QStringLiteral("claude-sonnet-5-5"), QStringLiteral("claude-haiku-5-5"),
    QStringLiteral("claude-fable-5"), QStringLiteral("claude-opus-5"), QStringLiteral("claude-sonnet-5"),
    QStringLiteral("claude-opus-4-8"), QStringLiteral("claude-opus-4-7"), QStringLiteral("claude-opus-4-6"),
    QStringLiteral("claude-sonnet-4-6"), QStringLiteral("claude-haiku-4-5"),
};

struct Parsed {
    QString family;
    int major = 0;
    int minor = 0;
    bool ok = false;
};

Parsed parseId(QString id)
{
    // "claude-fable-5-1[1m]" is the same model with a 1M-token context.
    const int bracket = id.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        id.truncate(bracket);
    static const QRegularExpression re(QStringLiteral("^claude-(fable|opus|sonnet|haiku)-(\\d+)(?:-(\\d{1,2}))?$"));
    const auto m = re.match(id);
    Parsed p;
    if (!m.hasMatch())
        return p;
    p.family = m.captured(1);
    p.major = m.captured(2).toInt();
    p.minor = m.captured(3).toInt();
    p.ok = true;
    return p;
}

int familyRank(const QString &family)
{
    static const QStringList order = {QStringLiteral("fable"), QStringLiteral("opus"),
                                      QStringLiteral("sonnet"), QStringLiteral("haiku")};
    const int i = order.indexOf(family);
    return i < 0 ? order.size() : i;
}

// Grouped by family, largest first (fable, opus, sonnet, haiku), and newest
// first within a family.
void sortNewestFirst(QStringList &ids)
{
    std::stable_sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
        const Parsed pa = parseId(a), pb = parseId(b);
        if (pa.family != pb.family)
            return familyRank(pa.family) < familyRank(pb.family);
        if (pa.major != pb.major)
            return pa.major > pb.major;
        if (pa.minor != pb.minor)
            return pa.minor > pb.minor;
        return a < b; // plain ID before its [1m] form
    });
}

// Model options Claude Code recorded for the account (for example a model
// that is only offered to some plans).
QStringList accountModels()
{
    QStringList out;
    QFile f(QDir::homePath() + QStringLiteral("/.claude.json"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("additionalModelOptionsCache")).toArray()) {
        const QString id = v.toObject().value(QStringLiteral("value")).toString();
        if (!id.isEmpty())
            out << id;
    }
    return out;
}

QString imageId()
{
    QProcess p;
    p.start(QStringLiteral("docker"), {QStringLiteral("image"), QStringLiteral("inspect"), QStringLiteral("-f"),
                                       QStringLiteral("{{.Id}}"), QStringLiteral("claude-code")});
    if (!p.waitForFinished(8000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

} // namespace

QString scanCommand()
{
    // The program file is a native executable or a JavaScript bundle; either
    // way the IDs are plain text inside it. A match is followed by something
    // that cannot continue an ID, so dated forms such as claude-opus-4-20250514
    // do not produce a false "claude-opus-4".
    return QStringLiteral(
        "f=$(readlink -f \"$(command -v claude)\") && "
        "grep -aoE 'claude-(opus|sonnet|haiku|fable)-[0-9]+(-[0-9]{1,2})?([^0-9a-z-]|$)' \"$f\" "
        "| sed -E 's/[^0-9a-z-]+$//' | sort -u");
}

QStringList parseScan(const QString &scanOutput)
{
    QStringList ids;
    for (const QString &line : scanOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString id = line.trimmed();
        const Parsed p = parseId(id);
        if (!p.ok || ids.contains(id))
            continue;
        // Current models only: 4.5 and later, and any 5.x. The older IDs the
        // program still mentions for compatibility are not worth offering.
        if (p.major < 4 || (p.major == 4 && p.minor < 5))
            continue;
        ids << id;
    }
    sortNewestFirst(ids);
    return ids;
}

QStringList known()
{
    QStringList ids = QSettings().value(kCacheKey).toStringList();
    if (ids.isEmpty())
        ids = kBaseline;
    for (const QString &id : accountModels()) {
        if (!ids.contains(id))
            ids << id;
    }
    sortNewestFirst(ids);
    return ids;
}

void refreshIfStale()
{
    (void)QtConcurrent::run([]() {
        const QString id = imageId();
        if (id.isEmpty())
            return; // no Docker or no image: keep what is cached

        QSettings s;
        const QDateTime scanned = QDateTime::fromSecsSinceEpoch(s.value(kTimeKey, 0).toLongLong());
        const bool fresh = s.value(kImageKey).toString() == id && scanned.daysTo(QDateTime::currentDateTime()) < kMaxAgeDays
                           && !s.value(kCacheKey).toStringList().isEmpty();
        if (fresh)
            return;

        QProcess p;
        p.start(QStringLiteral("docker"), {QStringLiteral("run"), QStringLiteral("--rm"), QStringLiteral("--entrypoint"),
                                           QStringLiteral("sh"), QStringLiteral("claude-code"), QStringLiteral("-c"),
                                           scanCommand()});
        if (!p.waitForFinished(120000) || p.exitCode() != 0)
            return;
        const QStringList ids = parseScan(QString::fromUtf8(p.readAllStandardOutput()));
        if (ids.isEmpty())
            return; // an unexpected program layout: better to keep the old list
        QSettings w;
        w.setValue(kCacheKey, ids);
        w.setValue(kImageKey, id);
        w.setValue(kTimeKey, QDateTime::currentSecsSinceEpoch());
    });
}

} // namespace ModelCatalog
