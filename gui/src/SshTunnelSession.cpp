#include "SshTunnelSession.h"

#include "BoxRecord.h"

#include <QProcess>

SshTunnelSession::SshTunnelSession(QObject *parent)
    : QObject(parent)
{
}

SshTunnelSession::~SshTunnelSession()
{
    stop();
}

namespace {

// rec.sshHost is "user@host" or "user@host:port" -- ssh itself only takes
// the port via -p, so a trailing ":<digits>" after the last '@' is peeled
// off here rather than handed to ssh as part of the destination.
void splitHostPort(const QString &raw, QString &hostOut, int &portOut)
{
    hostOut = raw;
    portOut = -1;

    const int at = raw.lastIndexOf('@');
    const int colon = raw.lastIndexOf(':');
    if (colon <= at)
        return;

    bool ok = false;
    const int port = raw.mid(colon + 1).toInt(&ok);
    if (!ok || port <= 0 || port > 65535)
        return;

    hostOut = raw.left(colon);
    portOut = port;
}

} // namespace

bool SshTunnelSession::start(const BoxRecord &rec)
{
    stop();

    if (rec.sshForwards.isEmpty() || rec.sshHost.trimmed().isEmpty())
        return true; // nothing configured -- not a failure

    QString host;
    int port = -1;
    splitHostPort(rec.sshHost.trimmed(), host, port);

    QStringList args;
    args << "-N"
         << "-o" << "BatchMode=yes"          // no tty here to answer a prompt with
         << "-o" << "ExitOnForwardFailure=yes" // fail loudly instead of silently missing a forward
         << "-o" << "ServerAliveInterval=15"
         << "-o" << "ServerAliveCountMax=3"
         << "-o" << "StrictHostKeyChecking=accept-new"; // TOFU: trusts a *new* host, still rejects a changed one

    if (!rec.sshIdentity.isEmpty())
        args << "-i" << rec.sshIdentity;
    if (port > 0)
        args << "-p" << QString::number(port);

    for (const QString &fwd : rec.sshForwards) {
        const QStringList parts = fwd.split(':');
        if (parts.size() != 5)
            continue; // malformed -- shouldn't happen via the editor, skip rather than crash the tunnel
        const QString &dir = parts.at(0);
        const QString &bindAddr = parts.at(1);
        const QString &bindPort = parts.at(2);
        const QString &destHost = parts.at(3);
        const QString &destPort = parts.at(4);

        const QString spec = (bindAddr.isEmpty() ? QString() : bindAddr + ":")
            + bindPort + ":" + destHost + ":" + destPort;
        args << (dir == "R" ? "-R" : "-L") << spec;
    }

    args << host;

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        const QString text = QString::fromUtf8(m_process->readAllStandardError());
        for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
            emit log(line);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) { emit finished(exitCode); });

    m_process->start(QStringLiteral("ssh"), args);
    return m_process->waitForStarted(3000);
}

void SshTunnelSession::stop()
{
    if (!m_process)
        return;

    m_process->disconnect();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000))
            m_process->kill();
    }
    m_process->deleteLater();
    m_process = nullptr;
}

bool SshTunnelSession::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}
