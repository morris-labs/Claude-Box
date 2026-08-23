#include "PtySession.h"

#include <QSocketNotifier>

#include <cerrno>
#include <vector>

#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

PtySession::PtySession(QObject *parent)
    : QObject(parent)
{
}

PtySession::~PtySession()
{
    if (m_childPid > 0)
        ::kill(m_childPid, SIGTERM);
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_childPid > 0) {
        int status = 0;
        ::waitpid(m_childPid, &status, 0);
        m_childPid = -1;
    }
    // m_notifier is a QObject child of `this`; Qt destroys it automatically.
}

bool PtySession::isRunning() const
{
    return m_masterFd >= 0;
}

bool PtySession::start(const QString &program, const QStringList &args, const QString &workingDir)
{
    if (isRunning())
        return false;

    // Build everything the child needs to exec *before* forking. Only
    // async-signal-safe calls (chdir, execvp, _exit) should run between
    // fork() and exec() in a process that may have Qt/other background
    // threads -- QString/QByteArray allocation is not guaranteed safe
    // there, so none of it happens after forkpty() below.
    const QByteArray progBytes = program.toLocal8Bit();
    std::vector<QByteArray> argBytesStorage;
    argBytesStorage.reserve(static_cast<size_t>(args.size()));
    for (const QString &a : args)
        argBytesStorage.push_back(a.toLocal8Bit());

    std::vector<char *> argv;
    argv.reserve(argBytesStorage.size() + 2);
    argv.push_back(const_cast<char *>(progBytes.constData()));
    for (auto &b : argBytesStorage)
        argv.push_back(b.data());
    argv.push_back(nullptr);

    const QByteArray workingDirBytes = workingDir.toLocal8Bit();

    struct winsize ws {};
    ws.ws_row = 24;
    ws.ws_col = 80;

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0)
        return false;

    if (pid == 0) {
        // Child: fds 0/1/2 are already the pty slave and it's already our
        // controlling terminal (forkpty() handles setsid()/TIOCSCTTY).
        if (!workingDirBytes.isEmpty())
            ::chdir(workingDirBytes.constData());
        ::execvp(argv[0], argv.data());
        ::_exit(127); // only reached if exec failed
    }

    // Parent.
    m_masterFd = master;
    m_childPid = pid;

    const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
    ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &PtySession::onMasterReadable);

    return true;
}

void PtySession::onMasterReadable()
{
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
            emit dataReady(QByteArray(buf, static_cast<int>(n)));
            if (n < static_cast<ssize_t>(sizeof(buf)))
                break; // drained for now, wait for the next activation
            continue;
        }
        if (n == 0) {
            cleanup(); // EOF: remote side closed
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        if (errno == EINTR)
            continue;
        // EIO on Linux is the normal way a pty master reports that the
        // slave (and thus our child) is gone.
        cleanup();
        break;
    }
}

void PtySession::write(const QByteArray &data)
{
    if (m_masterFd < 0)
        return;

    const char *p = data.constData();
    qint64 remaining = data.size();
    while (remaining > 0) {
        const ssize_t n = ::write(m_masterFd, p, static_cast<size_t>(remaining));
        if (n > 0) {
            p += n;
            remaining -= n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        // EAGAIN (master is non-blocking) or a real error: drop the rest
        // rather than block the UI thread. Fine for keystroke-sized
        // writes; a large paste could theoretically be truncated under
        // heavy backpressure.
        break;
    }
}

void PtySession::resize(int rows, int cols)
{
    if (m_masterFd < 0)
        return;
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ::ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void PtySession::cleanup()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }

    int exitCode = -1;
    if (m_childPid > 0) {
        int status = 0;
        // The child is already dead or dying by the time we see EOF/EIO,
        // so this just reaps it rather than actually blocking.
        if (::waitpid(m_childPid, &status, 0) > 0 && WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
        m_childPid = -1;
    }

    emit finished(exitCode);
}
