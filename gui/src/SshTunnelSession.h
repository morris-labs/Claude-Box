#pragma once

#include <QObject>

class QProcess;
struct SshRemote;

// Runs one background `ssh -N` process implementing every -L/-R forward
// one SshRemote carries (see BoxRecord.h).
//
// Deliberately host-side, not something spawned inside the container: the
// forwards this exists for typically name addresses that only mean
// something from the host's point of view -- the docker bridge gateway,
// a sibling container's own IP -- and putting it here means no box image
// ever needs an ssh client or credentials of its own. One process per
// *remote* (a box can tunnel to several -- a Windows machine and a Mac,
// say -- each getting its own session), covering every forward that
// remote has configured, since ssh happily multiplexes any number of
// -L/-R flags over a single connection.
//
// No pty is involved (ssh -N runs no remote command and needs none), so
// this is a plain QProcess, unlike PtySession. That also means there is
// no way to answer an interactive password/passphrase prompt -- BatchMode
// is forced on, so only key-based auth (an unlocked identity file, or an
// already-running ssh-agent) works headlessly.
class SshTunnelSession : public QObject {
    Q_OBJECT
public:
    explicit SshTunnelSession(QObject *parent = nullptr);
    ~SshTunnelSession() override;

    // Starts the tunnel. Stops whatever this session was already running
    // first. Returns false only if the `ssh` process itself failed to
    // launch (missing binary, etc) -- a bad forward spec or an auth
    // failure shows up later via log()/finished(), since ssh only reports
    // those after it actually tries to connect.
    bool start(const SshRemote &remote);

    void stop();
    bool isRunning() const;

signals:
    // One line of ssh's stderr at a time -- connection/auth/forward
    // failures, mainly. Surfaced rather than swallowed since a tunnel that
    // silently isn't there is a much worse failure mode than a noisy one.
    void log(const QString &line);
    void finished(int exitCode);

private:
    QProcess *m_process = nullptr;
};
