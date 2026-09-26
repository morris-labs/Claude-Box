#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

// Checks the GitHub releases API for a newer version of claude-box-gui.
// Call checkInBackground() once after startup; it respects a 24-hour
// cooldown so repeated launches don't hammer the API. checkNow() bypasses
// the cooldown -- use it for the Help > Check for Updates action.
//
// Network errors and malformed responses are swallowed silently (logged via
// qDebug); the caller only hears about a definite available update.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QNetworkAccessManager *nam, QObject *parent = nullptr);

    // Checks only if the last successful check was more than 24 hours ago.
    void checkInBackground();

    // Checks unconditionally. Emits upToDate() if already on the latest
    // version (for Help > Check for Updates feedback).
    void checkNow();

signals:
    void updateAvailable(const QString &newVersion, const QUrl &releaseUrl);
    void upToDate();   // emitted only by checkNow(), not checkInBackground()
    void checkFailed(const QString &reason); // network/parse error on checkNow()

private:
    void doCheck(bool emitUpToDate);

    QNetworkAccessManager *m_nam;
};
