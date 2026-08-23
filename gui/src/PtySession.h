#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <sys/types.h>

class QSocketNotifier;

// Owns one PTY + child process (used to run `docker attach <name>`).
// Event-driven by design, unlike DockerBackend's synchronous admin calls:
// this is a long-lived connection whose output can arrive at any time.
//
// Closing/destroying a session (or the remote process exiting on its own)
// does not affect the underlying docker container -- `docker attach`
// detaching doesn't stop what it's attached to, only `docker stop` does.
class PtySession : public QObject {
    Q_OBJECT
public:
    explicit PtySession(QObject *parent = nullptr);
    ~PtySession() override;

    // Spawns `program args...` with stdio wired to a fresh PTY, chdir'd to
    // workingDir first if non-empty. Returns false if the PTY/fork setup
    // itself failed (a bad `program` still "succeeds" here and shows up
    // as an immediate `finished` with a non-zero/undefined exit code).
    bool start(const QString &program, const QStringList &args, const QString &workingDir = QString());

    bool isRunning() const;

    void resize(int rows, int cols);

public slots:
    void write(const QByteArray &data);

signals:
    void dataReady(const QByteArray &data);
    void finished(int exitCode);

private slots:
    void onMasterReadable();

private:
    int m_masterFd = -1;
    pid_t m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;

    void cleanup();
};
