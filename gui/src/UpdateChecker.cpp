#include "UpdateChecker.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QVersionNumber>
#include <QtDebug>

namespace {
const QString kLastCheckKey      = QStringLiteral("updateChecker/lastCheck");
const QString kReleasesUrl       =
    QStringLiteral("https://api.github.com/repos/morris-labs/Claude-Box/releases/latest");
constexpr qint64 kCooldownSecs   = 24 * 60 * 60;
}

UpdateChecker::UpdateChecker(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_nam(nam)
{}

void UpdateChecker::checkInBackground()
{
    QSettings s;
    const qint64 last = s.value(kLastCheckKey, 0LL).toLongLong();
    const qint64 now  = QDateTime::currentSecsSinceEpoch();
    if (now - last < kCooldownSecs)
        return;
    doCheck(false);
}

void UpdateChecker::checkNow()
{
    doCheck(true);
}

void UpdateChecker::doCheck(bool emitUpToDate)
{
    // If a request is already in flight, record whether this caller wants
    // upToDate/checkFailed feedback; the pending reply will honor it.
    if (m_pendingCheck) {
        m_pendingEmitUpToDate = m_pendingEmitUpToDate || emitUpToDate;
        return;
    }
    m_pendingCheck        = true;
    m_pendingEmitUpToDate = false;

    const QUrl apiUrl(kReleasesUrl);
    QNetworkRequest req(apiUrl);
    req.setRawHeader("User-Agent",
        QStringLiteral("claude-box-gui/%1")
            .arg(QApplication::applicationVersion()).toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(10000);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, emitUpToDate] {
        reply->deleteLater();
        m_pendingCheck = false;
        const bool shouldNotify = emitUpToDate || m_pendingEmitUpToDate;
        m_pendingEmitUpToDate   = false;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "UpdateChecker: network error:" << reply->errorString();
            if (shouldNotify)
                emit checkFailed(reply->errorString());
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            qDebug() << "UpdateChecker: bad JSON:" << parseErr.errorString();
            if (shouldNotify)
                emit checkFailed(QStringLiteral("Unexpected response from GitHub."));
            return;
        }

        const QJsonObject obj = doc.object();
        QString tag = obj.value(QStringLiteral("tag_name")).toString();
        const QString url = obj.value(QStringLiteral("html_url")).toString();

        if (tag.isEmpty() || url.isEmpty()) {
            qDebug() << "UpdateChecker: missing tag_name or html_url";
            if (shouldNotify)
                emit checkFailed(QStringLiteral("Unexpected response from GitHub."));
            return;
        }

        // Strip leading 'v' so QVersionNumber::fromString can parse it.
        if (tag.startsWith(QLatin1Char('v')))
            tag = tag.mid(1);

        qsizetype suffix = 0;
        const QVersionNumber remote = QVersionNumber::fromString(tag, &suffix);
        const QVersionNumber local  =
            QVersionNumber::fromString(QApplication::applicationVersion());

        if (remote.isNull() || local.isNull()) {
            qDebug() << "UpdateChecker: could not parse versions:" << tag
                     << QApplication::applicationVersion();
            if (shouldNotify)
                emit checkFailed(QStringLiteral("Could not compare version numbers."));
            return;
        }

        // Reject tags with a pre-release suffix (e.g. "1.2.3-rc1") that
        // fromString accepted but only partially consumed. GitHub's
        // /releases/latest endpoint excludes pre-releases by default, but
        // guard against it anyway rather than silently dropping the suffix.
        if (suffix != tag.size()) {
            qDebug() << "UpdateChecker: tag has pre-release suffix, ignoring:" << tag;
            if (shouldNotify)
                emit checkFailed(QStringLiteral("Could not compare version numbers."));
            return;
        }

        // Persist last-check only after a valid release response is parsed.
        QSettings s;
        s.setValue(kLastCheckKey, QDateTime::currentSecsSinceEpoch());

        if (QVersionNumber::compare(remote, local) > 0) {
            emit updateAvailable(remote.toString(), QUrl(url));
        } else if (shouldNotify) {
            emit upToDate();
        }
    });
}
