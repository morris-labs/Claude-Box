#pragma once

#include <QObject>

class QLocalServer;

// Enforces "only one claude-box-gui at a time". Construction tries to reach
// a running instance over a well-known QLocalSocket name -- the same
// unix-socket-on-Linux/named-pipe-on-Windows abstraction DockerApi already
// uses for the Docker Engine API (see its socketPath() comment), so this
// needs no Q_OS_WIN branch of its own to work on both.
//
// If another instance answers, isPrimary() is false: main() should return
// immediately without ever constructing a MainWindow. The existing
// instance has already been sent a one-byte wake-up and will emit
// raiseRequested() so it can bring its window to front.
//
// If nothing answers, this instance becomes the one every later launch
// will find, for as long as it lives -- there is deliberately no handoff
// when it exits; the next launch after that just becomes primary in turn.
class SingleInstanceGuard : public QObject {
    Q_OBJECT
public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);

    bool isPrimary() const { return m_isPrimary; }

signals:
    // A later launch reached this instance and is asking to be shown
    // instead of starting a second window. Only ever emitted when
    // isPrimary() is true.
    void raiseRequested();

private:
    QLocalServer *m_server = nullptr;
    bool m_isPrimary = false;
};
