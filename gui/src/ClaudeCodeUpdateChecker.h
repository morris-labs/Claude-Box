#pragma once

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>

class DockerBackend;
class QNetworkAccessManager;

// Checks whether the @anthropic-ai/claude-code package installed in the
// claude-code Docker image is behind the latest release on npm.
//
// Needs the DockerBackend to read the installed version from the image
// (via a one-shot docker run). Shares the QNetworkAccessManager owned by
// MainWindow with UpdateChecker.
//
// An image built before /etc/claude-code-version was added to the
// Dockerfile returns an empty installed version; this is treated as
// "out of date" per the design, and the notification is always shown
// for such images so the user rebuilds.
class ClaudeCodeUpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit ClaudeCodeUpdateChecker(const DockerBackend *docker,
                                     QNetworkAccessManager *nam,
                                     QObject *parent = nullptr);

    // Checks only if the last check was more than 24 hours ago.
    void checkInBackground();

    // Checks unconditionally. Emits upToDate() if the image is current.
    void checkNow();

    // Blocks until any in-flight docker thread-pool job finishes. Call
    // from MainWindow::~MainWindow() before m_docker is destroyed.
    void cancelAndWait();

signals:
    // `installed` may be empty when the image has no version file.
    void updateAvailable(const QString &installed, const QString &latest);
    void upToDate();
    void checkFailed(const QString &reason);

private:
    void doCheck(bool emitUpToDate);
    void proceedWithNpmCheck(const QString &installed, bool emitUpToDate);

    const DockerBackend *m_docker;
    QNetworkAccessManager *m_nam;
    QFuture<QString> m_pendingFuture;
    bool m_checkInProgress     = false;
    bool m_pendingRecheck      = false;
    bool m_pendingEmitUpToDate = false;
};
