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
const QString kNpmUrl =
    QStringLiteral("https://registry.npmjs.org/@anthropic-ai/claude-code/latest");
constexpr qint64 kCooldownSecs = 24 * 60 * 60;
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

void ClaudeCodeUpdateChecker::doCheck(bool emitUpToDate)
{
    // claudeCodeImageVersion() spawns a docker container (slow path), so it
    // must not run on the GUI thread. Run it on the thread pool; when it
    // finishes, proceed with the npm fetch back on the GUI thread.
    const DockerBackend *docker = m_docker;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, emitUpToDate] {
        watcher->deleteLater();
        const QString installed = watcher->result();

        // Update the cooldown timestamp regardless of whether the image is
        // current -- the docker run already ran, so there is no point in
        // repeating it within the cooldown window.
        QSettings s;
        s.setValue(kLastCheckKey, QDateTime::currentSecsSinceEpoch());

        // Fetch the latest version from npm.
        const QUrl npmUrl(kNpmUrl);
        QNetworkRequest req(npmUrl);
        req.setRawHeader("User-Agent",
            QStringLiteral("claude-box-gui/%1")
                .arg(QApplication::applicationVersion()).toUtf8());

        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, installed, emitUpToDate] {
            reply->deleteLater();

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

            // An empty installed version means the image either wasn't built
            // yet, or was built before /etc/claude-code-version was added.
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
    });

    watcher->setFuture(QtConcurrent::run([docker] {
        return docker->claudeCodeImageVersion();
    }));
}
