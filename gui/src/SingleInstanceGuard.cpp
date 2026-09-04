#include "SingleInstanceGuard.h"

#include <QLocalServer>
#include <QLocalSocket>

namespace {
// Arbitrary but fixed -- every launch that should be treated as "the same
// instance" has to agree on it. Versioned so a future incompatible change
// to the one-byte wire message below can't be misread by an old build's
// listener still hanging around during an upgrade.
const char *kServerName = "claude-box-gui-singleton-v1";
}

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : QObject(parent)
{
    // Ask first: if a primary is already listening, hand it a wake-up and
    // step aside. A short timeout rather than QLocalSocket's ~30s default --
    // the stale-socket-file case below needs this to fail fast, or a second
    // launch would sit here for half a minute before its own window ever
    // appears.
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(kServerName));
    if (probe.waitForConnected(300)) {
        probe.write("raise");
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return; // m_isPrimary stays false -- main() exits without a window
    }

    // Nothing answered. Either this is genuinely the first instance, or a
    // previous one crashed and left its socket file behind (Unix only --
    // listen() below would otherwise fail with AddressInUseError even
    // though nothing is actually listening). removeServer() clears exactly
    // that stale file and is a harmless no-op if there wasn't one.
    QLocalServer::removeServer(QString::fromLatin1(kServerName));

    m_server = new QLocalServer(this);
    if (!m_server->listen(QString::fromLatin1(kServerName))) {
        // Lost a startup race to another launch between the probe above and
        // here. Treat this one as secondary too rather than ending up with
        // two primaries.
        return;
    }

    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *conn = m_server->nextPendingConnection()) {
            // The message itself carries no information worth reading --
            // any connection at all means "someone tried to launch a
            // second copy, please come to front".
            connect(conn, &QLocalSocket::disconnected, conn, &QLocalSocket::deleteLater);
            emit raiseRequested();
        }
    });

    m_isPrimary = true;
}
