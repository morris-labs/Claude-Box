#include "ClaudeCodeUpdateChecker.h"

#include "DockerBackend.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QVersionNumber>
#include <QtConcurrent>
#include <QtDebug>

namespace {
const QString kLastCheckKey =
    QStringLiteral("claudeCodeUpdateChecker/lastCheck");
const QString kVersionKey =
    QStringLiteral("claudeCode/version");
const QString kVersionCachedAtKey =
    QStringLiteral("claudeCode/versionCachedAt");
const QString kNpmUrl =
    QStringLiteral("https://registry.npmjs.org/@anthropic-ai/claude-code/latest");
constexpr qint64 kCooldownSecs  = 24 * 60 * 60;
constexpr qint64 kVersionTtlSecs = 24 * 60 * 60;
}

ClaudeCodeUpdateChecker::ClaudeCodeUpdateChecker(const DockerBackend *docker,
                                                 QNetworkAccessManager *nam,
                                                 QObject *parent)
    : QObject(parent), m_docker(docker), m_nam(nam)
{}

void ClaudeCodeUpdateChecker::checkInBackground()
{
    QSettings s;
    const qint64 last = s.value(kLastCheckKey, 0LL).toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - last < kCooldownSecs)
        return;
    doCheck(false);
}

void ClaudeCodeUpdateChecker::checkNow()
{
    doCheck(true);
}

void ClaudeCodeUpdateChecker::cancelAndWait()
{
    if (m_pendingFuture.isRunning())
        m_pendingFuture.waitForFinished();
}

void ClaudeCodeUpdateChecker::doCheck(bool emitUpToDate)
{
    // Guard against concurrent calls (e.g. the startup timer and a manual
    // Help > Check action firing before the first reply lands).
    if (m_checkInProgress)
        return;
    m_checkInProgress = true;

    // Read the docker image version cache on the GUI thread. QSettings is
    // not thread-safe, so all reads and writes stay on the GUI thread; only
    // the blocking docker run itself is offloaded to the thread pool.
    QSettings s;
    const qint64 cachedAt =
        s.value(kVersionCachedAtKey, 0LL).toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - cachedAt < kVersionTtlSecs) {
        const QString installed = s.value(kVersionKey).toString();
        proceedWithNpmCheck(installed, emitUpToDate);
        return;
    }

    // Cache expired -- spawn a container to read the version file. Store
    // the future so cancelAndWait() can block on it in the MainWindow
    // destructor before m_docker (a value member of MainWindow) is freed.
    const DockerBackend *docker = m_docker;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, emitUpToDate] {
        watcher->deleteLater();
        const QString installed = watcher->result();

        // Write the version cache on the GUI thread (safe for QSettings).
        QSettings s;
        s.setValue(kVersionKey, installed);
        s.setValue(kVersionCachedAtKey, QDateTime::currentSecsSinceEpoch());

        proceedWithNpmCheck(installed, emitUpToDate);
    });

    m_pendingFuture = QtConcurrent::run([docker] {
        return docker->claudeCodeImageVersion();
    });
    watcher->setFuture(m_pendingFuture);
}

void ClaudeCodeUpdateChecker::proceedWithNpmCheck(const QString &installed,
                                                   bool emitUpToDate)
{
    // Record the cooldown timestamp now that the docker step has either
    // completed or was served from cache.
    QSettings s;
    s.setValue(kLastCheckKey, QDateTime::currentSecsSinceEpoch());

    const QUrl npmUrl(kNpmUrl);
    QNetworkRequest req(npmUrl);
    req.setRawHeader("User-Agent",
        QStringLiteral("claude-box-gui/%1")
            .arg(QApplication::applicationVersion()).toUtf8());

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, installed, emitUpToDate] {
        reply->deleteLater();
        m_checkInProgress = false;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "ClaudeCodeUpdateChecker: npm error:"
                     << reply->errorString();
            if (emitUpToDate)
                emit checkFailed(reply->errorString());
            return;
        }

        QJsonParseError pe;
        const QJsonDocument doc =
            QJsonDocument::fromJson(reply->readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            if (emitUpToDate)
                emit checkFailed(
                    QStringLiteral("Unexpected response from npm registry."));
            return;
        }

        const QString latest =
            doc.object().value(QStringLiteral("version")).toString();
        if (latest.isEmpty()) {
            if (emitUpToDate)
                emit checkFailed(
                    QStringLiteral("Unexpected response from npm registry."));
            return;
        }

        // An empty installed version means the image either hasn't been
        // built yet or was built before /etc/claude-code-version was added.
        // Treat it as out of date either way.
        if (installed.isEmpty()) {
            emit updateAvailable(installed, latest);
            return;
        }

        const QVersionNumber remoteVer =
            QVersionNumber::fromString(latest);
        const QVersionNumber localVer =
            QVersionNumber::fromString(installed);

        if (remoteVer.isNull() || localVer.isNull()) {
            qDebug() << "ClaudeCodeUpdateChecker: could not parse versions:"
                     << installed << latest;
            if (emitUpToDate)
                emit checkFailed(
                    QStringLiteral("Could not compare version numbers."));
            return;
        }

        if (QVersionNumber::compare(remoteVer, localVer) > 0)
            emit updateAvailable(installed, latest);
        else if (emitUpToDate)
            emit upToDate();
    });
}
